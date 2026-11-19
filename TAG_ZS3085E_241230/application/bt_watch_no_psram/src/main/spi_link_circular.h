/* V11 matched peer only: 512 bytes, two-slot DMA ring, ACK delay = 2 blocks.
 * READY remains one credit per block. First two replies are pipeline history.
 */
#ifndef ATS_SPI_LINK_CIRCULAR_H_
#define ATS_SPI_LINK_CIRCULAR_H_
#define CIRC_MASTER 0xc3u
#define CIRC_SLAVE 0x3cu
#define CIRC_ACK_DELAY 2u
static bool s_swap_file_used;

static int shell_spi2circ(const struct shell *sh, size_t argc, char *argv[])
{
    static uint16_t sid;
    struct spi_config config = {
        .frequency = 4000000u,
        .operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8),
    };
    const struct device *dev;
    unsigned long count = 1, gap = 0, hz = 4000000u;
    unsigned int i, good = 0, attempts = 0;
    uint32_t start, elapsed, waited;
    uint64_t wait_total = 0;
    char *end;
    int ret, close_ret;
    if (argc > 4) return -EINVAL;
    if (argc > 1) {
        count = strtoul(argv[1], &end, 10);
        if (!argv[1][0] || *end || count < 1 || count > 10000) return -EINVAL;
    }
    if (argc > 2) {
        gap = strtoul(argv[2], &end, 10);
        if (!argv[2][0] || *end || gap > 1000) return -EINVAL;
    }
    if (argc > 3) {
        hz = strtoul(argv[3], &end, 10);
        if (!argv[3][0] || *end || hz < 100000 || hz > 8000000) return -EINVAL;
    }
    if (s_swap_file_used || s_ready_legacy_used || (s_ready_used && !s_swap_circular_used)) return -EPERM;
    dev = device_get_binding(CONFIG_SPI_2_NAME);
    if (!dev) return -ENODEV;
    config.frequency = hz;
    sys_wake_lock(FULL_WAKE_LOCK);
    ret = swap_ready_begin();
    if (ret) {
        if (ret == -EPERM)
            shell_error(sh, "SPI11 BLOCKED ret=%d; reboot both peers after a link fault", ret);
        else
            shell_error(sh, "SPI11 READY begin failed ret=%d", ret);
        sys_wake_unlock(FULL_WAKE_LOCK);
        return ret;
    }
    s_swap_circular_used = true;
    if (++sid == 0) ++sid;
    shell_print(sh, "SPI11 START sid=%u target=%u bytes=512 payload=480 ack_delay=2 hz=%u gap_ms=%u",
                sid, (unsigned int)count, (unsigned int)hz, (unsigned int)gap);
    start = k_uptime_get_32();
    ret = spi_acts_spi2_circular_open(dev, &config);
    if (ret) goto end;
    for (i = 0; i < count + CIRC_ACK_DELAY; ++i) {
        swap_block_frame(s_swap_tx, CIRC_MASTER, sid, (uint16_t)i);
        ret = swap_ready_wait(&waited);
        wait_total += waited;
        if (ret) break;
        swap_ready_consume();
        ++attempts;
        ret = spi_acts_spi2_circular_transfer(dev, s_swap_tx, s_swap_rx);
        if (ret) break;
        ret = swap_block_check(s_swap_rx, CIRC_SLAVE);
        if (ret || (i >= CIRC_ACK_DELAY && (swap_u16(s_swap_rx + 2) != sid ||
            swap_block_sequence(s_swap_rx) != i - CIRC_ACK_DELAY))) {
            shell_error(sh, "SPI11 ACK fail at=%u chk=%d sid=%u seq=%u", i, ret,
                        swap_u16(s_swap_rx + 2), swap_block_sequence(s_swap_rx));
            shell_print(sh, "SPI11 RX headers frame0/frame1 (16 bytes each):");
            shell_hexdump(sh, s_swap_rx, 16);
            shell_hexdump(sh, s_swap_rx + SWAP_SIZE, 16);
            ret = -EIO; break;
        }
        if (i >= CIRC_ACK_DELAY) ++good;
        if (gap && i + 1u < count + CIRC_ACK_DELAY) k_msleep(gap);
    }
    if (!ret) {
        ret = swap_ready_wait(&waited); /* Include the peer's final processing. */
        wait_total += waited;          /* Keep this token for the next command. */
    }
    close_ret = spi_acts_spi2_circular_close(dev);
    if (!ret) ret = close_ret;
end:
    ret = swap_ready_end(ret);
    elapsed = k_uptime_get_32() - start;
    shell_print(sh, "SPI11 RESULT sid=%u good=%u target=%u tx_blocks=%u ms=%u payload_Bps=%u ready_us=%llu ret=%d %s",
                sid, good, (unsigned int)count, attempts, elapsed,
                elapsed ? (unsigned int)((uint64_t)good * SWAP_BLOCK_PAYLOAD * 1000u / elapsed) : 0u,
                (unsigned long long)wait_total, ret, ret ? "FAIL" : "PASS");
    sys_wake_unlock(FULL_WAKE_LOCK);
    return ret;
}
#endif
