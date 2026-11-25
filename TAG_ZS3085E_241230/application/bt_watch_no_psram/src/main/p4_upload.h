#ifndef P4_UPLOAD_H
#define P4_UPLOAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tinycrypt/hmac.h>
#include "p4_json.h"

#define P4_RESPONSE_MAX 6144u
#define P4_SPEC_MAX 3072u
#define P4_PART_SIZE 4194304u
#define P4_REQUEST_LIMIT 128u
#define P4_RETRY_LIMIT 3u
/* Transport failures may return immediately while the modem is registering.
 * Separate their wall-clock budget from HTTP/no-progress retry accounting. */
#define P4_NETWORK_WINDOW_MS 180000u
#define P4_NETWORK_BACKOFF_MAX_MS 10000u
/* IO must positively identify network failures. Unknown errors are fatal. */
enum p4_error {
    P4_ERR_NETWORK = -1001, P4_ERR_LINK = -1002, P4_ERR_SOURCE = -1003,
    P4_ERR_PROTOCOL = -1004, P4_ERR_AUTH = -1005, P4_ERR_LIMIT = -1006,
    P4_ERR_LOCAL = -1007, P4_ERR_HTTP = -1008
};
#define P4_INIT_PATH "/voice-card/device/v1/oss/audio-sync/multipart/init"
#define P4_COMPLETE_PATH "/voice-card/device/v1/oss/audio-sync/multipart/complete"
#define P4_ORIGIN "https://lifeiot.cmaiot.cn:8081"
struct p4_record {
    void *source;
    int (*read)(void *source, uint32_t offset, uint8_t *data, unsigned int size);
    uint32_t size, duration_ms;
    const char *local_id, *filename, *recorded_at, *sample_md5, *sha256;
    uint32_t part_size; /* Zero selects 4 MiB; keep stable for the same ID. */
};
struct p4_io {
    void *context;
    int (*device)(void *context, char imei[16], uint32_t *utc);
    int (*random)(void *context, uint8_t *out, unsigned int size);
    /* Return 0 only for a complete 2xx response and a clean SPI teardown.
     * Never automatically replay a request with an uncertain outcome. */
    int (*request)(void *context, const char *spec, const uint8_t *body,
                   const struct p4_record *record, uint32_t length, char *response, unsigned int capacity,
                   unsigned int *received, unsigned int *http);
    void (*event)(void *context, const char *stage, int code, uint32_t value);
    void (*sleep_ms)(void *context, uint32_t milliseconds);
    uint32_t (*now_ms)(void *context); /* Monotonic uptime, wraps modulo 2^32. */
};
struct p4_work {
    char response[P4_RESPONSE_MAX + 1], spec[P4_SPEC_MAX + 1], body[768];
    char session[192], upload[192], imei[16];
    /* Signing scratch is kept off the 2 KiB shell task stack. */
    struct {
        uint8_t digest[32], random[16];
        char hash[65], signature[65], nonce[33], timestamp[24], imei[16];
    } sign;
    union { struct tc_sha256_state_struct sha; struct tc_hmac_state_struct hmac; } crypto;
    unsigned int received, http;
    uint32_t network_started;
    unsigned int network_retries;
    bool network_waiting;
    uint32_t utc;
    const struct p4_record *original;
    struct p4_record part;
    uint32_t part_offset, part_size;
    unsigned int requests;
};
/* Sealed immutable source; every retry/part begins with server reconciliation.
 * Returns 0 only after matching server completion; does not delete the source. */
int p4_upload_run(struct p4_work *w, const struct p4_record *record,
                  const uint8_t *key, size_t key_size, const struct p4_io *io);
#endif
