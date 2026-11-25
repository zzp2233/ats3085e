#ifndef SPI_LINK_P4_H
#define SPI_LINK_P4_H
#ifdef P4_EMBEDDED_VALIDATION
#include "p4_upload.h"
#include "p4_fixture.h"
#include "p4_fault.h"
#include <sdfs.h>

/* Optional P4-only clock comparison; ordinary P4 builds retain 4 MHz.
 * Request 2.3 MHz for the supported 32 MHz HOSC /14 (~2.286 MHz). */
#ifndef P4_SPI_TEST_HZ
#define P4_SPI_TEST_HZ SFILE_HTTPS_HZ
#endif
#if P4_SPI_TEST_HZ != 2300000u && P4_SPI_TEST_HZ != 4000000u
#error Unsupported P4 SPI test clock
#endif

/* The shell is already a dedicated task and serializes these commands with
 * P3 diagnostics. Keep the small no-PSRAM build free of another permanent stack. */
struct p4_spi_source {
    const uint8_t *body;
    const struct p4_record *record;
    bool failed;
};
struct p4_spi_context {
    const struct shell *shell;
    char *response;
    unsigned int capacity, used;
    bool response_error;
    struct p4_fault fault;
    bool radio_sent;
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once;
#endif
    /* Allocated with p4_work, not on the 2 KiB shell task stack. */
    struct p4_spi_source source;
    struct sfile_http_files http;
    struct sfile_sender sender;
    uint8_t device_info[SFILE_DEVICE_INFO_BYTES];
};
static void p4_spi_stack(struct p4_spi_context *c, const char *stage)
{
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    size_t unused;
    if (!k_thread_stack_space_get(k_current_get(), &unused))
        shell_print(c->shell, "P4 STACK %s unused=%u", stage, (unsigned int)unused);
#else
    (void)c; (void)stage;
#endif
}
static int p4_spi_receive(void *context, uint32_t offset, const uint8_t *data, unsigned int size)
{
    struct p4_spi_context *c = context;
    if (offset != c->used || offset > c->capacity || size > c->capacity - offset) {
        c->response_error = true;
        return -EFBIG;
    }
    memcpy(c->response + offset, data, size);
    c->used += size;
    c->response[c->used] = 0;
    return 0;
}
static int p4_spi_read(void *context, uint32_t index, uint8_t *data,
                        unsigned int size, bool repair)
{
    (void)repair;
    struct p4_spi_source *source = context;
    if (source->record) {
        uint32_t offset = index * SFILE_DATA_BYTES;
        int ret;
        if (offset > source->record->size || size > source->record->size - offset) ret = -EINVAL;
        else ret = source->record->read(source->record->source, offset, data, size);
        if (ret) source->failed = true;
        return ret;
    }
    memcpy(data, source->body + index * SFILE_DATA_BYTES, size);
    return 0;
}
static int p4_spi_request(void *context, const char *spec, const uint8_t *body,
                           const struct p4_record *record, uint32_t length, char *response, unsigned int capacity,
                           unsigned int *received, unsigned int *http_code)
{
    struct p4_spi_context *c = context;
    struct sfile_http_files *http = &c->http;
    struct sfile_sender *x = &c->sender;
    int ret;
    if (!length || strlen(spec) > SFILE_HTTP_META_MAX) return P4_ERR_LOCAL;
    memset(http, 0, sizeof(*http));
    memset(x, 0, sizeof(*x));
    memset(&c->source, 0, sizeof(c->source));
    c->source.body = body; c->source.record = record;
    http->spec_data = (const uint8_t *)spec; http->spec_size = strlen(spec);
#ifdef P4_RADIO_TEST
    if (!c->radio_sent &&
        ((c->fault.mode == 64u && !strncmp(spec, "POST\n" P4_ORIGIN P4_INIT_PATH "\n",
                         sizeof("POST\n" P4_ORIGIN P4_INIT_PATH "\n") - 1)) ||
         (c->fault.mode == 128u && !strncmp(spec, "PUT\n", 4)))) {
        http->radio_test = c->fault.mode == 64u ? 1u : 2u;
        c->radio_sent = true; /* Never re-arm automatically after ambiguous IO. */
        shell_print(c->shell, "P4 RADIO request mode=%u; check COM6 for actual RF state", http->radio_test);
    }
#endif
    http->receive = p4_spi_receive; http->receive_context = c;
    x->http = http; x->source = &c->source; x->read_block = p4_spi_read;
    x->size = length; x->bad_block = UINT32_MAX; x->delivery_command = SFILE_START_HTTP;
    c->response = response; c->capacity = capacity; c->used = 0;
    c->response_error = false;
    response[0] = 0;
#ifdef P4_SPI_DIAG
    http->diag = k_heap_alloc(&sys_mem_pool, sizeof(*http->diag), K_NO_WAIT);
    if (!http->diag) return P4_ERR_LOCAL;
    memset(http->diag, 0, sizeof(*http->diag));
#endif
    ret = sfile_run_context(c->shell, x, NULL, true, P4_SPI_TEST_HZ);
#ifdef P4_SPI_DIAG
    {
        unsigned int i;
        const uint32_t *r = http->diag->initial;
        shell_print(c->shell, "P4 DIAG initial clk=%x ctrl=%x sta=%x pins=%x/%x/%x/%x",
                    r[0],r[1],r[2],r[3],r[4],r[5],r[6]);
        for (i = 0; i < http->diag->count; ++i) {
            const struct sfile_diag_sample *s = &http->diag->samples[i];
            r = s->regs;
            shell_print(c->shell, "P4 DIAG pair=%u wait_us=%u transfer_us=%u cmd=%u clk=%x ctrl=%x sta=%x pins=%x/%x/%x/%x",
                        i,http->diag->waited_us,s->transfer_us,s->command,r[0],r[1],r[2],r[3],r[4],r[5],r[6]);
            shell_print(c->shell, "P4 DIAG tx=%08x/%08x/%08x/%08x rx=%08x/%08x/%08x/%08x",
                        s->tx[0],s->tx[1],s->tx[2],s->tx[3],s->rx[0],s->rx[1],s->rx[2],s->rx[3]);
        }
        k_heap_free(&sys_mem_pool, http->diag);
        http->diag = NULL;
    }
#endif
    *received = c->used; *http_code = http->http_code;
    if (!ret && (!http->response_complete || c->used != http->response_size)) ret = -EBADMSG;
    shell_print(c->shell, "P4 HTTP http=%u bytes=%u complete=%u link_failed=%u ret=%d",
                *http_code, *received, http->response_complete, http->link_failed, ret);
    p4_spi_stack(c, "HTTP");
    /* Only explicit peer HTTPS failures on a clean SPI session may retry.
     * READY timeouts, CRC faults and successful RESYNC remain task failures. */
    if (c->source.failed) return P4_ERR_SOURCE;
    if (c->response_error) return P4_ERR_PROTOCOL;
    if (http->transport_fault || http->link_failed) return P4_ERR_LINK;
    if (http->wire_status == SFILE_SEND_FAILED || http->wire_status == SFILE_HTTP_FAILED)
        return http->http_code && (http->http_code < 200 || http->http_code >= 300) ?
               P4_ERR_HTTP : P4_ERR_NETWORK;
    return ret ? P4_ERR_LINK : 0;
}
/* Extra wrapper exists only on explicitly requested diagnostic invocations. */
static int p4_spi_test_request(void *context, const char *spec, const uint8_t *body,
                              const struct p4_record *record, uint32_t length,
                              char *response, unsigned int capacity,
                              unsigned int *received, unsigned int *http)
{
    struct p4_spi_context *c = context;
    unsigned int previous = c->fault.fired;
    int ret;
    *received = *http = 0; response[0] = 0;
    ret = p4_fault_apply(&c->fault, spec, false, 0, response, received, http);
    if (!ret) {
        ret = p4_spi_request(context, spec, body, record, length, response, capacity, received, http);
        ret = p4_fault_apply(&c->fault, spec, true, ret, response, received, http);
    }
    if (c->fault.fired != previous)
        shell_print(c->shell, "P4 INJECT fired=%u simulated_ret=%d http=%u; next action must INIT",
                    c->fault.fired ^ previous, ret, *http);
    return ret;
}
static int p4_spi_device(void *context, char imei[16], uint32_t *utc)
{
    struct p4_spi_context *c = context;
    uint8_t *info = c->device_info;
    struct sfile_sender *x = &c->sender;
    int ret;
    memset(info, 0, sizeof(c->device_info));
    memset(x, 0, sizeof(*x));
    x->device_info = info; x->delivery_command = SFILE_DEVICE_INFO; x->bad_block = UINT32_MAX;
#ifdef P4_SPI_WITNESS_TEST
    x->witness_once = c->witness_once;
#endif
    ret = sfile_run_context(c->shell, x, NULL, true, P4_SPI_TEST_HZ);
#ifdef P4_SPI_WITNESS_TEST
    if (x->witness_once) {
        bool recovered = ret == -EBADMSG && x->witness_notice && !x->resync_needed &&
                         !x->link_failed && !s_ready_faulted;
        shell_print(c->shell, "P4 WITNESS %s tid=%u frames=%u notice=%u link_ret=%d",
                    recovered ? "RECOVERED" : "UNVERIFIED", x->tid,
                    (unsigned int)x->witness_frames, (unsigned int)x->witness_notice, ret);
        return P4_ERR_LINK; /* End this job before its first HTTP request. */
    }
#endif
    if (ret) return P4_ERR_LINK;
    if (info[0] != 1) return P4_ERR_PROTOCOL;
    *utc = sfile_u32(info + 1);
    memcpy(imei, info + 5, 15); imei[15] = 0;
    shell_print(c->shell, "P4 DEVICE utc=%u imei_suffix=%.4s", *utc, imei + 11);
    p4_spi_stack(c, "DEVICE");
    return 0;
}
static int p4_spi_random(void *context, uint8_t *out, unsigned int size)
{
    (void)context;
    return sfile_entropy_fill(out, size);
}
static void p4_spi_event(void *context, const char *stage, int code, uint32_t value)
{
    struct p4_spi_context *c = context;
    shell_print(c->shell, "P4 STAGE %s code=%d value=%u", stage, code, value);
}
static void p4_spi_sleep(void *context, uint32_t milliseconds)
{
    (void)context;
    k_msleep(milliseconds);
}
static uint32_t p4_spi_now(void *context)
{
    (void)context;
    return k_uptime_get_32();
}
static int p4_resource_read(void *context, uint32_t offset, uint8_t *data, unsigned int size)
{
    struct sd_file *file = context;
    if (sd_fseek(file, (int)offset, FS_SEEK_SET)) return -EIO;
    return sd_fread(file, data, (int)size) == (int)size ? 0 : -EIO;
}
struct p4_spi_job {
    struct p4_work work;
    struct p4_spi_context context;
    struct p4_record record;
    struct p4_io io;
};
static int shell_p4upload(const struct shell *sh, size_t argc, char *argv[])
{
    struct p4_spi_job *job;
    struct p4_record *record;
    const char *local_id = p4_fixture.local_id;
    uint32_t part_size = p4_fixture.part_size;
    struct sd_file *file;
    int ret;
    int fault = 0;
    if (argc < 1 || argc > 4) {
        shell_error(sh, "Usage: app p4upload [14-digit test localFileId [128|4096 part KiB [fault]]]");
        return -EINVAL;
    }
    if (argc >= 2) {
        unsigned int i;
        if (strlen(argv[1]) != 14) {
            shell_error(sh, "P4 localFileId must be 14 digits; no request sent");
            return -EINVAL;
        }
        for (i = 0; i < 14; ++i) {
            if (argv[1][i] < '0' || argv[1][i] > '9') {
                shell_error(sh, "P4 localFileId must be 14 digits; no request sent");
                return -EINVAL;
            }
        }
        /* This command runs synchronously. Keep the chosen identity through
         * init, PUT and complete; retries must explicitly reuse the same ID. */
        local_id = argv[1];
    }
    if (argc >= 3) {
        if (!strcmp(argv[2], "128")) part_size = 128u * 1024u;
        else if (!strcmp(argv[2], "4096")) part_size = P4_PART_SIZE;
        else { shell_error(sh, "P4 part KiB must be 128 or 4096; no request sent"); return -EINVAL; }
    }
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once = argc == 4 && !strcmp(argv[3], "witness-once");
    if (witness_once)
        shell_print(sh, "P4 WITNESS armed device pair");
    else
#endif
    if (argc == 4 && (fault = p4_fault_mode(argv[3])) < 0) {
#ifdef P4_RADIO_TEST
        shell_error(sh, "P4 fault: put-before|put-lost|complete-before|complete-lost|both-lost|put-403|init-503|radio-before|radio-mid");
#else
        shell_error(sh, "P4 fault: put-before|put-lost|complete-before|complete-lost|both-lost|put-403|init-503");
#endif
        return -EINVAL;
    }
    file = sd_fopen("/NOR:A/p4.m4a");
    if (!file) { shell_error(sh, "P4 source missing; flash the matching resource image"); return -ENOENT; }
    if ((uint32_t)file->size != p4_fixture.size) { sd_fclose(file); return -EBADMSG; }
    job = k_heap_alloc(&sys_mem_pool, sizeof(*job), K_NO_WAIT);
    if (!job) {
        sd_fclose(file);
        shell_error(sh, "P4 FAIL memory need=%u; no network request sent", (unsigned int)sizeof(*job));
        return -ENOMEM;
    }
    memset(job, 0, sizeof(*job));
    job->context.shell = sh;
#ifdef P4_SPI_WITNESS_TEST
    job->context.witness_once = witness_once;
#endif
    job->context.fault.mode = (unsigned int)fault;
    record = &job->record;
    *record = p4_fixture;
    record->source = file; record->read = p4_resource_read;
    record->local_id = local_id; record->part_size = part_size;
    job->io.context = &job->context;
    job->io.device = p4_spi_device; job->io.random = p4_spi_random;
    job->io.request = p4_spi_request; job->io.event = p4_spi_event;
    job->io.sleep_ms = p4_spi_sleep;
    job->io.now_ms = p4_spi_now;
    if (fault) {
#ifdef P4_RADIO_TEST
        if (fault == 64 || fault == 128) {
            shell_print(sh, "P4 RADIO armed=%s; real RF control, automatic restore on 503", argv[3]);
        } else
#endif
        {
        job->io.request = p4_spi_test_request;
        shell_print(sh, "P4 INJECT armed=%u mode=%s; synthetic business IO fault, not physical network loss",
                    (unsigned int)fault, argv[3]);
        }
    }
    shell_print(sh, "P4 BEGIN id=%s file=%s bytes=%u duration_ms=%u sha256=%s",
                record->local_id, record->filename, record->size,
                record->duration_ms, record->sha256);
    shell_print(sh, "P4 SPI requested_hz=%u", (unsigned int)P4_SPI_TEST_HZ);
    shell_print(sh, "P4 MULTIPART part_size=%u requests_max=%u retries_without_progress=%u",
                part_size ? part_size : P4_PART_SIZE, P4_REQUEST_LIMIT, P4_RETRY_LIMIT);
    shell_print(sh, "P4 NETWORK window_ms=%u backoff_max_ms=%u; resume via INIT",
                P4_NETWORK_WINDOW_MS, P4_NETWORK_BACKOFF_MAX_MS);
    ret = p4_upload_run(&job->work, record, p4_fixture_key, p4_fixture_key_size, &job->io);
    sd_fclose(file);
    if (fault == 64 || fault == 128)
        shell_print(sh, "P4 RADIO RESULT %s; COM6 verifies trigger/restoration, upload PASS alone is insufficient",
                    job->context.radio_sent ? "REQUESTED" : "NOT_REQUESTED");
    else if (fault)
        shell_print(sh, "P4 INJECT RESULT %s armed=%u fired=%u",
                    job->context.fault.fired == (unsigned int)fault ? "TRIGGERED" : "NOT_FULLY_TRIGGERED",
                    (unsigned int)fault, job->context.fault.fired);
    {
        volatile uint8_t *p = (volatile uint8_t *)job;
        unsigned int i;
        for (i = 0; i < sizeof(*job); ++i) p[i] = 0;
    }
    k_heap_free(&sys_mem_pool, job);
    shell_print(sh, "P4 RESULT %s bytes=%u id=%s",
                ret ? "FAIL" : "PASS", p4_fixture.size, local_id);
    return ret;
}
#endif
#endif
