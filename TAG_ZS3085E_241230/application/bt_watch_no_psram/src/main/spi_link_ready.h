/* V9 diagnostics on the unchanged V8 one-wire readiness token.
 * UMA GPIO50 -> ATS GPIO61, both 1.8 V.
 * Slave starts low and publishes HIGH only after its first RX/TX arm + prefill.
 * Each subsequent arm toggles the held level once. Consume BEFORE clocking;
 * wait for the opposite level before the next frame. Never reset this state
 * at command boundaries. A failure poisons the link until BOTH boards reboot.
 * GPIO BOTH edges wake the existing task; the held level grants permission.
 * No new frame buffers or worker stacks. Wire protocol remains V7-compatible.
 */
#ifndef SPI_LINK_READY_H_
#define SPI_LINK_READY_H_

#define SWAP_READY_PIN 61u
#define SWAP_READY_TIMEOUT_MS 200u
#if IS_ENABLED(CONFIG_I2C_2) || IS_ENABLED(CONFIG_SPIMT_0) || IS_ENABLED(CONFIG_MMC_0)
#error GPIO61 must be reserved for SPI READY; conflicting board peripheral enabled
#endif

static const struct device *s_ready_gpio;
static uint32_t s_ready_saved_ctl;
static uint8_t s_ready_expected = 1u;
static bool s_ready_used, s_ready_legacy_used, s_ready_faulted, s_ready_configured;
static bool s_ready_cleanup_failed;
static struct gpio_callback s_ready_callback;
static struct k_sem s_ready_sem;
static bool s_ready_callback_added;
static volatile uint32_t s_ready_irq_count;
static uint32_t s_ready_block_count;
static uint64_t s_ready_block_us;
static volatile uint32_t s_ready_irq_cycles;
static struct {
    uint64_t pre_us, post_us;
    uint32_t n, pre_max, post_max, held, unpaired;
} s_ready_diag;

static void swap_ready_irq(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    /* This SDK's GPIO ISR can pass its shared IRQ device, not GPIOB. Only the
     * callback's registered bit mask is authoritative. Never read DMA here. */
    (void)port; (void)cb;
    if (pins & (1u << (SWAP_READY_PIN % 32u))) {
        s_ready_irq_cycles = k_cycle_get_32();
        __asm__ volatile ("dmb" ::: "memory");
        ++s_ready_irq_count;
        k_sem_give(&s_ready_sem);
    }
}

static int swap_ready_restore(void)
{
    int ret = 0, r;
    if (s_ready_configured)
        ret = gpio_pin_interrupt_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INT_DISABLE);
    if (s_ready_callback_added) {
        r = gpio_remove_callback(s_ready_gpio, &s_ready_callback);
        if (!ret) ret = r;
        if (!r) s_ready_callback_added = false;
    }
    if (s_ready_configured) sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));
    s_ready_configured = false;
    if (ret) s_ready_faulted = s_ready_cleanup_failed = true;
    return ret;
}

static int swap_ready_begin_internal(bool boot_probe)
{
    int ret;
    if ((!boot_probe && s_ready_faulted) || s_ready_legacy_used || s_ready_cleanup_failed) return -EPERM;
    s_ready_gpio = device_get_binding(CONFIG_GPIO_B_NAME);
    if (!s_ready_gpio) return -ENODEV;
    s_ready_saved_ctl = sys_read32(GPION_CTL(SWAP_READY_PIN));
    if (s_ready_saved_ctl & (GPIO_CTL_MFP_MASK | GPIO_CTL_GPIO_OUTEN | GPIO_CTL_INTC_EN)) return -EBUSY;
    ret = gpio_pin_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret) {
        sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));
        return ret;
    }
    s_ready_configured = true;
    if ((sys_read32(GPION_CTL(SWAP_READY_PIN)) &
         (GPIO_CTL_MFP_MASK | GPIO_CTL_GPIO_OUTEN | GPIO_CTL_GPIO_INEN)) != GPIO_CTL_GPIO_INEN) {
        sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));
        s_ready_configured = false;
        return -EIO;
    }
    k_sem_init(&s_ready_sem, 0, 1);
    s_ready_irq_count = s_ready_block_count = 0;
    s_ready_block_us = 0;
    s_ready_irq_cycles = 0;
    s_ready_diag.pre_us = s_ready_diag.post_us = 0;
    s_ready_diag.n = s_ready_diag.pre_max = s_ready_diag.post_max = 0;
    s_ready_diag.held = s_ready_diag.unpaired = 0;
    gpio_init_callback(&s_ready_callback, swap_ready_irq, 1u << (SWAP_READY_PIN % 32u));
    ret = gpio_add_callback(s_ready_gpio, &s_ready_callback);
    if (!ret) {
        s_ready_callback_added = true;
        ret = gpio_pin_interrupt_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INT_EDGE_BOTH);
    }
    if (ret) { (void)swap_ready_restore(); return ret; }
    s_ready_used = true;
    return 0;
}

