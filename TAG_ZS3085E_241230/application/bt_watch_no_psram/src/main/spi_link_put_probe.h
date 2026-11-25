/* 无 SD 卡的 P3 探针。串口 Shell 串行收取请求规格，发起 SPI 和网络操作前先检查总长和 CRC。 */
#ifndef ATS_SPI_LINK_PUT_PROBE_H
#define ATS_SPI_LINK_PUT_PROBE_H

/* 系统堆由应用内存池提供。诊断使用 K_NO_WAIT 分配，空间不足时立即报错。 */
extern struct k_heap sys_mem_pool;    /* 应用定义系统堆，此处仅声明供暂存规格使用。 */

static struct {
    uint8_t *data;                    /* begin 分配的请求规格暂存区。 */
    uint32_t size, used, crc;         /* 协议总长度、已连续填入的字节数和预期 CRC32。 */
} sfile_put_spec;

/**
  * @brief  解码串口分段输入中的一位十六进制数。
  * @param  c：[输入]一个 ASCII 字符。
  * @retval 0～15 为数值，-1 为非法字符。
  * @note   允许大小写十六进制字母。
  */
static int sfile_probe_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';         /* 数字字符对应十六进制 0～9。 */
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;    /* 小写字母对应 10～15。 */
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;    /* 同时支持大写输入。 */
    return -1;                                        /* 非十六进制字符不可纳入暂存块。 */
}

/**
  * @brief  解析有上限的十进制非负整数。
  * @param  s：[输入]以零结束的非空指针。
  * @param  max：[输入]含端点的上限。
  * @param  value：[输出]非空整数地址。
  * @retval true 成功，false 为格式或范围错误。
  * @note   拒绝空串、符号及尾随字符，成功才写出值。
  */
static bool sfile_probe_number(const char *s, uint32_t max, uint32_t *value)
{
    uint32_t n = 0;                                                  /* 初始值零，使逐位十进制累加有确定起点。 */
    if (!*s) return false;                                           /* 空字符串不表示合法长度或偏移。 */
    while (*s) {                                                     /* 读到字符串结尾才算整个数字合法。 */
        unsigned int digit;                                          /* 当前待乘加的十进制数字，范围 0～9。 */
        if (*s < '0' || *s > '9') return false;                      /* 命令行不接受符号、空格或数字以外的字符。 */
        digit = (unsigned int)(*s++ - '0');                          /* 转成数值后推进输入位置。 */
        if (digit > max || n > (max - digit) / 10u) return false;    /* 先检查单个数字及加法剩余额度，再乘十，避免溢出。 */
        n = n * 10u + digit;                                         /* 剩余范围已校核，此处乘十不会回绕。 */
    }
    *value = n;                                                      /* 完整字符串验证通过才提交数值。 */
    return true;                                                     /* 所有输入已完整转换。 */
}

/**
  * @brief  擦除并释放上次 Shell 暂存的 HTTP 请求规格。
  * @param  无。
  * @retval 无。
  * @note   先按原分配长度逐字节擦除，再清空指针、进度及 CRC。
  */
static void sfile_probe_clear(void)
{
    if (sfile_put_spec.data) {                              /* 尚未 begin 或已 clear 时没有可释放的数据区。 */
        volatile uint8_t *p = sfile_put_spec.data;          /* 将擦除写作用于真实缓存字节。 */
        uint32_t n = sfile_put_spec.size;                   /* 使用原申请长度覆盖整个规格区。 */
        while (n--) *p++ = 0;                               /* 不论实际已写入多少，释放前擦除整块分配空间。 */
        k_heap_free(&sys_mem_pool, sfile_put_spec.data);    /* 擦除之后才把缓冲归还原系统堆。 */
    }
    memset(&sfile_put_spec, 0, sizeof(sfile_put_spec));     /* 清空数据指针、容量、写入进度和预期校验值。 */
}

