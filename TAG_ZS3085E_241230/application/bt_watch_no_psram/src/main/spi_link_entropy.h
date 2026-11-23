/* TLS seed source for the Leopard SoC; independent of Bluetooth readiness.
 * Use the SDK's OSC+LFSR TRNG configuration, but bound every hardware wait.
 * Never substitute timestamps, fixed bytes, or a non-cryptographic PRNG. */
#ifndef ATS_SPI_LINK_ENTROPY_H
#define ATS_SPI_LINK_ENTROPY_H

#include <drivers/se/se.h>

#define SFILE_TRNG_TIMEOUT_US 2000u
#ifdef CONFIG_SE
#define SFILE_SE_LOCK se_lock
#else
/* This no-PSRAM image omits the AES/CRC driver and its shared mutex. */
static K_MUTEX_DEFINE(sfile_entropy_lock);
#define SFILE_SE_LOCK sfile_entropy_lock
#endif

static int sfile_entropy_sample(uint32_t *low, uint32_t *high)
{
    uint32_t start, control;
    unsigned int key = irq_lock();
    bool clock_owned = !(sys_read32(CMU_DEVCLKEN0) & (1u << CLOCK_ID_SE));
    int ret = 0;

    /* AES/CRC users share se_lock. Older raw TRNG users do not: exclude
     * preemption for this bounded sample and refuse an already active TRNG.
     * Never reset a running security engine or disturb its clock selection. */
    if (clock_owned) {
        sys_write32(0, CMU_SECCLK);
        acts_clock_peripheral_enable(CLOCK_ID_SE);
        acts_reset_peripheral(RESET_ID_SE);
    } else if (sys_read32(TRNG_CTRL) & ((1u << 3) | 1u)) {
        irq_unlock(key);
        return -EBUSY;
    }
    control = (1u << 3) | (1u << 8) | (1u << 9) | (1u << 10) | (2u << 6);
    sys_write32(control, TRNG_CTRL);
    sys_write32(control | 1u, TRNG_CTRL);
    start = k_cycle_get_32();
    while (!(sys_read32(TRNG_CTRL) & (1u << 31))) {
        if (k_cyc_to_us_floor32((uint32_t)(k_cycle_get_32() - start)) >=
            SFILE_TRNG_TIMEOUT_US) {
            ret = -ETIMEDOUT;
            break;
        }
    }
    if (!ret) {
        *low = sys_read32(TRNG_LR);
        *high = sys_read32(TRNG_MR);
    }
    sys_write32(0, TRNG_CTRL);
    if (clock_owned) acts_clock_peripheral_disable(CLOCK_ID_SE);
    irq_unlock(key);
    return ret;
}

static int sfile_entropy_fill(uint8_t *out, size_t length)
{
    static uint32_t previous_low, previous_high;
    static bool previous_valid;
    size_t offset;
    int ret;

    if (!out || !length || (length & 7u)) return -EINVAL;
    memset(out, 0, length);
    ret = k_mutex_lock(&SFILE_SE_LOCK, K_NO_WAIT);
    if (ret) return -EBUSY;
    for (offset = 0; offset < length; offset += 8u) {
        uint32_t low = 0, high = 0;
        unsigned int i;
        ret = sfile_entropy_sample(&low, &high);
        if (ret) break;
        /* Basic stuck-source detection; not a claim of entropy certification. */
        if ((!low && !high) || (low == UINT32_MAX && high == UINT32_MAX) ||
            (previous_valid && low == previous_low && high == previous_high)) {
            ret = -EIO;
            break;
        }
        previous_low = low;
        previous_high = high;
        previous_valid = true;
        for (i = 0; i < 4u; ++i) {
            out[offset + i] = (uint8_t)(low >> (8u * i));
            out[offset + 4u + i] = (uint8_t)(high >> (8u * i));
        }
    }
    k_mutex_unlock(&SFILE_SE_LOCK);
    if (ret) {
        volatile uint8_t *p = out;
        for (offset = 0; offset < length; ++offset) p[offset] = 0;
    }
    return ret;
}

#endif
