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

struct sfile_http_files {
    struct fs_file_t spec, result;
    const uint8_t *spec_data; /* RAM request; NULL retains file-backed P4 path. */
    uint16_t spec_size, http_code, response_size;
    bool console_result;
};

struct sfile_sender {
    struct sfile_http_files *http;
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
    uint8_t delivery_command;
    bool link_failed, repaired_bad_block, paired, half_pair;
};

static bool sfile_pair_mode;

static int sfile_wait_ready(struct sfile_sender *x)
{
    uint32_t waited;
    uint32_t timeout = x->ready_timeout_ms ? x->ready_timeout_ms : SFILE_READY_TIMEOUT_MS;
    int ret = swap_ready_wait_timeout(&waited, timeout);
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
        if (!x->half_pair) swap_ready_consume();
        if (s_swap_tx[2] != SFILE_IDLE) {
            x->last_type = s_swap_tx[2];
            x->last_index = sfile_u32(s_swap_tx + 8);
        }
        if (x->paired)
            sfile_put32(s_swap_tx + SFILE_PAIR_WITNESS_OFFSET, SFILE_PAIR_WITNESS);
        ++x->transfers;
        ret = spi_acts_spi2_circular_transfer(x->dev, s_swap_tx, s_swap_rx);
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
    if (ret) x->link_failed = true;
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
    return s_swap_rx[2] == SFILE_ACK && sfile_u32(s_swap_rx + 4) == x->tid &&
        sfile_u32(s_swap_rx + 8) == index && swap_u16(s_swap_rx + 12) == request &&
        (len == SFILE_ACK_BYTES || http_result || http_page) &&
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
        if ((x->url || x->http) && (delivery_group || command == SFILE_FINISH) && i > 1u) {
            uint32_t elapsed = (uint32_t)(k_uptime_get_32() - start);
            x->ready_timeout_ms = elapsed < timeout ? timeout - elapsed : 1u;
        }
        ret = sfile_exchange(x);
        x->ready_timeout_ms = 0;
        if (ret) return ret;
        if (!x->half_pair && sfile_ack_matches(x, command, index, request)) {
            ret = s_swap_rx[SFILE_HEADER + 1];
            if (ret != SFILE_BUSY || (command != SFILE_FINISH && !delivery_group)) {
                unsigned int len = swap_u16(s_swap_rx + 14);
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
        if (x->http->console_result) {
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
    uint8_t *params;
    uint32_t start, elapsed;
    size_t name_len = name ? strlen(name) : 0;
    size_t url_len = x.url ? strlen(x.url) : 0;
    unsigned int param_len = 4u + name_len;
    uint8_t start_command = x.delivery_command ? x.delivery_command :
                            (pair ? SFILE_START_PAIR : (name ? SFILE_START : SFILE_START_RAM));
    int ret, wire_status = SFILE_OK, end_ret;
    bool opened = false, finish_sent = false;

    if (x.url && (url_len < 10 || url_len > SFILE_HTTPS_URL_MAX)) return -EINVAL;
    x.shell = sh;
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

    params = k_heap_alloc(&sys_mem_pool, SFILE_DATA_BYTES, K_NO_WAIT);
    if (!params) {
        shell_error(sh, "SPI FILE request allocation failed");
        return -ENOMEM;
    }
    sys_wake_lock(FULL_WAKE_LOCK);
    start = k_uptime_get_32();
    if (x.url || x.http) {
        ret = sfile_entropy_fill(params + 4, SFILE_HTTPS_SEED_BYTES);
        shell_print(sh, "SPI HTTPS SEED source=SE_TRNG bytes=%u ret=%d",
                    SFILE_HTTPS_SEED_BYTES, ret);
        if (ret) goto unlock;
    }
    ret = swap_ready_begin();
    if (ret) goto unlock;
    s_swap_circular_used = s_swap_file_used = true;
    ret = spi_acts_spi2_circular_open(x.dev, &config);
    if (ret) goto end_ready;
    opened = true;
    memset(s_swap_tx, 0, SWAP_BLOCK_SIZE);
    sfile_put32(params, x.size);
    if (name_len) memcpy(params + 4, name, name_len);
    if (x.url || x.http) {
        if (x.http) {
            sfile_put16(params + SFILE_HTTPS_START_FIXED, x.http->spec_size);
            param_len = SFILE_HTTPS_START_FIXED + 2u;
        } else {
            memcpy(params + SFILE_HTTPS_START_FIXED, x.url, url_len);
            param_len = SFILE_HTTPS_START_FIXED + url_len;
        }
    } else if (x.delivery_command) {
        sfile_put32(params + 4, x.hold_ms);
        param_len = 8;
        if (x.delivery_command == SFILE_START_STREAM) {
            sfile_put32(params + 8, x.fail_at);
            param_len = 12;
        }
    }
    if (pair && !x.paired) ret = sfile_pair_begin(&x, start_command, params, param_len);
    else ret = sfile_control(&x, start_command, 0, params, param_len);
    if (x.url || x.http) {
        volatile uint8_t *seed = params + 4;
        unsigned int i;
        for (i = 0; i < SFILE_HTTPS_SEED_BYTES; ++i) seed[i] = 0;
    }
    if (!ret && x.http) ret = sfile_send_http_spec(&x, params);
    if (!ret) ret = sfile_send_body(&x);
    if (!ret && !x.source && x.bad_block != UINT32_MAX && !x.repaired_bad_block)
        ret = -EBADMSG;
    if (!ret) {
        finish_sent = true;
        sfile_put32(params, x.crc ^ UINT32_MAX);
        ret = sfile_control(&x, SFILE_FINISH, 0, params, 4);
        if (x.http && (ret == SFILE_OK || ret == SFILE_HTTP_FAILED)) {
            int result_ret = sfile_read_http_result(&x);
            if (result_ret) ret = result_ret;
        }
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
    {
        volatile uint8_t *p = params;
        unsigned int i;
        for (i = 0; i < SFILE_DATA_BYTES; ++i) p[i] = 0;
        k_heap_free(&sys_mem_pool, params);
    }
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
                x.peak_ready_us, x.peak_type, x.peak_index,
                x.peak_timeout_ms ? x.peak_timeout_ms : SFILE_READY_TIMEOUT_MS);
    if (x.url || x.http)
        shell_print(sh, "SPI HTTP FLOW tid=%u group_busy=%u group_wait_ms=%u group_peak_ms=%u",
                    x.tid, x.group_busy, x.group_wait_ms, x.group_peak_ms);
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
        else shell_warn(sh, "%s FINISH result was not confirmed.", (x.url || x.http) ? "HTTPS" : "RAM");
    }
    return ret;
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

#endif