/**
  * @brief  在串口命令间暂存并拼接一份 PUT 请求规格。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]子命令及对应的长度、CRC32 或偏移和十六进制数据。
  * @retval 0 成功，-EINVAL 为格式、范围或进度错误，-ENOMEM 为堆空间不足。
  * @note   begin 设置总长度与 CRC32；data 只允许按序追加；clear 擦除暂存。
  */
static int shell_spi2putcfg(const struct shell *sh, size_t argc, char *argv[])
{
    uint32_t size, offset, crc = 0;                                                        /* size 和 offset 分别用于 begin 和 data，CRC 从零拼接。 */
    unsigned int i, chars;                                                                 /* i 遍历输入字符；chars 记录本次十六进制文本长度。 */

    if (argc == 2 && !strcmp(argv[1], "clear")) {                                          /* clear 不需要规格长度或数据。 */
        sfile_probe_clear();                                                               /* 擦除已暂存的请求规格并清空游标。 */
        shell_print(sh, "SPI PUT CFG cleared");
        return 0;                                                                          /* 已处理清除命令，此调用不会进入传输。 */
    }
    if (argc != 4) return -EINVAL;                                                         /* begin 和 data 都需要子命令及两项数据。 */

    /* begin 创建一笔新的规格暂存。总长和预期 CRC 都须先验证。 */
    if (!strcmp(argv[1], "begin")) {
        if (!sfile_probe_number(argv[2], SFILE_HTTP_META_MAX, &size) || !size ||
            strlen(argv[3]) != 8u) return -EINVAL;                                         /* 预期 CRC32 使用恰好八位十六进制表示。 */
        for (i = 0; i < 8u; ++i) {                                                         /* 将预期校验值的八个字符逐位解码。 */
            int digit = sfile_probe_hex(argv[3][i]);                                       /* 输入一位对应 CRC32 的四位。 */
            if (digit < 0) return -EINVAL;                                                 /* 碰到非法位时保留上次暂存，不启动新规格。 */
            crc = (crc << 4) | (unsigned int)digit;                                        /* 按输入顺序拼出完整的 32 位预期值。 */
        }
        sfile_probe_clear();                                                               /* 新规格的参数验证通过后才清除上一份。 */
        sfile_put_spec.data = k_heap_alloc(&sys_mem_pool, size, K_NO_WAIT);                /* 使用即时分配，使 Shell 在内存不足时可以立即返回。 */
        if (!sfile_put_spec.data) return -ENOMEM;                                          /* 申请不到指定字节数时立即向终端返回内存不足。 */
        sfile_put_spec.size = size;                                                        /* 保存校验过的总长以约束后续追加。 */
        sfile_put_spec.crc = crc;                                                          /* 保留外部声明的整体 CRC，在真正发 PUT 前比对。 */
    } else if (!strcmp(argv[1], "data")) {

        /* data 按明确偏移追加，一条命令至多携带 128 个十六进制字符，即 64 个原始字节。 */
        chars = strlen(argv[3]);
        if (!sfile_put_spec.data || !chars || chars > 128u || (chars & 1u) ||
            !sfile_probe_number(argv[2], SFILE_HTTP_META_MAX, &offset) ||
            offset != sfile_put_spec.used || chars / 2u > sfile_put_spec.size - offset)    /* 必须顺着当前游标追加，且此段不超出剩余容量。 */
            return -EINVAL;                                                                /* 任一长度或偏移检查失败时本段完全不写入。 */
        /* 整段十六进制通过校验以后才写暂存区，防止错误输入留下半段数据。 */
        for (i = 0; i < chars; ++i)
            if (sfile_probe_hex(argv[3][i]) < 0) return -EINVAL;                           /* 先扫描整串，任何非法位都会取消这段数据。 */
        for (i = 0; i < chars; i += 2u)                                                    /* 数据确认后，每两个字符写成一个原始字节。 */
            sfile_put_spec.data[offset++] = (uint8_t)((sfile_probe_hex(argv[3][i]) << 4) |
                                                     sfile_probe_hex(argv[3][i + 1u]));    /* 先高四位再低四位还原二进制字节。 */
        sfile_put_spec.used = offset;                                                      /* 整段成功后提交新的已用长度。 */
    } else return -EINVAL;                                                                 /* 不支持的子命令不能改变暂存进度。 */
    shell_print(sh, "SPI PUT CFG bytes=%u/%u", sfile_put_spec.used, sfile_put_spec.size);
    return 0;                                                                              /* 正常追加后只报告累计长度，不发送网络请求。 */
}

