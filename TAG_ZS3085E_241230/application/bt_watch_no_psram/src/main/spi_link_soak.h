/* Bounded, offline SPI diagnosis. No HTTP requests and no source-file writes. */
#ifndef ATS_SPI_LINK_SOAK_H
#define ATS_SPI_LINK_SOAK_H

struct sfile_soak_job {
    struct sfile_sender sender;
    struct sfile_probe probe;
    uint8_t info[SFILE_DEVICE_INFO_BYTES];
    uint32_t round, phase, clean_rounds, clean_phases, elapsed;
    int result;
};
static struct sfile_soak_job *sfile_soak_retained;
static const char *const sfile_soak_phases[] = { "INFO", "RAM", "QUIET" };

static int sfile_soak_number(const char *arg, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;
    if (!*arg) return -EINVAL;
    while (*arg) {
        unsigned int d = (unsigned char)*arg++ - '0';
        if (d > 9u || n > max / 10u || (n == max / 10u && d > max % 10u)) return -EINVAL;
        n = n * 10u + d;
    }
    *out = n;
    return 0;
}

static void sfile_soak_regs(const struct shell *sh, const char *label, const uint32_t r[7])
{
    shell_print(sh, "SPI SOAK REG %s clk=%08x ctrl=%08x status=%08x", label, r[0], r[1], r[2]);
    shell_print(sh, "SPI SOAK PIN %s 57..60=%08x/%08x/%08x/%08x", label, r[3], r[4], r[5], r[6]);
}

static void sfile_soak_show(const struct shell *sh, const struct sfile_soak_job *j)
{
    static const char *const labels[] = { "before_open", "after_open", "before_close", "after_close" };
    const struct sfile_probe *p = &j->probe;
    unsigned int i, at;
    shell_print(sh, "SPI SOAK FIRST round=%u phase=%s tid=%u ret=%d frame=%u captured=%u bytes_valid=%u",
        j->round, sfile_soak_phases[j->phase], j->sender.tid, j->result, p->frame,
        (unsigned int)p->captured, (unsigned int)p->frame_valid);
    shell_print(sh, "SPI SOAK COUNTERS clean_rounds=%u clean_phases=%u invalid=%u resync=%u recovered=%u resent=%u ms=%u",
        j->clean_rounds, j->clean_phases, p->invalid, p->recoveries, p->recovered, j->sender.resent, j->elapsed);
    for (i = 0; i < 4; ++i) if (p->regs_mask & (1u << i)) sfile_soak_regs(sh, labels[i], p->regs[i]);
    if (p->captured) sfile_soak_regs(sh, "first_fault", p->fault_regs);
    if (!p->frame_valid) return; /* A READY timeout has no new RX frame. */
    for (i = 0; i < 2; ++i) for (at = 0; at < SWAP_BLOCK_SIZE; at += 16u) {
        const uint8_t *b = (i ? p->rx : p->tx) + at;
        shell_print(sh, "SPI SOAK %s off=%03u le32=%08x/%08x/%08x/%08x", i ? "RX" : "TX", at,
            sfile_u32(b), sfile_u32(b + 4), sfile_u32(b + 8), sfile_u32(b + 12));
    }
}

