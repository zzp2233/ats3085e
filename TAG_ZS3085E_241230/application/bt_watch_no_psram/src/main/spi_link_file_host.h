#ifndef ATS_SPI_LINK_FILE_H
#define ATS_SPI_LINK_FILE_H

#include <fs/fs.h>
#include "spi_link_file_protocol.h"

#define SFILE_READY_TIMEOUT_MS 2000u
#define SFILE_DEFAULT_HZ 4000000u

struct sfile_sender {
    const struct device *dev;
    int (*read_block)(void *context, uint32_t index, uint8_t *data,
                      unsigned int len, bool repair);
    void *source;
    uint32_t tid, size, crc, transfers, resent, groups, bad_block;
    uint32_t last_index, peak_index, peak_ready_us;
    uint64_t ready_us, data_ready_us;
    uint16_t request;
    uint8_t last_type, peak_type;
    unsigned int pairs;
    uint32_t hold_ms;
    bool delivery;
    bool link_failed, repaired_bad_block, paired, half_pair;
};

static bool sfile_pair_mode;

static int sfile_wait_ready(struct sfile_sender *x)
{
    uint32_t waited;
    int ret = swap_ready_wait_timeout(&waited, SFILE_READY_TIMEOUT_MS);
    x->ready_us += waited;
    if (x->last_type == SFILE_DATA) x->data_ready_us += waited;
    if (waited > x->peak_ready_us) {
        x->peak_ready_us = waited;
        x->peak_type = x->last_type;
        x->peak_index = x->last_index;
    }
    return ret;
}

static int sfile_exchange(struct sfile_sender *x)
{
    int ret = x->half_pair ? 0 : sfile_wait_ready(x);
    if (!ret) {
        if (!x->half_pair) swap_ready_consume();
        if (s_swap_tx[2] != SFILE_IDLE) {
            x->last_type = s_swap_tx[2];
            x->last_index = sfile_u32(s_swap_tx + 8);
        }
        if (x->paired)
            sfile_put32(s_swap_tx + SFILE_PAIR_WITNESS_OFFSET, SFILE_PAIR_WITNESS);
        ++x->transfers;
        ret = spi_acts_spi2_circular_transfer(x->dev, s_swap_tx, s_swap_rx);
        if (!ret && x->paired) {
            x->half_pair = !x->half_pair;
            if (!x->half_pair) ++x->pairs;
        }
    }
    if (ret) x->link_failed = true;
    return ret;
}

static bool sfile_ack_matches(const struct sfile_sender *x, uint8_t command,
                              uint32_t index, uint16_t request)
{
    return s_swap_rx[2] == SFILE_ACK && sfile_u32(s_swap_rx + 4) == x->tid &&
        sfile_u32(s_swap_rx + 8) == index && swap_u16(s_swap_rx + 12) == request &&
        swap_u16(s_swap_rx + 14) == SFILE_ACK_BYTES &&
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
    if (!ret) sfile_pair_mode = x->paired = true;
    return ret;
}

static int sfile_control(struct sfile_sender *x, uint8_t command, uint32_t index,
                         const uint8_t *body, unsigned int len)
{
    uint32_t start = k_uptime_get_32();
    bool delivery_group = x->delivery && command == SFILE_GROUP_END;
    uint32_t timeout = command == SFILE_FINISH ? 120000u :
                       (delivery_group ? 20000u : 2000u);
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
        ret = sfile_exchange(x);
        if (ret) return ret;
        if (!x->half_pair && sfile_ack_matches(x, command, index, request)) {
            ret = s_swap_rx[SFILE_HEADER + 1];
            if (ret != SFILE_BUSY || (command != SFILE_FINISH && !delivery_group))
                return ret;
            verifying = true;
        }
        if (delivery_group && verifying && !x->half_pair) k_msleep(20);
    } while (x->half_pair || (uint32_t)(k_uptime_get_32() - start) < timeout);
    return -ETIMEDOUT;
}

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

static int sfile_read_file(void *context, uint32_t index, uint8_t *data,
                           unsigned int len, bool repair)
{
    struct fs_file_t *file = context;
    if (repair && fs_seek(file, index * SFILE_DATA_BYTES, FS_SEEK_SET)) return -EIO;
    return fs_read(file, data, len) == (ssize_t)len ? 0 : -EIO;
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
            ret = sfile_control(x, SFILE_GROUP_END, first, NULL, 0);
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
        if (x->source && first < blocks && x->resent &&
            fs_seek(x->source, first * SFILE_DATA_BYTES, FS_SEEK_SET)) return -EIO;
    }
    return 0;
}

