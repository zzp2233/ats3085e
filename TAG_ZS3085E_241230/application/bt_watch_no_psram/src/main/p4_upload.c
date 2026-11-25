#include "p4_upload.h"
#include <stdio.h>
#include <string.h>

static void tohex(const uint8_t *data, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[n * 2] = 0;
}
static int field_string(struct p4_json o, const char *key, char *out, size_t cap)
{
    struct p4_json v;
    return p4_json_get(o, key, &v) == 1 && p4_json_string(v, out, cap) > 0 ? 0 : -1;
}
static int field_u32(struct p4_json o, const char *key, uint32_t *out)
{
    struct p4_json v;
    return p4_json_get(o, key, &v) == 1 ? p4_json_u32(v, out) : -1;
}
static int safe_text(const char *s)
{
    if (!s || !*s) return 0;
    for (; *s; ++s) if ((unsigned char)*s < 32 || (unsigned char)*s > 126 ||
                        *s == '"' || *s == '\\') return 0;
    return 1;
}
/* Keep signing temporaries out of the deeper SPI/driver/logging call chain. */
static __attribute__((noinline)) int prepare_post(struct p4_work *w, const uint8_t *key, size_t key_size,
                       const struct p4_io *io, const char *path)
{
    const char *parts[6];
    int n, ret;
    size_t i;
    /* Read current module time for every signed POST, not PC time or boot time. */
    w->received = w->http = 0;
    ret = io->device(io->context, w->sign.imei, &w->utc);
    if (ret) return ret == P4_ERR_NETWORK ? ret : P4_ERR_LINK;
    if (w->utc < 1735689600u ||
        strlen(w->sign.imei) != 15 || io->random(io->context, w->sign.random, sizeof(w->sign.random))) return -1;
    for (i = 0; i < 15; ++i) if (w->sign.imei[i] < '0' || w->sign.imei[i] > '9') return -1;
    if (w->imei[0] && strcmp(w->imei, w->sign.imei)) return -1;
    memcpy(w->imei, w->sign.imei, sizeof(w->imei));
    tohex(w->sign.random, sizeof(w->sign.random), w->sign.nonce);
    /* The board's newlib-nano printf does not support %llu. UTC has whole
     * second precision, so append the millisecond zeros without 64-bit printf. */
    n = snprintf(w->sign.timestamp, sizeof(w->sign.timestamp), "%u000", (unsigned int)w->utc);
    if (n <= 0 || (size_t)n >= sizeof(w->sign.timestamp)) return -1;
    if (!tc_sha256_init(&w->crypto.sha) ||
        !tc_sha256_update(&w->crypto.sha, (const uint8_t *)w->body, strlen(w->body)) ||
        !tc_sha256_final(w->sign.digest, &w->crypto.sha)) return -1;
    tohex(w->sign.digest, 32, w->sign.hash);
    parts[0] = "POST"; parts[1] = path; parts[2] = w->imei;
    parts[3] = w->sign.timestamp; parts[4] = w->sign.nonce; parts[5] = w->sign.hash;
    if (!tc_hmac_set_key(&w->crypto.hmac, key, key_size) || !tc_hmac_init(&w->crypto.hmac))
        return -1;
    for (i = 0; i < 6; ++i) {
        if (i && !tc_hmac_update(&w->crypto.hmac, (const uint8_t *)"\n", 1)) return -1;
        if (!tc_hmac_update(&w->crypto.hmac, (const uint8_t *)parts[i], strlen(parts[i]))) return -1;
    }
    if (!tc_hmac_final(w->sign.digest, sizeof(w->sign.digest), &w->crypto.hmac)) return -1;
    tohex(w->sign.digest, 32, w->sign.signature);
    n = snprintf(w->spec, sizeof(w->spec),
        "POST\n%s%s\nContent-Type: application/json\r\n"
        "X-Device-Imei: %s\r\nX-Device-Timestamp: %s\r\n"
        "X-Device-Nonce: %s\r\nX-Device-Signature: %s\r\n",
        P4_ORIGIN, path, w->imei, w->sign.timestamp, w->sign.nonce, w->sign.signature);
    memset(&w->crypto, 0, sizeof(w->crypto));
    memset(&w->sign, 0, sizeof(w->sign));
    if (n <= 0 || (size_t)n >= sizeof(w->spec)) return -1;
    return 0;
}
static bool network_expired(const struct p4_work *w, const struct p4_io *io)
{
    return w->network_waiting &&
        (uint32_t)(io->now_ms(io->context) - w->network_started) >= P4_NETWORK_WINDOW_MS;
}
static int signed_post(struct p4_work *w, const uint8_t *key, size_t key_size,
                       const struct p4_io *io, const char *path)
{
    int ret = prepare_post(w, key, key_size, io, path);
    if (ret) return ret;
    w->received = w->http = 0;
    if (w->requests >= P4_REQUEST_LIMIT || network_expired(w, io)) return P4_ERR_LIMIT;
    ++w->requests;
    return io->request(io->context, w->spec, (const uint8_t *)w->body, NULL, strlen(w->body),
                       w->response, P4_RESPONSE_MAX, &w->received, &w->http);
}
static int init_body(struct p4_work *w, const struct p4_record *r)
{
    int n = snprintf(w->body, sizeof(w->body),
        "{\"localFileId\":\"%s\",\"fileMd5\":\"%s\",\"fileSize\":%u,"
        "\"durationMs\":%u,\"audioFormat\":\"m4a\",\"contentType\":\"audio/mp4\","
        "\"originalFileName\":\"%s\",\"recordedAt\":\"%s\","
        "\"partSize\":%u,\"uploadSignVersion\":\"V2\"}",
        r->local_id, r->sample_md5, (unsigned int)r->size,
        (unsigned int)r->duration_ms, r->filename, r->recorded_at, (unsigned int)w->part_size);
    return n <= 0 || (size_t)n >= sizeof(w->body) ? -1 : 0;
}
static int result(struct p4_work *w, struct p4_json *out, bool *done)
{
    struct p4_json root;
    char status[24];
    if (w->http < 200 || w->http >= 300 || w->received > P4_RESPONSE_MAX ||
        p4_json_document(w->response, w->received, &root) ||
        field_string(root, "status", status, sizeof(status)) || strcmp(status, "00000") ||
        p4_json_get(root, "result", out) != 1 ||
        field_string(*out, "syncStatus", status, sizeof(status))) return -1;
    *done = !strcmp(status, "SYNC_SUCCESS");
    return *done || !strcmp(status, "SYNCING") ? 0 : -1;
}
static int identity(struct p4_work *w, struct p4_json r, bool completed)
{
    char value[192];
    struct p4_json upload;
    int present;
    if (field_string(r, "syncSessionId", value, sizeof(value)) || !safe_text(value)) return -1;
    if (w->session[0] && strcmp(w->session, value)) return -1;
    strcpy(w->session, value);
    present = p4_json_get(r, "multipartUploadId", &upload);
    /* The live complete response contains syncSessionId but omits upload ID. */
    if (!present && completed) return 0;
    if (present != 1 || p4_json_string(upload, value, sizeof(value)) <= 0 || !safe_text(value)) return -1;
    if (w->upload[0] && strcmp(w->upload, value)) return -1;
    strcpy(w->upload, value);
    return 0;
}
static int progress(struct p4_json r, uint32_t size, bool complete)
{
    uint32_t uploaded, contiguous;
    if (field_u32(r, "uploadedBytes", &uploaded) ||
        field_u32(r, "contiguousUploadedBytes", &contiguous) ||
        uploaded > size || contiguous > uploaded) return -1;
    return complete && (uploaded != size || contiguous != size) ? -1 : 0;
}
static int part_read(void *context, uint32_t offset, uint8_t *data, unsigned int size)
{
    struct p4_work *w = context;
    if (offset > w->part.size || size > w->part.size - offset) return P4_ERR_SOURCE;
    return w->original->read(w->original->source, w->part_offset + offset, data, size);
}
/* Validation scratch must unwind before entering the transport. */
static __attribute__((noinline)) int put_spec(struct p4_work *w, struct p4_json r, const struct p4_record *record)
{
    struct p4_json pending, part, value;
    uint32_t number, offset, length, uploaded, contiguous, missing = 0;
    int n, found, count, j, k;
    size_t used, i;
    if (p4_json_get(r, "pendingParts", &pending) != 1 ||
        (count = p4_json_array(pending, NULL)) <= 0 ||
        field_u32(r, "uploadedBytes", &uploaded) ||
        field_u32(r, "contiguousUploadedBytes", &contiguous)) return -1;
    /* ZS302A nt26_part_prepare/cloud_storage_upload_open_range: the server
     * owns offset/length. Requested part_size is a hint, not a local layout.
     * Check every advertised range before sending any source bytes. */
    for (j = 0; j < count; ++j) {
        if (p4_json_at(pending, j, &part) ||
            field_u32(part, "partNumber", &number) || !number || number > 10000u ||
            field_u32(part, "offset", &offset) || offset >= record->size ||
            ((number == 1u) != (offset == 0u)) ||
            offset < contiguous || field_u32(part, "length", &length) || !length ||
            length > record->size - offset || length > record->size - missing) return -1;
        missing += length;
        for (k = 0; k < j; ++k) {
            uint32_t previous, previous_offset, previous_length;
            if (p4_json_at(pending, k, &value) || field_u32(value, "partNumber", &previous) ||
                field_u32(value, "offset", &previous_offset) ||
                field_u32(value, "length", &previous_length) || previous == number ||
                (previous < number && previous_offset + previous_length > offset) ||
                (previous > number && offset + length > previous_offset)) return -1;
        }
    }
    if (uploaded > record->size || missing > record->size - uploaded ||
        p4_json_at(pending, 0, &part) ||
        field_u32(part, "offset", &w->part_offset) ||
        field_u32(part, "length", &length) || p4_json_get(part, "url", &value) != 1) return -1;
    w->original = record;
    w->part = *record;
    w->part.source = w; w->part.read = part_read; w->part.size = length;
    memcpy(w->spec, "PUT\n", 4);
    n = p4_json_string(value, w->spec + 4, sizeof(w->spec) - 4);
    if (n < 10 || strncmp(w->spec + 4, "https://", 8)) return -1;
    used = 4u + (size_t)n;
    for (i = 4; i < used; ++i)
        if ((unsigned char)w->spec[i] <= 32 || (unsigned char)w->spec[i] >= 127) return -1;
    if (used + 2 >= sizeof(w->spec)) return -1;
    w->spec[used++] = '\n';
    found = p4_json_get(part, "headers", &value);
    if (!found) found = p4_json_get(part, "uploadHeaders", &value);
    if (!found) found = p4_json_get(r, "uploadHeaders", &value);
    if (found < 0) return -1;
    if (found) n = p4_json_headers(value, w->spec + used, sizeof(w->spec) - used);
    else n = snprintf(w->spec + used, sizeof(w->spec) - used,
                      "Content-Type: application/octet-stream\r\n");
    return n < 0 || (size_t)n >= sizeof(w->spec) - used ? -1 : 0;
}

