#ifndef ATS_SPI_LINK_FILE_H
#define ATS_SPI_LINK_FILE_H

#include <fs/fs.h>
#include "spi_link_file_protocol.h"
#include "spi_link_entropy.h"

#define SFILE_READY_TIMEOUT_MS 2000u
#define SFILE_DEFAULT_HZ 4000000u
/* Retry cellular/TLS acceptance below 8 MHz after slave RX FIFO overrun.
 * SPI2 uses 32 MHz HOSC with maximum /14: 1 MHz is unsupported.
 * 4 MHz is an exact /8 clock; mitigation still requires hardware validation. */
#define SFILE_HTTPS_HZ 4000000u

extern struct k_heap sys_mem_pool;

#ifdef P4_SPI_DIAG
struct sfile_diag_sample {
    uint32_t regs[7], tx[4], rx[4], transfer_us, command;
};
struct sfile_diag {
    struct sfile_diag_sample samples[2];
    uint32_t initial[7], waited_us;
    unsigned int count;
};
static void sfile_diag_regs(uint32_t regs[7])
{
    unsigned int i;
    regs[0] = sys_read32(CMU_SPI2CLK);
    regs[1] = sys_read32(SPI2_REG_BASE);
    regs[2] = sys_read32(SPI2_REG_BASE + 4);
    for (i = 0; i < 4; ++i) regs[3 + i] = sys_read32(GPIO_REG_BASE + (57u + i) * 4u);
}
#endif

struct sfile_http_files {
    uint8_t radio_test; /* Explicit test image only: 1 before init, 2 mid PUT. */
    struct fs_file_t spec, result;
    const uint8_t *spec_data; /* RAM request; NULL retains file-backed P4 path. */
    uint16_t spec_size, http_code, response_size;
    bool console_result;
    int (*receive)(void *context, uint32_t offset, const uint8_t *data, unsigned int len);
    void *receive_context;
    bool response_complete, link_failed;
    bool transport_fault; /* Includes a recovered CRC/RESYNC incident. */
    unsigned int wire_status;
#ifdef P4_SPI_DIAG
    struct sfile_diag *diag;
#endif
};

#ifdef P4_SPI_SOAK
/* Heap-owned by spi2soak, never placed on the 2 KiB UART shell stack. */
struct sfile_probe {
    uint32_t regs[4][7], fault_regs[7], regs_mask;
    uint8_t tx[SWAP_BLOCK_SIZE], rx[SWAP_BLOCK_SIZE];
    uint32_t frame, invalid, recoveries, recovered;
    int error;
    bool captured, frame_valid, quiet;
};
#endif
struct sfile_sender {
#ifdef P4_SPI_SOAK
    struct sfile_probe *probe;
#endif
    struct sfile_http_files *http;
    uint8_t *device_info;
    const struct shell *shell;
    const char *url;
    const struct device *dev;
    int (*read_block)(void *context, uint32_t index, uint8_t *data,
                      unsigned int len, bool repair);
    void *source;
    uint32_t tid, size, crc, transfers, resent, groups, bad_block;
    uint32_t last_index, peak_index, peak_ready_us;
    uint32_t ready_timeout_ms, peak_timeout_ms;
    uint64_t ready_us, data_ready_us;
    uint16_t request;
    uint8_t last_type, peak_type;
    unsigned int pairs;
    uint32_t hold_ms, fail_at;
    uint32_t group_busy, group_wait_ms, group_peak_ms;
    unsigned int invalid_replies;
    bool resync_needed, resync_active;
    uint8_t delivery_command;
    bool link_failed, repaired_bad_block, paired, half_pair;
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once, witness_notice;
    uint8_t witness_frames;
#endif
};

#ifdef P4_SPI_SOAK
static void sfile_probe_fault(struct sfile_sender *x, int error, bool frame_valid)
{
    struct sfile_probe *p = x->probe;
    if (!p || p->captured) return;
    p->captured = true; p->error = error; p->frame = x->transfers;
    p->frame_valid = frame_valid;
    sfile_diag_regs(p->fault_regs);
    if (frame_valid) {
        memcpy(p->tx, s_swap_tx, SWAP_BLOCK_SIZE);
        memcpy(p->rx, s_swap_rx, SWAP_BLOCK_SIZE);
    }
}
static void sfile_probe_regs(struct sfile_sender *x, unsigned int phase)
{
    if (!x->probe) return;
    sfile_diag_regs(x->probe->regs[phase]);
    x->probe->regs_mask |= 1u << phase;
}
#else
#define sfile_probe_regs(x, phase) ((void)0)
#endif
static bool sfile_pair_mode;

static int sfile_wait_ready(struct sfile_sender *x)
{
    uint32_t waited;
    uint32_t timeout = x->ready_timeout_ms ? x->ready_timeout_ms : SFILE_READY_TIMEOUT_MS;
    int ret = swap_ready_wait_timeout(&waited, timeout);
#ifdef P4_SPI_DIAG
    if (!ret && x->http && x->http->diag && waited >= 100000u && !x->http->diag->waited_us)
        x->http->diag->waited_us = waited;
#endif
    x->ready_us += waited;
    if (x->last_type == SFILE_DATA) x->data_ready_us += waited;
    if (waited > x->peak_ready_us) {
        x->peak_ready_us = waited;
        x->peak_type = x->last_type;
        x->peak_index = x->last_index;
        x->peak_timeout_ms = timeout;
    }
    return ret;
}

