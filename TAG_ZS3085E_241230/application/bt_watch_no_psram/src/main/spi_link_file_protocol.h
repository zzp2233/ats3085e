/* 主从两端共用的 SPI 文件协议 V1，以 512 字节为物理块，所有多字节字段低字节在前。 */
#ifndef SPI_LINK_FILE_PROTOCOL_H
#define SPI_LINK_FILE_PROTOCOL_H

#define SFILE_MASTER 0xacu                                                  /* 文件协议主机发送方向。 */
#define SFILE_SLAVE 0xcau                                                   /* 从机返回方向，与测试图案协议区分。 */
#define SFILE_VERSION 1u                                                    /* 线布局版本，收发两端必须一致。 */
#define SFILE_HEADER 16u                                                    /* 文件头大小，单位字节，决定有效载荷的起点。 */
#define SFILE_DATA_BYTES 480u                                               /* 单块可承载的文件或控制数据上限，单位字节。 */
#define SFILE_GROUP 100u                                                    /* 每次位图确认最多覆盖 100 个数据块。 */
#define SFILE_DELIVERY_MAX_HOLD_MS 15000u                                   /* 交付诊断允许请求的最大静默毫秒数。 */
#define SFILE_BITMAP 13u                                                    /* 接收 100 个槽的状态需向上取整为 13 字节。 */
#define SFILE_ACK_BYTES (2u + SFILE_BITMAP)                                 /* 普通 ACK 为回显命令、状态字节及整组位图。 */
#define SFILE_DEVICE_INFO_BYTES 20u                                         /* 设备信息为版本、4 字节小端 UTC 秒数、15 个 ASCII 数字的 IMEI。 */
#define SFILE_HTTPS_SEED_BYTES 48u                                          /* 从主机硬件 TRNG 取得 48 字节后交给从机 TLS。 */
#define SFILE_HTTPS_START_FIXED (4u + SFILE_HTTPS_SEED_BYTES)               /* HTTPS 开始参数先放正文长度，再放随机种子。 */
#define SFILE_HTTPS_URL_MAX (SFILE_DATA_BYTES - SFILE_HTTPS_START_FIXED)    /* 旧 HTTPS 开始命令能在一个控制载荷中容纳的 URL 字节数。 */
#define SFILE_HTTP_RESULT_HEAD (SFILE_ACK_BYTES + 2u)                       /* 旧结果头在普通 ACK 后另带 16 位 HTTP 状态码。 */
#define SFILE_HTTP_RESPONSE_MAX 192u                                        /* 旧 FINISH 内联的响应数据上限，单位字节。 */
#define SFILE_HTTP_META_MAX 3072u                                           /* 通用 HTTP 规格经 META 分段传输的总长度上限。 */
#define SFILE_HTTP_BODY_MAX 16384u                                          /* 通用 HTTP 结果可供分页读取的最大字节数。 */
#define SFILE_HTTP_PAGE_BYTES 400u                                          /* 每次 HTTP_READ 返回的正文长度上限。 */
#define SFILE_NAME_MAX 31u                                                  /* 从机文件名的最大字节数，不计终止符。 */
#define SFILE_MAX_SIZE 0x7fffffffu                                          /* 文件长度与有符号 32 位定位接口相容。 */
#define SFILE_CRC_OFFSET (SWAP_BLOCK_SIZE - 2u)                             /* 储存 CRC16 的固定位置，校验范围随有效载荷长度变化。 */
#define SFILE_PROBE_COOKIE 0x51554954u                                      /* 仅用于离线 QUIET 诊断的标记。 */
#define SFILE_PAIR_WITNESS_OFFSET (SWAP_BLOCK_SIZE - 8u)                    /* 见证字位于块尾保留区，独立于头和有效载荷的 CRC16。 */
#define SFILE_PAIR_WITNESS 0xa35ce719u                                      /* 接收端用已知常数检查配对槽的边界。 */
#define SFILE_RESYNC_COOKIE 0x5253594eu                                     /* 要求双方重置配对传输边界的 V1 标记。 */
#define SFILE_RECOVERY_COOKIE 0x52585032u                                   /* 从机通告 RX 边界已重建；主机还需重新握手。 */
#define SFILE_BOOT_COOKIE 0x42535431u                                       /* 启动期单块授权握手的版本标记。 */
#define SFILE_LINK_BOOT 19u                                                 /* 独立的新启动命令号，避免改变现有枚举的线值。 */

/* 控制帧以传输号、请求号和索引匹配 ACK。IDLE 仅占一个流水槽，无业务载荷。 */
enum sfile_command {
    SFILE_IDLE, SFILE_START, SFILE_DATA, SFILE_GROUP_END,    /* 依次为空闲填充、开始落盘、文件数据块和分组确认。 */