/**
  * @brief  用暂存的请求规格和确定性生成数据执行一次 PUT。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]正文的十进制字节数，范围 1～10000*SFILE_DATA_BYTES。
  * @retval 0 成功，负值为参数、规格 CRC、SPI 或交付错误。
  * @note   通过入口校验后调用传输，返回即擦除规格，即使远端结果未知也不复用。
  */
static int shell_spi2putgen(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_http_files http = { .console_result = true };                 /* 响应通过串口分行输出，不要求本地文件系统。 */
    struct sfile_sender x = {
        .http = &http, .read_block = sfile_read_generated,
        .bad_block = UINT32_MAX, .delivery_command = SFILE_START_HTTP,         /* 正常请求关闭合成坏块，使用通用 HTTP 分块规格。 */
    };
    static const char prefix[] = "PUT\nhttps://";                              /* 规格应以 PUT 方法行和 HTTPS 地址前缀起始。 */
    unsigned int i;                                                            /* 逐字节匹配固定前缀使用的偏移。 */
    int ret;                                                                   /* 同步传输的返回值需保留到清理结束。 */

    /* 发起网络前核对生成长度、规格完整性和整体 CRC，再检查规格以 HTTPS PUT 开头。 */
    if (argc != 2 || !sfile_probe_number(argv[1], 10000u * SFILE_DATA_BYTES, &x.size) ||
        !x.size) return -EINVAL;                                               /* 生成正文必须有至少一个字节。 */
    if (!sfile_put_spec.data || sfile_put_spec.used != sfile_put_spec.size)    /* 必须已完整接收 begin 承诺的总长度。 */
        return -EINVAL;                                                        /* 尚未 begin 或暂存未齐，全程不发 SPI 请求。 */
    if ((sfile_crc32(UINT32_MAX, sfile_put_spec.data, sfile_put_spec.size) ^ UINT32_MAX) !=
        sfile_put_spec.crc) return -EBADMSG;                                   /* 不匹配表示串口暂存资料损坏，禁止发起网络操作。 */
    if (sfile_put_spec.size < sizeof(prefix)) return -EINVAL;                  /* 规格至少包含前缀和后续一个字节。 */
    for (i = 0; i < sizeof(prefix) - 1u; ++i)                                  /* 比较固定前缀时不将 C 字符串结尾零作为协议信息。 */
        if (sfile_put_spec.data[i] != (uint8_t)prefix[i]) return -EINVAL;      /* 逐字节检查方法行和 HTTPS 前缀，不假定缓冲以零结束。 */
    http.spec_data = sfile_put_spec.data;                                      /* 在同步文件会话中借用暂存数据作为请求元信息。 */
    http.spec_size = (uint16_t)sfile_put_spec.size;                            /* 告诉文件协议按规格实际字节总数分段。 */
    ret = sfile_run(sh, x, NULL, true, SFILE_HTTPS_HZ);                        /* 申请配对传输，使用蜂窝/TLS 测试的 SPI 时钟。 */
    /* 已尝试传输的请求只使用一次，SPI 失败或远端结果未知时同样擦除。 */
    sfile_probe_clear();                                                       /* 返回之后不再保留已尝试过的规格。 */
    return ret;                                                                /* 清理暂存后返回原始传输状态。 */
}

#endif