static int sfile_exchange(struct sfile_sender *x)
{
    int ret = x->half_pair ? 0 : sfile_wait_ready(x);
    if (!ret) {
#ifdef P4_SPI_WITNESS_TEST
        bool inject = false;
#endif
        if (!x->half_pair) swap_ready_consume();
        if (s_swap_tx[2] != SFILE_IDLE) {
            x->last_type = s_swap_tx[2];
            x->last_index = sfile_u32(s_swap_tx + 8);
        }
        if (x->paired) {
            uint32_t witness = SFILE_PAIR_WITNESS;
#ifdef P4_SPI_WITNESS_TEST
            /* Only the DEVICE_INFO/QUERY pair. Count completed transfers below. */
            inject = x->witness_once &&
                ((!x->half_pair && !x->witness_frames && s_swap_tx[2] == SFILE_DEVICE_INFO) ||
                 (x->half_pair && x->witness_frames == 1 && s_swap_tx[2] == SFILE_QUERY));
            if (inject) witness ^= 1u;
#endif
            sfile_put32(s_swap_tx + SFILE_PAIR_WITNESS_OFFSET, witness);
        }
        ++x->transfers;
#ifdef P4_SPI_DIAG
        /* Sample only the first credited pair after a long QUIET. No UART or
         * extra waits on the wire path; print the retained snapshot at teardown. */
        struct sfile_diag *d = x->http ? x->http->diag : NULL;
        struct sfile_diag_sample *sample = d && d->waited_us && d->count < 2 ? &d->samples[d->count] : NULL;
        uint32_t transfer_start = 0;
        if (sample) {
            sfile_diag_regs(sample->regs);
            memcpy(sample->tx, s_swap_tx, sizeof(sample->tx));
            sample->command = s_swap_tx[2];
            transfer_start = k_cycle_get_32();
        }
#endif
        ret = spi_acts_spi2_circular_transfer(x->dev, s_swap_tx, s_swap_rx);
#ifdef P4_SPI_WITNESS_TEST
        if (!ret && inject) ++x->witness_frames;
#endif
#ifdef P4_SPI_SOAK
        if (x->probe) {
            bool invalid = !sfile_valid(s_swap_rx, SFILE_SLAVE) && swap_block_check(s_swap_rx, 0x3cu);
            bool recovery = sfile_valid(s_swap_rx, SFILE_SLAVE) && s_swap_rx[2] == SFILE_LINK_RECOVERY;
            if (invalid) {
                ++x->probe->invalid;
                /* Diagnostic run stops at the first bad reply, draining the
                 * credited pair before the existing bounded recovery path. */
                if (x->paired) x->resync_needed = true;
            }
            if (ret || invalid || recovery)
                sfile_probe_fault(x, ret ? ret : -EBADMSG, true);
        }
#endif
        /* A locally rebuilt slave cannot trust the failed request identity.
         * This CRC-checked notice only aborts business; it is NEVER an ACK.
         * Drain the second credit, then require a fresh RESYNC handshake. */
        if (!ret && x->paired && sfile_valid(s_swap_rx, SFILE_SLAVE) &&
            s_swap_rx[2] == SFILE_LINK_RECOVERY &&
            !sfile_u32(s_swap_rx + 4) && !sfile_u32(s_swap_rx + 8) &&
            !swap_u16(s_swap_rx + 12) && swap_u16(s_swap_rx + 14) == 4 &&
            sfile_u32(s_swap_rx + SFILE_HEADER) == SFILE_RECOVERY_COOKIE) {
#ifdef P4_SPI_WITNESS_TEST
            if (!inject && x->witness_frames == 2 && !x->resync_active)
                x->witness_notice = true;
#endif
            if (!x->resync_needed)
                shell_error(x->shell, "SPI RESYNC required tid=%u peer RX boundary reset", x->tid);
            x->resync_needed = true;
        }
        if (!ret && x->paired && !sfile_valid(s_swap_rx, SFILE_SLAVE) &&
            swap_block_check(s_swap_rx, 0x3cu))
            ++x->invalid_replies;
#ifdef P4_SPI_DIAG
        if (sample) {
            sample->transfer_us = k_cyc_to_us_floor32(k_cycle_get_32() - transfer_start);
            memcpy(sample->rx, s_swap_rx, sizeof(sample->rx));
            ++d->count;
        }
#endif
        if (s_swap_tx[2] == SFILE_START_HTTPS || s_swap_tx[2] == SFILE_START_HTTP) {
            volatile uint8_t *seed = s_swap_tx + SFILE_HEADER + 4;
            unsigned int i;
            for (i = 0; i < SFILE_HTTPS_SEED_BYTES; ++i) seed[i] = 0;
        }
        if (!ret && x->paired) {
            x->half_pair = !x->half_pair;
            if (!x->half_pair) ++x->pairs;
        }
    }
#ifdef P4_SPI_SOAK
    if (ret) sfile_probe_fault(x, ret, false);
#endif
    if (ret) x->link_failed = true;
    if (!ret && !x->half_pair && x->resync_needed && !x->resync_active)
        return -EBADMSG;
    return ret;
}

static bool sfile_ack_matches(const struct sfile_sender *x, uint8_t command,
                              uint32_t index, uint16_t request)
{
    unsigned int len = swap_u16(s_swap_rx + 14);
    bool http_result = (x->url || x->http) && command == SFILE_FINISH &&
        len >= SFILE_HTTP_RESULT_HEAD && len <= SFILE_HTTP_RESULT_HEAD + SFILE_HTTP_RESPONSE_MAX;
    bool http_page = x->http && command == SFILE_HTTP_READ &&
        len > SFILE_ACK_BYTES && len <= SFILE_ACK_BYTES + SFILE_HTTP_PAGE_BYTES;
    bool info = x->device_info && command == SFILE_DEVICE_INFO &&
        len == SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES;
    return s_swap_rx[2] == SFILE_ACK && sfile_u32(s_swap_rx + 4) == x->tid &&
        sfile_u32(s_swap_rx + 8) == index && swap_u16(s_swap_rx + 12) == request &&
        (len == SFILE_ACK_BYTES || http_result || http_page || info) &&
        s_swap_rx[SFILE_HEADER] == command && sfile_valid(s_swap_rx, SFILE_SLAVE);
}

/* The peer switches to two credits after these exact three single blocks.
 * On an ambiguous reply, quarantine the link instead of guessing its mode. */
static int sfile_pair_begin(struct sfile_sender *x, uint8_t command,
                            const uint8_t *body, unsigned int len)
{
    unsigned int i;
    uint16_t request = ++x->request;
    int ret;

    memcpy(s_swap_tx + SFILE_HEADER, body, len);
    for (i = 0; i < 3; ++i) {
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : command,
                    x->tid, 0, request, i ? 0 : len);
        ret = sfile_exchange(x);
        if (ret) return ret;
    }
    if (!sfile_ack_matches(x, command, 0, request)) {
        x->link_failed = true;
        return -EBADMSG;
    }
    ret = s_swap_rx[SFILE_HEADER + 1];
    if (!ret && x->device_info) {
        if (swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES)
            return -EBADMSG;
        memcpy(x->device_info, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, SFILE_DEVICE_INFO_BYTES);
    }
    if (!ret) sfile_pair_mode = x->paired = true;
    return ret;
}