static int sfile_run(const struct shell *sh, struct sfile_sender x, const char *name, bool pair,
                     uint32_t hz)
{
    static uint32_t next_id;
    struct spi_config config = {
        .frequency = hz,
        .operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8),
    };
    uint8_t params[4 + SFILE_NAME_MAX];
    uint32_t start, elapsed;
    size_t name_len = name ? strlen(name) : 0;
    unsigned int param_len = 4u + name_len;
    uint8_t start_command = x.delivery ? SFILE_START_DELIVERY :
                            (pair ? SFILE_START_PAIR : (name ? SFILE_START : SFILE_START_RAM));
    int ret, wire_status = SFILE_OK, end_ret;
    bool opened = false, finish_sent = false;

    if (name && !sfile_name_valid((const uint8_t *)name, name_len)) return -EINVAL;
    if (sfile_pair_mode && !pair) return -EPERM;
    x.paired = sfile_pair_mode;
    if (s_ready_legacy_used || (s_ready_used && !s_swap_file_used)) return -EPERM;
    x.dev = device_get_binding(CONFIG_SPI_2_NAME);
    if (!x.dev) return -ENODEV;
    x.crc = UINT32_MAX;
    if (!next_id) next_id = k_cycle_get_32();
    if (++next_id == 0) ++next_id;
    x.tid = next_id;

    sys_wake_lock(FULL_WAKE_LOCK);
    start = k_uptime_get_32();
    ret = swap_ready_begin();
    if (ret) goto unlock;
    s_swap_circular_used = s_swap_file_used = true;
    ret = spi_acts_spi2_circular_open(x.dev, &config);
    if (ret) goto end_ready;
    opened = true;
    memset(s_swap_tx, 0, SWAP_BLOCK_SIZE);
    sfile_put32(params, x.size);
    if (name_len) memcpy(params + 4, name, name_len);
    if (x.delivery) {
        sfile_put32(params + 4, x.hold_ms);
        param_len = 8;
    }
    if (pair && !x.paired) ret = sfile_pair_begin(&x, start_command, params, param_len);
    else ret = sfile_control(&x, start_command, 0, params, param_len);
    if (!ret) ret = sfile_send_body(&x);
    if (!ret && !x.source && x.bad_block != UINT32_MAX && !x.repaired_bad_block)
        ret = -EBADMSG;
    if (!ret) {
        finish_sent = true;
        sfile_put32(params, x.crc ^ UINT32_MAX);
        ret = sfile_control(&x, SFILE_FINISH, 0, params, 4);
    }
    if (ret > 0) {
        wire_status = ret;
        ret = -EIO;
    }
    /* A lost FINISH reply leaves commit status unknown. ABORT only before
     * FINISH or after the peer has explicitly reported a failed FINISH. */
    if (ret && !x.link_failed && (!finish_sent || wire_status))
        (void)sfile_control(&x, SFILE_ABORT, 0, NULL, 0);
    if (!x.link_failed) {
        end_ret = sfile_wait_ready(&x);
        if (end_ret) x.link_failed = true;
        if (!ret) ret = end_ret;
    }
    end_ret = spi_acts_spi2_circular_close(x.dev);
    if (end_ret) x.link_failed = true;
    if (!ret) ret = end_ret;
end_ready:
    end_ret = swap_ready_end(!opened || x.link_failed ? (ret ? ret : -EIO) : 0);
    if (!ret) ret = end_ret;
unlock:
    sys_wake_unlock(FULL_WAKE_LOCK);
    elapsed = k_uptime_get_32() - start;
    shell_print(sh, "SPI FILE tid=%u bytes=%u transfers=%u resent=%u ms=%u Bps=%u ready_us=%llu status=%u ret=%d",
                x.tid, x.size, x.transfers, x.resent, elapsed,
                !ret && elapsed ? (uint32_t)((uint64_t)x.size * 1000u / elapsed) : 0u,
                (unsigned long long)x.ready_us, wire_status, ret);
    if (!x.source) {
        shell_print(sh, "SPI FILE TEST groups=%u crc32=%08x %s",
                    x.groups, x.crc ^ UINT32_MAX, ret ? "FAIL" : "PASS");
        if (x.bad_block != UINT32_MAX)
            shell_print(sh, "SPI FILE TEST bad_block=%u repaired=%u",
                        x.bad_block, (unsigned int)x.repaired_bad_block);
    }
    shell_print(sh, "SPI FILE READY max_us=%u after_type=%u after_index=%u timeout_ms=%u",
                x.peak_ready_us, x.peak_type, x.peak_index, SFILE_READY_TIMEOUT_MS);
    if (!name)
        shell_print(sh, "SPI RAM READY data_us=%llu control_us=%llu",
                    (unsigned long long)x.data_ready_us,
                    (unsigned long long)(x.ready_us - x.data_ready_us));
    if (pair) shell_print(sh, "SPI PAIR HOST pairs=%u blocks=%u", x.pairs, x.transfers);
    if (s_ready_faulted && ret == -ETIMEDOUT)
        shell_warn(sh, "SPI FILE READY timeout after_type=%u after_index=%u",
                   x.last_type, x.last_index);
    if (finish_sent && ret && !wire_status) {
        if (name) shell_warn(sh, "FINISH outcome unknown. Check spi_rx_%s on UMA503.", name);
        else shell_warn(sh, "RAM FINISH result was not received.");
    }
    return ret;
}

static int shell_spi2file(const struct shell *sh, size_t argc, char *argv[])
{
    struct fs_file_t source;
    struct sfile_sender x = { .read_block = sfile_read_file, .source = &source };
    off_t size;
    int ret;
    if (argc != 3) return -EINVAL;
    fs_file_t_init(&source);
    ret = fs_open(&source, argv[1], FS_O_READ);
    if (ret) return ret;
    ret = fs_seek(&source, 0, FS_SEEK_END);
    if (!ret) {
        size = fs_tell(&source);
        if (size < 0 || (uint64_t)size > SFILE_MAX_SIZE) ret = -EFBIG;
        else {
            x.size = (uint32_t)size;
            ret = fs_seek(&source, 0, FS_SEEK_SET);
            if (!ret) ret = sfile_run(sh, x, argv[2], false, SFILE_DEFAULT_HZ);
        }
    }
    fs_close(&source);
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

static int shell_spi2deliver(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_sender x = {
        .read_block = sfile_read_generated, .bad_block = UINT32_MAX, .delivery = true,
    };
    unsigned long size, hold_ms, bad;
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
        bad = strtoul(argv[3], &end, 10);
        if (*end || bad >= (size + SFILE_DATA_BYTES - 1u) / SFILE_DATA_BYTES)
            return -EINVAL;
        x.bad_block = (uint32_t)bad;
    }
    x.size = (uint32_t)size;
    x.hold_ms = (uint32_t)hold_ms;
    return sfile_run(sh, x, NULL, true, 8000000u);
}

#endif
