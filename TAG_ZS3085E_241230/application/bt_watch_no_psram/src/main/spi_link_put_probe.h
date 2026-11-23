/* No-SD P3 probe. Shell commands are serialized by the UART shell thread.
 * Request bytes are staged with length/CRC before any SPI/network activity. */
#ifndef ATS_SPI_LINK_PUT_PROBE_H
#define ATS_SPI_LINK_PUT_PROBE_H

/* This application's CONFIG_APP_USED_MEM_POOL heap. mem_malloc waits forever
 * on exhaustion; a diagnostic command must instead fail without hanging. */
extern struct k_heap sys_mem_pool;

static struct {
    uint8_t *data;
    uint32_t size, used, crc;
} sfile_put_spec;

static int sfile_probe_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool sfile_probe_number(const char *s, uint32_t max, uint32_t *value)
{
    uint32_t n = 0;
    if (!*s) return false;
    while (*s) {
        unsigned int digit;
        if (*s < '0' || *s > '9') return false;
        digit = (unsigned int)(*s++ - '0');
        if (digit > max || n > (max - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    *value = n;
    return true;
}

static void sfile_probe_clear(void)
{
    if (sfile_put_spec.data) {
        volatile uint8_t *p = sfile_put_spec.data;
        uint32_t n = sfile_put_spec.size;
        while (n--) *p++ = 0;
        k_heap_free(&sys_mem_pool, sfile_put_spec.data);
    }
    memset(&sfile_put_spec, 0, sizeof(sfile_put_spec));
}

static int shell_spi2putcfg(const struct shell *sh, size_t argc, char *argv[])
{
    uint32_t size, offset, crc = 0;
    unsigned int i, chars;

    if (argc == 2 && !strcmp(argv[1], "clear")) {
        sfile_probe_clear();
        shell_print(sh, "SPI PUT CFG cleared");
        return 0;
    }
    if (argc != 4) return -EINVAL;
    if (!strcmp(argv[1], "begin")) {
        if (!sfile_probe_number(argv[2], SFILE_HTTP_META_MAX, &size) || !size ||
            strlen(argv[3]) != 8u) return -EINVAL;
        for (i = 0; i < 8u; ++i) {
            int digit = sfile_probe_hex(argv[3][i]);
            if (digit < 0) return -EINVAL;
            crc = (crc << 4) | (unsigned int)digit;
        }
        sfile_probe_clear();
        sfile_put_spec.data = k_heap_alloc(&sys_mem_pool, size, K_NO_WAIT);
        if (!sfile_put_spec.data) return -ENOMEM;
        sfile_put_spec.size = size;
        sfile_put_spec.crc = crc;
    } else if (!strcmp(argv[1], "data")) {
        chars = strlen(argv[3]);
        if (!sfile_put_spec.data || !chars || chars > 128u || (chars & 1u) ||
            !sfile_probe_number(argv[2], SFILE_HTTP_META_MAX, &offset) ||
            offset != sfile_put_spec.used || chars / 2u > sfile_put_spec.size - offset)
            return -EINVAL;
        /* Validate the entire chunk before advancing or changing staged data. */
        for (i = 0; i < chars; ++i)
            if (sfile_probe_hex(argv[3][i]) < 0) return -EINVAL;
        for (i = 0; i < chars; i += 2u)
            sfile_put_spec.data[offset++] = (uint8_t)((sfile_probe_hex(argv[3][i]) << 4) |
                                                     sfile_probe_hex(argv[3][i + 1u]));
        sfile_put_spec.used = offset;
    } else return -EINVAL;
    shell_print(sh, "SPI PUT CFG bytes=%u/%u", sfile_put_spec.used, sfile_put_spec.size);
    return 0;
}

static int shell_spi2putgen(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_http_files http = { .console_result = true };
    struct sfile_sender x = {
        .http = &http, .read_block = sfile_read_generated,
        .bad_block = UINT32_MAX, .delivery_command = SFILE_START_HTTP,
    };
    static const char prefix[] = "PUT\nhttps://";
    unsigned int i;
    int ret;

    if (argc != 2 || !sfile_probe_number(argv[1], 10000u * SFILE_DATA_BYTES, &x.size) ||
        !x.size) return -EINVAL;
    if (!sfile_put_spec.data || sfile_put_spec.used != sfile_put_spec.size)
        return -EINVAL;
    if ((sfile_crc32(UINT32_MAX, sfile_put_spec.data, sfile_put_spec.size) ^ UINT32_MAX) !=
        sfile_put_spec.crc) return -EBADMSG;
    if (sfile_put_spec.size < sizeof(prefix)) return -EINVAL;
    for (i = 0; i < sizeof(prefix) - 1u; ++i)
        if (sfile_put_spec.data[i] != (uint8_t)prefix[i]) return -EINVAL;
    http.spec_data = sfile_put_spec.data;
    http.spec_size = (uint16_t)sfile_put_spec.size;
    ret = sfile_run(sh, x, NULL, true, SFILE_HTTPS_HZ);
    /* A request is single-use, including ambiguous completion or failed SPI. */
    sfile_probe_clear();
    return ret;
}

#endif