static int sfile_control(struct sfile_sender *x, uint8_t command, uint32_t index,
                         const uint8_t *body, unsigned int len)
{
    uint32_t start = k_uptime_get_32();
    bool delivery_group = x->delivery_command && command == SFILE_GROUP_END;
    uint32_t timeout = command == SFILE_FINISH ? 120000u :
                       (delivery_group ? ((x->url || x->http) ? 35000u : 20000u) : 2000u);
    uint16_t request;
    unsigned int i = 0;
    bool verifying = false;
    if (++x->request == 0) ++x->request;
    request = x->request;
    do {
        bool send_command = !verifying && (i++ % 4u == 0);
        int ret;
        if (send_command && len) memcpy(s_swap_tx + SFILE_HEADER, body, len);
        sfile_frame(s_swap_tx, SFILE_MASTER, send_command ? command : SFILE_QUERY,
                    x->tid, index, request, send_command ? len : 0);
        /* The peer holds READY during network work after a complete pair.
         * Share the existing operation deadline, never extend it per poll. */
        if ((x->url || x->http
#ifdef P4_SPI_SOAK
             || (x->probe && x->probe->quiet)
#endif
            ) && (delivery_group || command == SFILE_FINISH) && i > 1u) {
            uint32_t elapsed = (uint32_t)(k_uptime_get_32() - start);
            x->ready_timeout_ms = elapsed < timeout ? timeout - elapsed : 1u;
        }
        ret = sfile_exchange(x);
        /* Drain BOTH credits before stopping or requesting resynchronization.
         * Never accept a rotated/bad CRC frame, even if its fields look right. */
        if (!ret && x->paired && !x->half_pair && x->invalid_replies >= 4u) {
            x->resync_needed = true;
            shell_error(x->shell, "SPI RESYNC required tid=%u invalid=%u rx=%08x/%08x",
                        x->tid, x->invalid_replies, sfile_u32(s_swap_rx), sfile_u32(s_swap_rx + 4));
            return -EBADMSG;
        }
        x->ready_timeout_ms = 0;
        if (ret) return ret;
        if (!x->half_pair && sfile_ack_matches(x, command, index, request)) {
            ret = s_swap_rx[SFILE_HEADER + 1];
            if (ret != SFILE_BUSY || (command != SFILE_FINISH && !delivery_group)) {
                unsigned int len = swap_u16(s_swap_rx + 14);
                if (!ret && x->device_info && command == SFILE_DEVICE_INFO) {
                    if (len != SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES) return -EBADMSG;
                    memcpy(x->device_info, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, SFILE_DEVICE_INFO_BYTES);
                }
                if (x->http && command == SFILE_FINISH &&
                    (ret == SFILE_OK || ret == SFILE_HTTP_FAILED)) {
                    if (len != SFILE_HTTP_RESULT_HEAD + 2u) return -EBADMSG;
                    x->http->http_code = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES);
                    x->http->response_size = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_HTTP_RESULT_HEAD);
                    if (x->http->response_size > SFILE_HTTP_BODY_MAX ||
                        (ret == SFILE_OK && (x->http->http_code < 200 || x->http->http_code >= 300)))
                        return -EBADMSG;
                }
                if (x->url && command == SFILE_FINISH) {
                    unsigned int code = 0;
                    if (len >= SFILE_HTTP_RESULT_HEAD) {
                        uint8_t *body = s_swap_rx + SFILE_HEADER + SFILE_HTTP_RESULT_HEAD;
                        unsigned int n = len - SFILE_HTTP_RESULT_HEAD, j;
                        code = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES);
                        for (j = 0; j < n; ++j)
                            if (body[j] < 32 || body[j] > 126) body[j] = ' ';
                        body[n] = 0;
                        shell_print(x->shell, "SPI HTTPS tid=%u http=%u response=%s",
                                    x->tid, code, (const char *)body);
                    }
                    if (ret == SFILE_OK && (code < 200 || code >= 300)) return -EBADMSG;
                }
                return ret;
            }
            if (delivery_group) ++x->group_busy;
            verifying = true;
        }
        if ((delivery_group || ((x->url || x->http) && command == SFILE_FINISH)) &&
            verifying && !x->half_pair) k_msleep(20);
    } while (x->half_pair || (uint32_t)(k_uptime_get_32() - start) < timeout);
    return -ETIMEDOUT;
}

/* One bounded recovery, not an upload retry. Old responses are ignored ONLY
 * while sending the reset pair; the next pair must contain two fresh, normal
 * CRC-checked reset ACKs. Leave the operation failed so P4 retries via init. */
static int sfile_resync(struct sfile_sender *x)
{
    unsigned int i;
    uint16_t request;
    int ret, invalid = 0;
    if (!x->paired || x->half_pair || x->link_failed) return -EPERM;
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recoveries;
#endif
    x->resync_active = true;
    if (++x->request == 0) ++x->request;
    request = x->request;
    shell_print(x->shell, "SPI RESYNC begin tid=%u request=%u", x->tid, request);
    x->ready_timeout_ms = 35000u;
    for (i = 0; i < 2; ++i) {
        sfile_put32(s_swap_tx + SFILE_HEADER, SFILE_RESYNC_COOKIE);
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : SFILE_LINK_RESYNC,
                    x->tid, 0, request, i ? 0 : 4);
        ret = sfile_exchange(x);
        if (ret) return ret;
    }
    /* The next READY is published only after slave STOP/reset/rearm. */
    for (i = 0; i < 2; ++i) {
        sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_QUERY, x->tid, 0, request, 0);
        ret = sfile_exchange(x);
        if (ret) return ret;
        if (!sfile_valid(s_swap_rx, SFILE_SLAVE) || s_swap_rx[2] != SFILE_ACK ||
            sfile_u32(s_swap_rx + 4) != x->tid || sfile_u32(s_swap_rx + 8) ||
            swap_u16(s_swap_rx + 12) != request ||
            swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + 4u ||
            s_swap_rx[SFILE_HEADER] != SFILE_LINK_RESYNC || s_swap_rx[SFILE_HEADER + 1] ||
            sfile_u32(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES) != SFILE_RESYNC_COOKIE)
            invalid = 1;
    }
    x->ready_timeout_ms = 0;
    if (invalid) return -EBADMSG;
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recovered;
#endif
    x->invalid_replies = 0; x->resync_needed = x->resync_active = false;
    shell_print(x->shell, "SPI RESYNC PASS tid=%u; retry same file ID", x->tid);
    return 0;
}