    /* QUERY/FINISH/ABORT/ACK 用于查询、提交、取消和回执；后续 START 类型指定 RAM、配对、交付、流式或 HTTP 模式。META/READ 分段写入请求规格和回读响应，末三项用于设备信息和运行期重同步。 */
    SFILE_QUERY, SFILE_FINISH, SFILE_ABORT, SFILE_ACK, SFILE_START_RAM, SFILE_START_PAIR, SFILE_START_DELIVERY, SFILE_START_STREAM, SFILE_START_HTTPS, SFILE_START_HTTP, SFILE_HTTP_META, SFILE_HTTP_READ, SFILE_DEVICE_INFO, SFILE_LINK_RESYNC, SFILE_LINK_RECOVERY
};
enum sfile_status {
    SFILE_OK, SFILE_MISSING, SFILE_BUSY, SFILE_INVALID,                                  /* 依次表示成功、缺块、仍在处理和输入或状态无效。 */
    SFILE_STORAGE, SFILE_CHECKSUM, SFILE_EXISTS, SFILE_SEND_FAILED, SFILE_HTTP_FAILED    /* 依次表示存储失败、文件校验错误、目标已存在、交付发送失败和 HTTP 失败。 */
};

/**
  * @brief  从非对齐线协议字段读取小端 32 位值。
  * @param  p：[输入]至少四个连续可读字节。
  * @retval 还原的无符号值。
  * @note   按字节拼接，不依赖主机对齐要求。
  */
static inline uint32_t sfile_u32(const uint8_t *p)
{
    /* 先分别转为 32 位再移位，避免未对齐的总线宽读。 */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);    /* 四个字节分别左移到各自的字节位置再作按位或。 */
}

/**
  * @brief  将 16 位值写成低字节在前的线格式。
  * @param  p：[输出]至少两字节可写区。
  * @param  value：[输入]要编码的无符号值。
  * @retval 无。
  * @note   只覆盖目标的两个字节。
  */
static inline void sfile_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;           /* 先写低八位。 */
    p[1] = (uint8_t)(value >> 8);    /* 高八位放在较高地址。 */
}

/**
  * @brief  将 32 位值写成小端线格式。
  * @param  p：[输出]至少四字节可写区。
  * @param  value：[输入]要编码的无符号值。
  * @retval 无。
  * @note   由两次小端 16 位写入组成。
  */
static inline void sfile_put32(uint8_t *p, uint32_t value)
{
    sfile_put16(p, (uint16_t)value);                /* 低半字占前两字节。 */
    sfile_put16(p + 2, (uint16_t)(value >> 16));    /* 高半字占后两字节。 */
}

/* 16 字节头依次存方向、版本、命令、保留字节、传输号、索引、请求号和载荷长度。块尾 CRC 仅覆盖头及有效载荷。 */
/**
  * @brief  封装文件传输块的地址、请求标识、载荷长度与 CRC16。
  * @param  p：[输入输出]512 字节块，载荷已备好。
  * @param  direction：[输入]主机或从机方向标识。
  * @param  command：[输入]本块协议命令。
  * @param  tid：[输入]传输标识。
  * @param  index：[输入]数据块号或控制命令约定的索引。
  * @param  request：[输入]用于匹配控制 ACK 的请求号。
  * @param  len：[输入]已填的有效载荷长度，0～480 字节。
  * @retval 无。
  * @note   调用前把有效载荷写在偏移 16 处。CRC 放在块尾，只覆盖头和有效载荷。
  */
static inline void sfile_frame(uint8_t *p, uint8_t direction, uint8_t command,
                               uint32_t tid, uint32_t index, uint16_t request,
                               uint16_t len)
{
    p[0] = direction;                                                      /* 方向标志区分主机请求与从机回传。 */
    p[1] = SFILE_VERSION;                                                  /* 供接收端拒绝不兼容的线布局。 */
    p[2] = command;                                                        /* 命令决定 index 与 payload 的解释方式。 */
    p[3] = 0;                                                              /* 预留字节固定写零。 */
    sfile_put32(p + 4, tid);                                               /* 写本次传输的 32 位标识。 */
    sfile_put32(p + 8, index);                                             /* 数据块号或控制操作约定的索引。 */
    sfile_put16(p + 12, request);                                          /* 写本请求号以排除旧 ACK。 */
    sfile_put16(p + 14, len);                                              /* 有效载荷的实际字节数，尾块可不足 480 字节。 */
    sfile_put16(p + SFILE_CRC_OFFSET, swap_crc(p, SFILE_HEADER + len));    /* 将头及有效载荷的 CRC16 固定写在整个块的最后两个字节。 */
}