static int swap_ready_begin(void)
{
    return swap_ready_begin_internal(false);
}

#ifdef SPI_LINK_BOOT_SYNC
/* No clocks during observation. A live, quiescent session can publish at most
 * one final token; only a fresh boot emits repeated 40 ms half-periods. This
 * is a permission to try the CRC-checked boot handshake, not proof of a peer.
 * Never clear fault state or change token parity merely for a held level. */
static int swap_ready_boot_probe(void)
{
    uint32_t start = k_uptime_get_32(), edge = start;
    unsigned int edges = 0;
    int previous = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);
    if (previous < 0) return previous;
    while ((uint32_t)(k_uptime_get_32() - start) < 320u) {
        uint32_t now;
        int level;
        k_msleep(1);
        now = k_uptime_get_32();
        level = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);
        if (level < 0) return level;
        if (level != previous) {
            uint32_t width = now - edge;
            if (edges && (width < 25u || width > 65u)) return -EIO;
            ++edges; edge = now; previous = level;
            if (edges >= 4u && level == 1) return 1;
        }
    }
    return edges > 1u ? -EIO : 0;
}
#endif

/* Also used after the last transfer: observe the pending token without
 * consuming it, so PASS includes the slave's final successful rearm. */
static int swap_ready_wait_timeout(uint32_t *wait_us, uint32_t timeout_ms)
{
    uint32_t start_ms = k_uptime_get_32(), start_cycles = k_cycle_get_32();
    uint32_t paired = 0, pair_gen = 0, pre = 0, post = 0, blocks = 0;
    int ret;
    *wait_us = 0;
    if (!s_ready_configured || s_ready_faulted) return -EPERM;
    for (;;) {
        uint32_t elapsed = k_uptime_get_32() - start_ms, block_cycles, block_gen;
        uint32_t resume_cycles, resume_gen, irq_cycles;
        int take_ret;
        if (elapsed > timeout_ms) { ret = -ETIMEDOUT; break; }
        ret = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);
        if (ret < 0) break;
        if ((unsigned int)ret == s_ready_expected) {
            /* Only a single, fresh IRQ inside this successful block has a
             * meaningful scheduling interval. Held levels, lost/coalesced
             * edges and stale hints still obey the original token rules. */
            if (paired && pair_gen == s_ready_irq_count) {
                ++s_ready_diag.n;
                s_ready_diag.pre_us += pre;
                s_ready_diag.post_us += post;
                if (pre > s_ready_diag.pre_max) s_ready_diag.pre_max = pre;
                if (post > s_ready_diag.post_max) s_ready_diag.post_max = post;
            } else if (!blocks) ++s_ready_diag.held;
            else ++s_ready_diag.unpaired;
            ret = 0;
            break;
        }
        paired = 0;
        elapsed = k_uptime_get_32() - start_ms;
        if (elapsed >= timeout_ms) {
            ret = -ETIMEDOUT;
            break;
        }
        /* No reset between level check and take: an edge in this gap must
         * remain pending. A stale/spurious notification only triggers a new
         * level check. Remaining time always uses the original deadline. */
        block_cycles = k_cycle_get_32();
        block_gen = s_ready_irq_count;
        ++s_ready_block_count;
        ++blocks;
        take_ret = k_sem_take(&s_ready_sem, K_MSEC(timeout_ms - elapsed));
        resume_cycles = k_cycle_get_32();
        resume_gen = s_ready_irq_count;
        __asm__ volatile ("dmb" ::: "memory");
        irq_cycles = s_ready_irq_cycles;
        __asm__ volatile ("dmb" ::: "memory");
        if (!take_ret && resume_gen == s_ready_irq_count &&
            resume_gen - block_gen == 1u &&
            (uint32_t)(irq_cycles - block_cycles) <= (uint32_t)(resume_cycles - block_cycles)) {
            paired = 1;
            pair_gen = resume_gen;
            pre = k_cyc_to_us_floor32(irq_cycles - block_cycles);
            post = k_cyc_to_us_floor32(resume_cycles - irq_cycles);
        }
        s_ready_block_us += k_cyc_to_us_floor32(resume_cycles - block_cycles);
    }
    *wait_us = k_cyc_to_us_floor32(k_cycle_get_32() - start_cycles);
    if (ret) s_ready_faulted = true;
    return ret;
}

static int swap_ready_wait(uint32_t *wait_us)
{
    return swap_ready_wait_timeout(wait_us, SWAP_READY_TIMEOUT_MS);
}

static void swap_ready_consume(void)
{
    s_ready_expected ^= 1u;
}

static int swap_ready_end(int result)
{
    int ret;
    if (result) s_ready_faulted = true;
    ret = swap_ready_restore();
    return result ? result : ret;
}
#endif