#ifdef SPI_LINK_BOOT_SYNC
static bool sfile_boot_hard_fault;
/* Three SINGLE blocks. First two must be CRC-checked boot notices; the third
 * must acknowledge this fresh transaction/challenge, before paired mode is
 * committed. No HTTP, device-time or storage operation is part of this path. */
static int sfile_boot_attempt(struct sfile_sender *x, unsigned int *failed_frame)
{
    uint32_t challenge = k_cycle_get_32() | 1u;
    unsigned int i;
    uint16_t request = ++x->request;
    int ret;
    x->paired = x->half_pair = false;
    s_ready_expected = 1u;
    s_ready_faulted = false; /* Provisional; any failure quarantines again. */
    shell_print(x->shell, "SPI BOOT SYNC begin tid=%u", x->tid);
    for (i = 0; i < 3u; ++i) {
        *failed_frame = i + 1u;
        sfile_put32(s_swap_tx + SFILE_HEADER, SFILE_BOOT_COOKIE);
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : SFILE_LINK_BOOT,
                    x->tid, challenge, request, i ? 0 : 4);
        ret = sfile_exchange(x);
        if (ret) return ret;
        if (!sfile_valid(s_swap_rx, SFILE_SLAVE)) return -EBADMSG;
        if (i < 2u) {
            if (s_swap_rx[2] != SFILE_LINK_BOOT || sfile_u32(s_swap_rx + 4) ||
                sfile_u32(s_swap_rx + 8) || swap_u16(s_swap_rx + 12) ||
                swap_u16(s_swap_rx + 14) != 4u ||
                sfile_u32(s_swap_rx + SFILE_HEADER) != SFILE_BOOT_COOKIE) return -EBADMSG;
        } else if (s_swap_rx[2] != SFILE_ACK || sfile_u32(s_swap_rx + 4) != x->tid ||
                   sfile_u32(s_swap_rx + 8) != challenge || swap_u16(s_swap_rx + 12) != request ||
                   swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + 4u ||
                   s_swap_rx[SFILE_HEADER] != SFILE_LINK_BOOT || s_swap_rx[SFILE_HEADER + 1] ||
                   sfile_u32(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES) != SFILE_BOOT_COOKIE)
            return -EBADMSG;
    }
    ret = sfile_wait_ready(x); /* Slave has completed all three and rearmed. */
    if (ret) return ret;
    sfile_pair_mode = x->paired = true;
    shell_print(x->shell, "SPI BOOT SYNC PASS tid=%u", x->tid);
    return 0;
}

/* A partial boot exchange has no business worker and no paired credits.
 * The companion slave abandons stage 1/2 after 2 s and resets only after a
 * stable idle boundary + confirmed STOP. Observe its NEW beacon before any
 * retry clocks. Never retry a transport/STOP error, or a third-frame error
 * (the slave could already be paired). No byte/bit correction is permitted.
 * Return 1 after recovery: caller preserves FAIL for the interrupted task. */
static int sfile_boot_sync(struct sfile_sender *x)
{
    unsigned int failed_frame = 0;
    int ret = sfile_boot_attempt(x, &failed_frame);
    if (ret != -EBADMSG || x->link_failed || failed_frame > 2u) return ret;
    shell_error(x->shell, "SPI BOOT RECOVER wait frame=%u ret=%d; no clocks for 2200 ms",
                failed_frame, ret);
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recoveries;
#endif
    k_msleep(2200);
    ret = swap_ready_boot_probe();
    if (ret != 1) {
        shell_error(x->shell, "SPI BOOT RECOVER FAIL no fresh beacon ret=%d", ret);
        return ret < 0 ? ret : -EBADMSG;
    }
    /* New challenge and request; both notice CRCs and ACK identity checked. */
    ret = sfile_boot_attempt(x, &failed_frame);
    if (ret) {
        shell_error(x->shell, "SPI BOOT RECOVER FAIL retry frame=%u ret=%d", failed_frame, ret);
        return ret;
    }
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recovered;
#endif
    shell_print(x->shell, "SPI BOOT RECOVER PASS; current task FAIL, retry same file ID");
    return 1;
}
#endif

static int sfile_end_pair(struct sfile_sender *x)
{
    if (!x->half_pair) return 0;
    sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_IDLE, x->tid, 0, 0, 0);
    return sfile_exchange(x);
}

static int sfile_read_generated(void *context, uint32_t index, uint8_t *data,
                                unsigned int len, bool repair)
{
    unsigned int i;
    (void)context;
    (void)repair;
    for (i = 0; i < len; ++i)
        data[i] = (uint8_t)(index ^ (index >> 8) ^ (i * 29u));
    return 0;
}

struct sfile_file_source {
    struct fs_file_t file;
    uint32_t offset, position;
};

static int sfile_read_file(void *context, uint32_t index, uint8_t *data,
                           unsigned int len, bool repair)
{
    struct sfile_file_source *source = context;
    uint32_t start = source->offset + index * SFILE_DATA_BYTES;
    (void)repair;
    if (source->position != start && fs_seek(&source->file, start, FS_SEEK_SET))
        return -EIO;
    if (fs_read(&source->file, data, len) != (ssize_t)len) return -EIO;
    source->position = start + len;
    return 0;
}

static int sfile_send_data(struct sfile_sender *x, uint32_t index, bool repair)
{
    uint32_t offset = index * SFILE_DATA_BYTES;
    unsigned int len = x->size - offset;
    uint8_t *data = s_swap_tx + SFILE_HEADER;
    int ret;
    if (len > SFILE_DATA_BYTES) len = SFILE_DATA_BYTES;
    ret = x->read_block(x->source, index, data, len, repair);
    if (ret) return ret;
    if (!x->source && repair && index == x->bad_block) x->repaired_bad_block = true;
    if (!repair) x->crc = sfile_crc32(x->crc, data, len);
    else ++x->resent;
    sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_DATA, x->tid, index, 0, len);
    if (!x->source && !repair && index == x->bad_block)
        s_swap_tx[SFILE_CRC_OFFSET] ^= 1u;
    return sfile_exchange(x);
}