/**
  * @brief  验证文件块的方向、版本、保留位、长度与 CRC。
  * @param  p：[输入]可读完整 512 字节块。
  * @param  direction：[输入]预期对端方向。
  * @retval 非零表示这些字段全部通过，0 表示无效。
  * @note   本层只验证帧格式；调用方还须匹配命令和请求身份。
  */
static inline int sfile_valid(const uint8_t *p, uint8_t direction)
{
    uint16_t len = swap_u16(p + 14);                                          /* 提取对端报告的长度，验证上限后才用它读载荷。 */

    /* 先约束方向、版本、保留字节和声明长度，再按该长度复算校验。 */
    return p[0] == direction && p[1] == SFILE_VERSION && !p[3] &&
        len <= SFILE_DATA_BYTES &&
        swap_u16(p + SFILE_CRC_OFFSET) == swap_crc(p, SFILE_HEADER + len);    /* 用收到的帧头和实际载荷复算 CRC。 */
}

/**
  * @brief  计算从指定块号开始的本组实际块数。
  * @param  blocks：[输入]文件的总数据块数。
  * @param  first：[输入]本组首块的零基块号。
  * @retval 剩余块数与 SFILE_GROUP 的较小值。
  * @note   调用方保证 first 不大于 blocks，最后一组可能不足 100 块。
  */
static inline unsigned int sfile_group_count(uint32_t blocks, uint32_t first)
{
    uint32_t remaining = blocks - first;                                        /* 记录从本组起点到文件末尾的剩余块数。 */
    return remaining >= SFILE_GROUP ? SFILE_GROUP : (unsigned int)remaining;    /* 满组以 100 块确认，最后一组只包括文件中确有的块。 */
}

/**
  * @brief  查询分组确认位图中的一个槽位。
  * @param  bitmap：[输入]包含目标位的有效位图。
  * @param  i：[输入]本组内的零基块号。
  * @retval 1 该位已设置，0 该位未设置。
  * @note   组内槽位 i 由第 i/8 字节的低位起排列。
  */
static inline int sfile_bit(const uint8_t *bitmap, unsigned int i)
{
    return (bitmap[i / 8u] >> (i % 8u)) & 1u;    /* 先定位字节，再取由低至高对应的状态位。 */
}

/**
  * @brief  验证可传给从机用于创建文件的名称字节。
  * @param  name：[输入]至少 len 个名称字节。
  * @param  len：[输入]有效名称字节数，1～31。
  * @retval 1 合法，0 为长度、首尾或字符错误。
  * @note   允许字母数字、点、下划线和连字符；首尾点及路径分隔符均不通过。
  */
static inline int sfile_name_valid(const uint8_t *name, unsigned int len)
{
    unsigned int i;                                                                          /* 遍历名称原始字节，无需输入末尾带零。 */
    if (!len || len > SFILE_NAME_MAX || name[0] == '.' || name[len - 1] == '.') return 0;    /* 禁止空名、过长名和首尾点，避免非法存储目标。 */
    for (i = 0; i < len; ++i) {                                                              /* 逐个检查所有字符，排除路径分隔符和控制字符。 */
        uint8_t c = name[i];                                                                 /* 按无符号八位比较，非 ASCII 不会误当字母或数字。 */
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-') return 0;           /* 普通字母数字之外只允许点、下划线和连字符。 */
    }
    return 1;                                                                                /* 文件名全部通过白名单。 */
}

/**
  * @brief  用半字节查表续算文件内容的反射 CRC32。
  * @param  crc：[输入]上次的原始累加状态。
  * @param  p：[输入]至少 len 个源字节。
  * @param  len：[输入]本次参与校验的字节数。
  * @retval 续算后的原始 CRC 状态。
  * @note   首块用 UINT32_MAX；调用方在最终 FINISH 前异或 UINT32_MAX，补块不重复累计。
  */
static inline uint32_t sfile_crc32(uint32_t crc, const uint8_t *p, unsigned int len)
{
    /* 每次处理四位，用 16 项反射多项式表节省查表空间。 */
    static const uint32_t table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    while (len--) {                             /* 每次从输入消费一个字节，直到指定长度耗尽。 */

        /* 每个字节与累积值低位混合，再依次处理两个半字节。 */
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 15u];    /* 先用低四位校正反射 CRC。 */
        crc = (crc >> 4) ^ table[crc & 15u];    /* 再处理另外四位，完成当前输入字节。 */
    }
    return crc;                                 /* 交出未作最终异或的状态，下一段可直接接续。 */
}

#endif