static int verify_source(struct p4_work *w, const struct p4_record *r)
{
    uint32_t offset;
    if (!tc_sha256_init(&w->crypto.sha)) return P4_ERR_SOURCE;
    for (offset = 0; offset < r->size;) {
        uint32_t length = r->size - offset;
        if (length > sizeof(w->body)) length = sizeof(w->body);
        if (r->read(r->source, offset, (uint8_t *)w->body, length) ||
            !tc_sha256_update(&w->crypto.sha, (const uint8_t *)w->body, length)) return P4_ERR_SOURCE;
        offset += length;
    }
    if (!tc_sha256_final(w->sign.digest, &w->crypto.sha)) return P4_ERR_SOURCE;
    tohex(w->sign.digest, sizeof(w->sign.digest), w->sign.hash);
    return strcmp(w->sign.hash, r->sha256) ? P4_ERR_SOURCE : 0;
}
static int request_error(int ret, unsigned int http, bool put)
{
    /* A clean HTTP status must never hide a source/CRC/DMA/teardown error. */
    if (ret && ret != P4_ERR_NETWORK && ret != P4_ERR_HTTP)
        return ret <= P4_ERR_NETWORK && ret >= P4_ERR_HTTP ? ret : P4_ERR_LOCAL;
    if (ret == P4_ERR_NETWORK) return ret;
    if (http == 408 || http == 429 || (http >= 500 && http <= 599) || (put && http == 403))
        return P4_ERR_NETWORK;
    if (http == 401 || http == 403) return P4_ERR_AUTH;
    return ret || http < 200 || http >= 300 ? P4_ERR_HTTP : 0;
}
int p4_upload_run(struct p4_work *w, const struct p4_record *r,
                  const uint8_t *key, size_t key_size, const struct p4_io *io)
{
    struct p4_json res, pending;
    bool done, await_progress = false;
    uint32_t uploaded = 0, contiguous = 0, high = 0, high_contiguous = 0;
    unsigned int retries = 0;
    int ret = P4_ERR_SOURCE, count, n;
    const char *stage = "SOURCE";
    if (!w || !io || !io->device || !io->random || !io->request || !io->event || !io->sleep_ms || !io->now_ms)
        return P4_ERR_LOCAL;
    memset(w, 0, sizeof(*w));
    if (!r || !r->read || !r->size || r->size > 0x7fffffffu || !key || !key_size ||
        !safe_text(r->local_id) || !safe_text(r->filename) || !safe_text(r->recorded_at) ||
        !safe_text(r->sample_md5) || strlen(r->sample_md5) != 32 || !r->sha256 ||
        strlen(r->sha256) != 64 || r->part_size > P4_PART_SIZE) goto fail;
    w->part_size = r->part_size ? r->part_size : P4_PART_SIZE;
    if ((ret = verify_source(w, r))) goto fail;
    io->event(io->context, stage, 0, r->size);
    for (;;) {
        /* Do not start another request after the recovery window. An already
         * running request retains its own transport timeout/cleanup owner. */
        if (network_expired(w, io)) {
            stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail;
        }
        stage = "INIT";
        if (init_body(w, r)) { ret = P4_ERR_LOCAL; goto fail; }
        ret = signed_post(w, key, key_size, io, P4_INIT_PATH);
        ret = request_error(ret, w->http, false);
        if (ret) goto retry;
        ret = P4_ERR_PROTOCOL;
        if (result(w, &res, &done) || identity(w, res, done) || progress(res, r->size, done) ||
            field_u32(res, "uploadedBytes", &uploaded) ||
            field_u32(res, "contiguousUploadedBytes", &contiguous) ||
            uploaded < high || contiguous < high_contiguous) goto fail;
        io->event(io->context, stage, (int)w->http, w->received);
        n = p4_json_get(res, "pendingParts", &pending);
        if (n < 0 || (!done && n != 1)) goto fail;
        count = n ? p4_json_array(pending, NULL) : 0;
        if (count < 0 || (done && count)) goto fail;
        if (done) goto success;
        if (uploaded > high || contiguous > high_contiguous) {
            retries = 0;
            w->network_waiting = false;
            w->network_retries = 0;
        }
        else if (await_progress) {
            await_progress = false;
            ret = P4_ERR_NETWORK;
            stage = "NO_PROGRESS";
            goto retry;
        }
        high = uploaded; high_contiguous = contiguous;
        await_progress = false;
        if (count) {
            stage = "PUT_PREPARE";
            if (put_spec(w, res, r)) goto fail;
            io->event(io->context, "PART_RANGE", (int)w->part_offset, w->part.size);
            stage = "PUT";
            if (network_expired(w, io)) { stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail; }
            if (w->requests >= P4_REQUEST_LIMIT) { ret = P4_ERR_LIMIT; goto fail; }
            ++w->requests;
            w->received = w->http = 0;
            ret = io->request(io->context, w->spec, NULL, &w->part, w->part.size,
                              w->response, P4_RESPONSE_MAX, &w->received, &w->http);
            io->event(io->context, stage, ret ? ret : (int)w->http, w->part_offset);
            ret = request_error(ret, w->http, true);
            if (ret) goto retry;
            await_progress = true;
            continue;
        }
        stage = "COMPLETE_PREFLIGHT";
        if (progress(res, r->size, true)) goto fail;
        if ((ret = verify_source(w, r))) goto fail;
        n = snprintf(w->body, sizeof(w->body),
            "{\"syncSessionId\":\"%s\",\"multipartUploadId\":\"%s\"}", w->session, w->upload);
        if (n <= 0 || (size_t)n >= sizeof(w->body)) { ret = P4_ERR_LOCAL; goto fail; }
        stage = "COMPLETE";
        ret = signed_post(w, key, key_size, io, P4_COMPLETE_PATH);
        ret = request_error(ret, w->http, false);
        if (ret) goto retry;
        ret = P4_ERR_PROTOCOL;
        if (result(w, &res, &done) || !done || identity(w, res, true) || progress(res, r->size, true)) goto fail;
        n = p4_json_get(res, "pendingParts", &pending);
        if (n < 0 || (n && p4_json_array(pending, NULL) != 0)) goto fail;
        io->event(io->context, stage, (int)w->http, w->received);
        goto success;
retry:
        if (ret != P4_ERR_NETWORK) goto fail;
        io->event(io->context, stage, ret, w->http);
        if (w->requests >= P4_REQUEST_LIMIT) {
            ret = P4_ERR_LIMIT; goto fail;
        }
        /* No HTTP response means transport availability is uncertain. A valid
         * init with unchanged progress must not restart this timer; otherwise
         * repeated failing PUT/complete could keep a job alive forever. */
        if (!w->http && strcmp(stage, "NO_PROGRESS")) {
            uint32_t now = io->now_ms(io->context), elapsed, delay;
            if (!w->network_waiting) {
                w->network_waiting = true;
                w->network_started = now;
            }
            elapsed = (uint32_t)(now - w->network_started);
            if (elapsed >= P4_NETWORK_WINDOW_MS) {
                stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail;
            }
            delay = w->network_retries < 4u ? 1000u << w->network_retries : P4_NETWORK_BACKOFF_MAX_MS;
            if (delay > P4_NETWORK_WINDOW_MS - elapsed) delay = P4_NETWORK_WINDOW_MS - elapsed;
            ++w->network_retries;
            io->event(io->context, "NETWORK_WAIT", w->network_retries, P4_NETWORK_WINDOW_MS - elapsed);
            io->event(io->context, "RETRY_INIT", w->network_retries, delay);
            io->sleep_ms(io->context, delay);
            continue;
        }
        /* HTTP 408/429/5xx, PUT 403 and server no-progress keep their existing
         * bounded policy. Authentication, protocol, source and SPI faults never
         * enter either retry branch. */
        if (retries >= P4_RETRY_LIMIT) { ret = P4_ERR_LIMIT; goto fail; }
        ++retries;
        io->event(io->context, "RETRY_INIT", retries, 1000u << (retries - 1));
        io->sleep_ms(io->context, 1000u << (retries - 1));
        /* No PUT/complete replay here. Only init decides what remains. */
    }
success:
    stage = "SOURCE_FINAL";
    if ((ret = verify_source(w, r))) goto fail;
    io->event(io->context, "SYNC_SUCCESS", 0, r->size);
    return 0;
fail:
    if (ret == P4_ERR_LIMIT && network_expired(w, io)) stage = "NETWORK_LIMIT";
    io->event(io->context, stage, ret, w->http);
    return ret;
}