static int sfile_send_body(struct sfile_sender *x)
{
    uint32_t blocks = (x->size + SFILE_DATA_BYTES - 1u) / SFILE_DATA_BYTES;
    uint32_t first = 0;
    while (first < blocks) {
        uint8_t bitmap[SFILE_BITMAP];
        unsigned int count = sfile_group_count(blocks, first);
        unsigned int i, round;
        int ret;
        for (i = 0; i < count; ++i) {
            ret = sfile_send_data(x, first + i, false);
            if (ret) return ret;
        }
        ret = sfile_end_pair(x);
        if (ret) return ret;
        for (round = 0; ; ++round) {
            uint32_t started = k_uptime_get_32(), waited;
            ret = sfile_control(x, SFILE_GROUP_END, first, NULL, 0);
            waited = k_uptime_get_32() - started;
            x->group_wait_ms += waited;
            if (waited > x->group_peak_ms) x->group_peak_ms = waited;
            if (ret == SFILE_OK) break;
            if (ret != SFILE_MISSING) return ret;
            if (round == 3u) return -EBADMSG;
            memcpy(bitmap, s_swap_rx + SFILE_HEADER + 2, sizeof(bitmap));
            for (i = 0; i < count; ++i) {
                if (sfile_bit(bitmap, i)) continue;
                ret = sfile_send_data(x, first + i, true);
                if (ret) return ret;
            }
            ret = sfile_end_pair(x);
            if (ret) return ret;
        }
        ++x->groups;
        first += count;
    }
    return 0;
}

static int sfile_send_http_spec(struct sfile_sender *x, uint8_t *buffer)
{
    uint32_t offset = 0;
    while (offset < x->http->spec_size) {
        unsigned int length = x->http->spec_size - offset;
        int ret;
        if (length > SFILE_DATA_BYTES) length = SFILE_DATA_BYTES;
        if (x->http->spec_data) memcpy(buffer, x->http->spec_data + offset, length);
        else if (fs_read(&x->http->spec, buffer, length) != (ssize_t)length) return -EIO;
        ret = sfile_control(x, SFILE_HTTP_META, offset, buffer, length);
        if (ret) return ret;
        offset += length;
    }
    return 0;
}

static int sfile_read_http_result(struct sfile_sender *x)
{
    uint32_t offset = 0;
    while (offset < x->http->response_size) {
        unsigned int length = x->http->response_size - offset;
        int ret;
        if (length > SFILE_HTTP_PAGE_BYTES) length = SFILE_HTTP_PAGE_BYTES;
        ret = sfile_control(x, SFILE_HTTP_READ, offset, NULL, 0);
        if (ret) return ret;
        if (swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + length) return -EBADMSG;
        if (x->http->receive) {
            ret = x->http->receive(x->http->receive_context, offset,
                                  s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, length);
            if (ret) return ret;
        } else if (x->http->console_result) {
            /* Small lossless lines, including binary/non-ASCII error responses. */
            static const char digits[] = "0123456789abcdef";
            unsigned int at;
            for (at = 0; at < length; at += 32u) {
                char hex[65];
                unsigned int j, n = length - at;
                if (n > 32u) n = 32u;
                for (j = 0; j < n; ++j) {
                    uint8_t b = s_swap_rx[SFILE_HEADER + SFILE_ACK_BYTES + at + j];
                    hex[j * 2u] = digits[b >> 4];
                    hex[j * 2u + 1u] = digits[b & 15u];
                }
                hex[n * 2u] = 0;
                shell_print(x->shell, "SPI HTTP DATA tid=%u offset=%u hex=%s",
                            x->tid, offset + at, hex);
            }
        } else if (fs_write(&x->http->result, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES,
                            length) != (ssize_t)length) return -EIO;
        offset += length;
    }
    shell_print(x->shell, "SPI HTTP tid=%u http=%u bytes=%u", x->tid,
                (unsigned int)x->http->http_code, (unsigned int)x->http->response_size);
    x->http->response_complete = true;
    return 0;
}

static int sfile_run_context(const struct shell *sh, struct sfile_sender *x, const char *name, bool pair,
                     uint32_t hz)
{
    static uint32_t next_id;
    struct spi_config config = {
        .frequency = hz,
        .operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8),
    };
    uint8_t *params;
    uint32_t start, elapsed;
    size_t name_len = name ? strlen(name) : 0;
    size_t url_len = x->url ? strlen(x->url) : 0;
    unsigned int param_len = 4u + name_len;
    uint8_t start_command = x->delivery_command ? x->delivery_command :
                            (pair ? SFILE_START_PAIR : (name ? SFILE_START : SFILE_START_RAM));
    int ret, wire_status = SFILE_OK, end_ret = 0;
    bool opened = false, finish_sent = false;
#ifdef SPI_LINK_BOOT_SYNC
    int boot = 0;
    bool previous_session = s_swap_file_used;
    if (sfile_boot_hard_fault) return -EPERM;
#endif

    if (x->url && (url_len < 10 || url_len > SFILE_HTTPS_URL_MAX)) return -EINVAL;
    x->shell = sh;
    if (name && !sfile_name_valid((const uint8_t *)name, name_len)) return -EINVAL;
    if (sfile_pair_mode && !pair) return -EPERM;
    x->paired = sfile_pair_mode;
    if (s_ready_legacy_used || (s_ready_used && !s_swap_file_used)) return -EPERM;
    x->dev = device_get_binding(CONFIG_SPI_2_NAME);
    if (!x->dev) return -ENODEV;
    x->crc = UINT32_MAX;
    if (!next_id) next_id = k_cycle_get_32();
    if (++next_id == 0) ++next_id;
    x->tid = next_id;

    params = k_heap_alloc(&sys_mem_pool, SFILE_DATA_BYTES, K_NO_WAIT);
    if (!params) {
        shell_error(sh, "SPI FILE request allocation failed");
        return -ENOMEM;
    }
    sys_wake_lock(FULL_WAKE_LOCK);
    start = k_uptime_get_32();
    if (x->url || x->http) {
        ret = sfile_entropy_fill(params + 4, SFILE_HTTPS_SEED_BYTES);
        shell_print(sh, "SPI HTTPS SEED source=SE_TRNG bytes=%u ret=%d",
                    SFILE_HTTPS_SEED_BYTES, ret);
        if (ret) goto unlock;
    }