static int shell_spi2soak(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_soak_job *j;
    uint32_t rounds, idle_ms, quiet_ms, start;
    unsigned int n;
    int ret = 0;
    if (argc == 2 && !strcmp(argv[1], "show")) {
        if (sfile_soak_retained) sfile_soak_show(sh, sfile_soak_retained);
        else shell_print(sh, "SPI SOAK no retained fault");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "clear")) {
        if (sfile_soak_retained) k_heap_free(&sys_mem_pool, sfile_soak_retained);
        sfile_soak_retained = NULL;
        shell_print(sh, "SPI SOAK snapshot cleared; hardware quarantine unchanged");
        return 0;
    }
    if (argc != 4 || sfile_soak_number(argv[1], 10u, &rounds) || !rounds ||
        sfile_soak_number(argv[2], 60000u, &idle_ms) || sfile_soak_number(argv[3], 15000u, &quiet_ms))
        return -EINVAL;
    if (sfile_soak_retained) {
        shell_error(sh, "SPI SOAK STOP retained fault; save logs, use show");
        return -EBUSY;
    }
    j = k_heap_alloc(&sys_mem_pool, sizeof(*j), K_NO_WAIT);
    if (!j) { shell_error(sh, "SPI SOAK FAIL allocation; no clocks sent"); return -ENOMEM; }
    memset(j, 0, sizeof(*j));
    start = k_uptime_get_32();
    shell_print(sh, "SPI SOAK BEGIN rounds=%u idle_ms=%u quiet_ms=%u hz=4000000 offline=1",
        rounds, idle_ms, quiet_ms);
    for (j->round = 1; j->round <= rounds; ++j->round) {
        for (j->phase = 0; j->phase < 3; ++j->phase) {
            struct sfile_sender *x = &j->sender;
            if (j->clean_phases && idle_ms) {
                shell_print(sh, "SPI SOAK IDLE ms=%u next_round=%u next_phase=%s",
                    idle_ms, j->round, sfile_soak_phases[j->phase]);
                /* No extra wake lock: exercise ordinary between-transaction idle. */
                k_msleep(idle_ms);
            }
            memset(x, 0, sizeof(*x));
            memset(&j->probe, 0, sizeof(j->probe));
            memset(j->info, 0, sizeof(j->info));
            x->probe = &j->probe; x->bad_block = x->fail_at = UINT32_MAX;
            if (!j->phase) { x->device_info = j->info; x->delivery_command = SFILE_DEVICE_INFO; }
            else {
                x->read_block = sfile_read_generated; x->size = SFILE_DATA_BYTES;
                if (j->phase == 2) {
                    x->delivery_command = SFILE_START_DELIVERY; x->hold_ms = quiet_ms;
                    j->probe.quiet = true;
                }
            }
            shell_print(sh, "SPI SOAK PHASE BEGIN round=%u phase=%s", j->round, sfile_soak_phases[j->phase]);
            ret = sfile_run_context(sh, x, NULL, true, 4000000u);
            if (!ret && !j->phase) {
                if (j->info[0] != 1 || sfile_u32(j->info + 1) < 1735689600u ||
                    sfile_u32(j->info + 1) >= 4102444800u) ret = -EBADMSG;
                for (n = 5; n < SFILE_DEVICE_INFO_BYTES; ++n)
                    if (j->info[n] < '0' || j->info[n] > '9') ret = -EBADMSG;
                /* Match the peer's 2025..2099 UTC range. No TLS is attempted. */
            }
            if (!ret && (j->probe.captured || j->probe.invalid || j->probe.recoveries ||
                         x->resent || x->resync_needed || x->link_failed)) ret = -EBADMSG;
            if (ret) goto failed;
            ++j->clean_phases;
            shell_print(sh, "SPI SOAK PHASE PASS round=%u phase=%s tid=%u frames=%u",
                j->round, sfile_soak_phases[j->phase], x->tid, x->transfers);
        }
        ++j->clean_rounds;
    }
    shell_print(sh, "SPI SOAK RESULT PASS clean_rounds=%u clean_phases=%u resync=0 invalid=0 ms=%u",
        j->clean_rounds, j->clean_phases, (uint32_t)(k_uptime_get_32() - start));
    k_heap_free(&sys_mem_pool, j);
    return 0;
failed:
    j->result = ret; j->elapsed = k_uptime_get_32() - start;
    sfile_soak_retained = j;
    shell_error(sh, "SPI SOAK RESULT FAIL STOP round=%u phase=%s ret=%d; first fault retained",
        j->round, sfile_soak_phases[j->phase], ret);
    sfile_soak_show(sh, j);
    return ret;
}
#endif