#ifdef SPI_LINK_BOOT_SYNC
    ret = swap_ready_begin_internal(true);
    if (ret) goto unlock;
    boot = swap_ready_boot_probe();
    if (boot < 0 || (!boot && (s_ready_faulted || !sfile_pair_mode))) {
        ret = boot < 0 ? boot : -EPERM;
        shell_error(sh, "SPI BOOT SYNC unavailable ret=%d; no clocks sent", ret);
        if (!previous_session && !s_ready_faulted) {
            /* The entry guard required unused READY. This first probe only
             * sampled GPIO: no SPI open, clocks or token consumption. */
            end_ret = swap_ready_end(0);
            if (end_ret) {
                ret = end_ret;
                shell_error(sh, "SPI BOOT probe GPIO cleanup failed ret=%d", ret);
            } else {
                s_ready_used = false;
                shell_print(sh, "SPI BOOT probe released; retry when peer ready");
            }
            goto unlock;
        }
        goto end_ready;
    }
#else
    ret = swap_ready_begin();
#endif
    if (ret) goto unlock;
    s_swap_circular_used = s_swap_file_used = true;
    sfile_probe_regs(x, 0);
    ret = spi_acts_spi2_circular_open(x->dev, &config);
    sfile_probe_regs(x, 1);
    if (ret) goto end_ready;
    opened = true;
#ifdef SPI_LINK_BOOT_SYNC
    if (boot) {
        ret = sfile_boot_sync(x);
        if (ret < 0) {
            x->link_failed = true;
            shell_error(sh, "SPI BOOT SYNC FAIL ret=%d; link quarantined", ret);
            goto close_driver;
        }
        if (previous_session || ret == 1) {
            /* Peer reboot invalidated the old task. Reconcile on NEXT init. */
            ret = -EIO;
            shell_error(sh, "SPI boot session recovered; current task FAIL, retry same file ID");
            goto close_driver;
        }
    }
#endif
#ifdef P4_SPI_DIAG
    if (x->http && x->http->diag) sfile_diag_regs(x->http->diag->initial);
#endif
    memset(s_swap_tx, 0, SWAP_BLOCK_SIZE);
    sfile_put32(params, x->size);
    if (name_len) memcpy(params + 4, name, name_len);
    if (x->url || x->http) {
        if (x->http) {
            sfile_put16(params + SFILE_HTTPS_START_FIXED, x->http->spec_size);
            param_len = SFILE_HTTPS_START_FIXED + 2u;
#ifdef P4_RADIO_TEST
            if (x->http->radio_test) {
                params[param_len++] = x->http->radio_test;
            }
#endif
        } else {
            memcpy(params + SFILE_HTTPS_START_FIXED, x->url, url_len);
            param_len = SFILE_HTTPS_START_FIXED + url_len;
        }
    } else if (x->delivery_command && !x->device_info) {
        sfile_put32(params + 4, x->hold_ms);
        param_len = 8;
#ifdef P4_SPI_SOAK
        if (x->probe && x->probe->quiet && x->delivery_command == SFILE_START_DELIVERY) {
            sfile_put32(params + 8, SFILE_PROBE_COOKIE);
            param_len = 12;
        }
#endif
        if (x->delivery_command == SFILE_START_STREAM) {
            sfile_put32(params + 8, x->fail_at);
            param_len = 12;
        }
    }
    if (pair && !x->paired) ret = sfile_pair_begin(x, start_command, params, param_len);
    else ret = sfile_control(x, start_command, 0, params, param_len);
    if (x->url || x->http) {
        volatile uint8_t *seed = params + 4;
        unsigned int i;
        for (i = 0; i < SFILE_HTTPS_SEED_BYTES; ++i) seed[i] = 0;
    }
    if (!ret && x->http) ret = sfile_send_http_spec(x, params);
    if (!ret && !x->device_info) ret = sfile_send_body(x);
    if (!ret && !x->device_info && !x->source && x->bad_block != UINT32_MAX && !x->repaired_bad_block)
        ret = -EBADMSG;
    if (!ret && !x->device_info) {
        finish_sent = true;
        sfile_put32(params, x->crc ^ UINT32_MAX);
        ret = sfile_control(x, SFILE_FINISH, 0, params, 4);
        if (ret > 0) wire_status = ret;
        if (x->http && (ret == SFILE_OK || ret == SFILE_HTTP_FAILED)) {
            int result_ret = sfile_read_http_result(x);
            if (result_ret) { x->http->transport_fault = true; ret = result_ret; }
        }
    }
    if (!ret && x->device_info) ret = sfile_control(x, SFILE_ABORT, 0, NULL, 0);
    if (ret > 0) {
        wire_status = ret;
        ret = -EIO;
    }
    /* A lost FINISH reply leaves commit status unknown. ABORT only before
     * FINISH or after the peer has explicitly reported a failed FINISH. */
    if (x->http && (x->resync_needed || x->link_failed || (ret && !wire_status)))
        x->http->transport_fault = true;
    if (ret && !x->link_failed && !x->resync_needed && (!finish_sent || wire_status)) {
        if (sfile_control(x, SFILE_ABORT, 0, NULL, 0)) x->link_failed = true;
    }
    if (x->resync_needed && !x->link_failed) {
        int recovery = sfile_resync(x);
        if (recovery) {
            x->link_failed = true;
            shell_error(sh, "SPI RESYNC FAIL ret=%d; link quarantined", recovery);
        }
    }
    if (!x->link_failed) {
        end_ret = sfile_wait_ready(x);
        if (end_ret) x->link_failed = true;
        if (!ret) ret = end_ret;
    }
#ifdef SPI_LINK_BOOT_SYNC
close_driver:
#endif
    sfile_probe_regs(x, 2);
    end_ret = spi_acts_spi2_circular_close(x->dev);
    sfile_probe_regs(x, 3);
#ifdef SPI_LINK_BOOT_SYNC
    if (end_ret) sfile_boot_hard_fault = true;
#endif
    if (end_ret) x->link_failed = true;
    if (!ret) ret = end_ret;
end_ready:
    end_ret = swap_ready_end(!opened || x->link_failed ? (ret ? ret : -EIO) : 0);
    if (!ret) ret = end_ret;
unlock:
    {
        volatile uint8_t *p = params;
        unsigned int i;
        for (i = 0; i < SFILE_DATA_BYTES; ++i) p[i] = 0;
        k_heap_free(&sys_mem_pool, params);
    }
    sys_wake_unlock(FULL_WAKE_LOCK);
    if (x->http) {
        x->http->link_failed = x->link_failed;
        x->http->wire_status = wire_status;
        if (x->link_failed || x->resync_needed || end_ret) x->http->transport_fault = true;
    }
    elapsed = k_uptime_get_32() - start;
    shell_print(sh, "SPI FILE tid=%u bytes=%u transfers=%u resent=%u ms=%u Bps=%u ready_us=%llu status=%u ret=%d",
                x->tid, x->size, x->transfers, x->resent, elapsed,
                !ret && elapsed ? (uint32_t)((uint64_t)x->size * 1000u / elapsed) : 0u,
                (unsigned long long)x->ready_us, wire_status, ret);
    if (!x->source) {
        shell_print(sh, "SPI FILE TEST groups=%u crc32=%08x %s",
                    x->groups, x->crc ^ UINT32_MAX, ret ? "FAIL" : "PASS");
        if (x->bad_block != UINT32_MAX)
            shell_print(sh, "SPI FILE TEST bad_block=%u repaired=%u",
                        x->bad_block, (unsigned int)x->repaired_bad_block);
    }
    shell_print(sh, "SPI FILE READY max_us=%u after_type=%u after_index=%u timeout_ms=%u",
                x->peak_ready_us, x->peak_type, x->peak_index,
                x->peak_timeout_ms ? x->peak_timeout_ms : SFILE_READY_TIMEOUT_MS);
    if (x->url || x->http)
        shell_print(sh, "SPI HTTP FLOW tid=%u group_busy=%u group_wait_ms=%u group_peak_ms=%u",
                    x->tid, x->group_busy, x->group_wait_ms, x->group_peak_ms);
    if (!name)
        shell_print(sh, "SPI RAM READY data_us=%llu control_us=%llu",
                    (unsigned long long)x->data_ready_us,
                    (unsigned long long)(x->ready_us - x->data_ready_us));
    if (pair) shell_print(sh, "SPI PAIR HOST pairs=%u blocks=%u", x->pairs, x->transfers);
    if (s_ready_faulted && ret == -ETIMEDOUT)
        shell_warn(sh, "SPI FILE READY timeout after_type=%u after_index=%u",
                   x->last_type, x->last_index);
    if (finish_sent && ret && !wire_status) {
        if (name) shell_warn(sh, "FINISH outcome unknown. Check spi_rx_%s on UMA503.", name);
        else shell_warn(sh, "%s FINISH result was not confirmed.", (x->url || x->http) ? "HTTPS" : "RAM");
    }
    return ret;
}

/* Existing shell diagnostics keep their by-value entry. P4 owns a heap-backed
 * sender and calls the pointer entry, avoiding two sender copies on shell_uart. */
static int sfile_run(const struct shell *sh, struct sfile_sender x, const char *name,
                     bool pair, uint32_t hz)
{
    return sfile_run_context(sh, &x, name, pair, hz);
}

static int sfile_send_path(const struct shell *sh, const char *path, const char *name,
                           const char *url, uint32_t offset, uint32_t length,
                           struct sfile_http_files *http)
{
    struct sfile_file_source source = { .offset = offset, .position = offset };
    struct sfile_sender x = { .read_block = sfile_read_file, .source = &source, .url = url,
                              .http = http, .delivery_command = http ? SFILE_START_HTTP :
                                                               (url ? SFILE_START_HTTPS : 0) };
    off_t size;
    int ret;

    fs_file_t_init(&source.file);
    ret = fs_open(&source.file, path, FS_O_READ);
    if (ret) return ret;
    ret = fs_seek(&source.file, 0, FS_SEEK_END);
    if (!ret) {
        size = fs_tell(&source.file);
        if (size < 0 || (uint64_t)size > SFILE_MAX_SIZE) ret = -EFBIG;
        else if (offset > (uint32_t)size || length > (uint32_t)size - offset)
            ret = -EINVAL;
        else {
            x.size = (url || http) ? length : (uint32_t)size;
            ret = fs_seek(&source.file, offset, FS_SEEK_SET);
            if (!ret) ret = sfile_run(sh, x, name, url || http, (url || http) ? SFILE_HTTPS_HZ : SFILE_DEFAULT_HZ);
        }
    }
    fs_close(&source.file);
    return ret;
}

static int shell_spi2file(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc != 3) return -EINVAL;
    return sfile_send_path(sh, argv[1], argv[2], NULL, 0, 0, NULL);
}

static int shell_spi2putfile(const struct shell *sh, size_t argc, char *argv[])
{
    unsigned long offset, length;
    char *end;

    if (argc != 5 || argv[2][0] < '0' || argv[2][0] > '9' ||
        argv[3][0] < '0' || argv[3][0] > '9') return -EINVAL;
    offset = strtoul(argv[2], &end, 10);
    if (*end || offset > SFILE_MAX_SIZE) return -EINVAL;
    length = strtoul(argv[3], &end, 10);
    if (*end || !length || length > SFILE_MAX_SIZE) return -EINVAL;
    return sfile_send_path(sh, argv[1], NULL, argv[4], (uint32_t)offset, (uint32_t)length, NULL);
}

/* The spec file carries POST/PUT, the URL and raw headers. The shell
 * receives only local paths so signed URLs and headers stay out of the console. */
static int shell_spi2http(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_http_files http = {0};
    struct fs_dirent existing;
    unsigned long offset, length;
    off_t spec_size;
    char *end;
    int ret, close_ret;

    if (argc != 6 || argv[3][0] < '0' || argv[3][0] > '9' ||
        argv[4][0] < '0' || argv[4][0] > '9') return -EINVAL;
    offset = strtoul(argv[3], &end, 10);
    if (*end || offset > SFILE_MAX_SIZE) return -EINVAL;
    length = strtoul(argv[4], &end, 10);
    if (*end || !length || length > SFILE_MAX_SIZE) return -EINVAL;
    ret = fs_stat(argv[5], &existing);
    if (ret != -ENOENT) return ret ? ret : -EEXIST;
    fs_file_t_init(&http.spec);
    fs_file_t_init(&http.result);
    ret = fs_open(&http.spec, argv[1], FS_O_READ);
    if (ret) return ret;
    ret = fs_seek(&http.spec, 0, FS_SEEK_END);
    if (ret) goto close_spec;
    spec_size = fs_tell(&http.spec);
    if (spec_size <= 0 || spec_size > (off_t)SFILE_HTTP_META_MAX) {
        ret = -EINVAL;
        goto close_spec;
    }
    http.spec_size = (uint16_t)spec_size;
    ret = fs_seek(&http.spec, 0, FS_SEEK_SET);
    if (ret) goto close_spec;
    ret = fs_open(&http.result, argv[5], FS_O_CREATE | FS_O_WRITE);
    if (ret) goto close_spec;
    ret = sfile_send_path(sh, argv[2], NULL, NULL, (uint32_t)offset, (uint32_t)length, &http);
    close_ret = fs_close(&http.result);
    if (!ret) ret = close_ret;
close_spec:
    fs_close(&http.spec);
    return ret;
}

static int sfile_run_generated(const struct shell *sh, const char *blocks_arg,
                               const char *bad_arg, const char *name, bool pair, uint32_t hz)
{
    struct sfile_sender x = { .read_block = sfile_read_generated, .bad_block = UINT32_MAX };
    unsigned long blocks, bad;
    char *end;
    if (blocks_arg[0] < '0' || blocks_arg[0] > '9') return -EINVAL;
    blocks = strtoul(blocks_arg, &end, 10);
    if (*end || !blocks || blocks > 10000u) return -EINVAL;
    if (bad_arg) {
        if (bad_arg[0] < '0' || bad_arg[0] > '9') return -EINVAL;
        bad = strtoul(bad_arg, &end, 10);
        if (*end || bad >= blocks) return -EINVAL;
        x.bad_block = (uint32_t)bad;
    }
    x.size = (uint32_t)blocks * SFILE_DATA_BYTES;
    return sfile_run(sh, x, name, pair, hz);
}

static int shell_spi2filetest(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc != 3 && argc != 4) return -EINVAL;
    return sfile_run_generated(sh, argv[1], argc == 4 ? argv[3] : NULL, argv[2], false,
                               SFILE_DEFAULT_HZ);
}

static int shell_spi2ram(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc != 2 && argc != 3) return -EINVAL;
    return sfile_run_generated(sh, argv[1], argc == 3 ? argv[2] : NULL, NULL, false,
                               SFILE_DEFAULT_HZ);
}

static int shell_spi2rampair(const struct shell *sh, size_t argc, char *argv[])
{
    unsigned long hz = SFILE_DEFAULT_HZ;
    if (argc >= 4 && strcmp(argv[argc - 2], "--hz") == 0) {
        const char *arg = argv[argc - 1];
        char *end;
        if (arg[0] < '0' || arg[0] > '9') return -EINVAL;
        hz = strtoul(arg, &end, 10);
        if (*end || hz < 100000u || hz > 8000000u) return -EINVAL;
        argc -= 2;
    }
    if (argc != 2 && argc != 3) return -EINVAL;
    return sfile_run_generated(sh, argv[1], argc == 3 ? argv[2] : NULL, NULL, true,
                               (uint32_t)hz);
}

static int sfile_run_delivery(const struct shell *sh, size_t argc, char *argv[], uint8_t command)
{
    struct sfile_sender x = {
        .read_block = sfile_read_generated, .bad_block = UINT32_MAX,
        .fail_at = UINT32_MAX, .delivery_command = command,
    };
    unsigned long size, hold_ms, inject;
    char *end;

    if (argc != 3 && argc != 4) return -EINVAL;
    if (argv[1][0] < '0' || argv[1][0] > '9' || argv[2][0] < '0' || argv[2][0] > '9')
        return -EINVAL;
    size = strtoul(argv[1], &end, 10);
    if (*end || !size || size > 10000u * SFILE_DATA_BYTES) return -EINVAL;
    hold_ms = strtoul(argv[2], &end, 10);
    if (*end || hold_ms > SFILE_DELIVERY_MAX_HOLD_MS) return -EINVAL;
    if (argc == 4) {
        if (argv[3][0] < '0' || argv[3][0] > '9') return -EINVAL;
        inject = strtoul(argv[3], &end, 10);
        if (*end) return -EINVAL;
        if (command == SFILE_START_STREAM) {
            if (inject >= size) return -EINVAL;
            x.fail_at = (uint32_t)inject;
        } else {
            if (inject >= (size + SFILE_DATA_BYTES - 1u) / SFILE_DATA_BYTES) return -EINVAL;
            x.bad_block = (uint32_t)inject;
        }
    }
    x.size = (uint32_t)size;
    x.hold_ms = (uint32_t)hold_ms;
    return sfile_run(sh, x, NULL, true, 8000000u);
}

static int shell_spi2deliver(const struct shell *sh, size_t argc, char *argv[])
{
    return sfile_run_delivery(sh, argc, argv, SFILE_START_DELIVERY);
}

static int shell_spi2stream(const struct shell *sh, size_t argc, char *argv[])
{
    return sfile_run_delivery(sh, argc, argv, SFILE_START_STREAM);
}

static int shell_spi2put(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_sender x = {
        .read_block = sfile_read_generated, .bad_block = UINT32_MAX,
        .delivery_command = SFILE_START_HTTPS,
    };
    unsigned long size;
    char *end;

    if (argc != 3 || argv[1][0] < '0' || argv[1][0] > '9') return -EINVAL;
    size = strtoul(argv[1], &end, 10);
    if (*end || !size || size > 10000u * SFILE_DATA_BYTES) return -EINVAL;
    x.size = (uint32_t)size;
    x.url = argv[2];
    return sfile_run(sh, x, NULL, true, SFILE_HTTPS_HZ);
}

#include "spi_link_put_probe.h"
#ifdef P4_SPI_SOAK
#include "spi_link_soak.h"
#endif

#endif
