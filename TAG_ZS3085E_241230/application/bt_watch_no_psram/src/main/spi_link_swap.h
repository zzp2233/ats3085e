/* 角色互换测试协议 v3；两端保持字节一致。
 * 一帧 SWAP_SIZE 字节：
 *   [0] 方向、[1] 版本、[2..3] 会话、[4..5] 小端序号、[6..11] 六字节图案、
 *   [12..13] 头部 CRC16（覆盖 [0..11]）、[14..SWAP_SIZE-3] 载荷图案、
 *   [SWAP_SIZE-2..SWAP_SIZE-1] 整帧 CRC16（覆盖前 SWAP_SIZE-2 字节）。
 * V5 保持 v3 线上布局、256 字节帧和原 CRC 定义；提速不改变协议。
 * 旧 PIO 预装限制只适用于 PIO；503 DMA 不使用 SWAP_PRELOAD。
 * 从机在下一次事务回报上一有效请求的会话及序号；启动空闲帧为 sid=0、seq=65535。 */
#ifndef SPI_LINK_SWAP_H_
#define SPI_LINK_SWAP_H_
#include <stdint.h>    /* 使用固定宽度整数，线协议不依赖两端 int 大小。 */
#define SWAP_SIZE 256u        /* 固定帧长；扩帧必须同步修改两端驱动、缓冲区及验证。 */
#define SWAP_HDR 14u          /* 帧头有效字节数，含头部 CRC。 */
#define SWAP_TAIL 2u          /* 帧尾整帧 CRC 字节数。 */
#define SWAP_PRELOAD 14u      /* 从机传输前可一次可靠装入 FIFO 的字节数。 */
#define SWAP_MASTER 0xA7u     /* 请求方向，区别于旧压力测试。 */
#define SWAP_SLAVE 0xB7u      /* 返回方向。 */
#define SWAP_GAP_MS 100u      /* 命令行未给间隔时使用的保守值。 */
#define SWAP_VER 3u           /* 协议版本，方向错和版本错共用 -2 返回码。 */

/**
  * @brief  从线协议读取一个小端 16 位字段。
  * @param  p：[输入]至少两字节可读区域，不可为空。
  * @retval 解码后的无符号整数。
  * @note   按字节访问，不要求地址对齐。
  */
static inline uint16_t swap_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));    /* 高字节提升后移位，避免未对齐宽访问。 */
}

/**
  * @brief  计算 CRC16-CCITT-FALSE，保护帧头及测试数据。
  * @param  p：[输入]至少 n 字节可读缓冲区，不可为空。
  * @param  n：[输入]参与校验的字节数量。
  * @retval 初值 0xffff、多项式 0x1021 的 CRC，无最终异或。
  * @note   16 项半字节查表，不修改输入；与原逐位 CRC 完全兼容。
  */
static inline uint16_t swap_crc(const uint8_t *p, unsigned int n)
{
    static const uint16_t table[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
    };
    uint16_t crc = 0xffff;
    unsigned int i;
    for (i = 0; i < n; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        crc = (uint16_t)((crc << 4) ^ table[crc >> 12]);
        crc = (uint16_t)((crc << 4) ^ table[crc >> 12]);
    }
    return crc;
}

/**
  * @brief  按帧内索引生成确定的测试图案字节。
  * @param  magic：[输入]方向标识；sid/seq：[输入]本帧会话与序号；i：[输入]该字节在帧内的索引。
  * @retval 该索引上应出现的字节值。
  * @note   只依赖帧字段和索引，两端各自算得同一结果，接收端可逐字节复算。
  */
static inline uint8_t swap_pat(uint8_t magic, uint16_t sid, uint16_t seq, unsigned int i)
{
    return (uint8_t)(magic ^ (uint8_t)sid ^ (uint8_t)(sid >> 8) ^
                     (uint8_t)seq ^ (uint8_t)(seq >> 8) ^ (i * 29u));
}

/**
  * @brief  构造一整帧请求或确认，含头部 CRC 与整帧 CRC。
  * @param  p：[输出]至少 SWAP_SIZE 字节可写缓冲区，不可为空。
  * @param  magic：[输入]SWAP_MASTER 或 SWAP_SLAVE。
  * @param  sid：[输入]会话号；0 保留给启动或坏帧后的空闲回复。
  * @param  seq：[输入]本会话序号；空闲回复使用 65535。
  * @retval 无。
  * @note   不分配内存；先铺满图案再写 CRC，保证同一输入得到同一帧内容。
  */
static inline void swap_frame(uint8_t *p, uint8_t magic, uint16_t sid, uint16_t seq)
{
    unsigned int i;    /* 遍历帧内除帧尾 CRC 外的所有字节。 */
    uint16_t crc;      /* 分别保存头部 CRC 与整帧 CRC。 */

    p[0] = magic;                    /* 方向字节。 */
    p[1] = SWAP_VER;                 /* 版本字节，防止旧协议固件混用。 */
    p[2] = (uint8_t)sid;             /* 会话号低字节。 */
    p[3] = (uint8_t)(sid >> 8);      /* 会话号高字节。 */
    p[4] = (uint8_t)seq;             /* 序号低字节。 */
    p[5] = (uint8_t)(seq >> 8);      /* 序号高字节。 */
    for (i = 6; i < SWAP_SIZE - SWAP_TAIL; ++i) {
        p[i] = swap_pat(magic, sid, seq, i);    /* 头部缓存区与载荷区共用同一条图案规则。 */
    }

    /* 头部 CRC 放在第 12、13 字节，只覆盖真正的头字段，便于快速判方向与版本。 */
    crc = swap_crc(p, SWAP_HDR - SWAP_TAIL);
    p[12] = (uint8_t)crc;
    p[13] = (uint8_t)(crc >> 8);

    /* 整帧 CRC 放在最后两字节，覆盖除自身外的所有字节，校验强度随块长自动提高。 */
    crc = swap_crc(p, SWAP_SIZE - SWAP_TAIL);
    p[SWAP_SIZE - 2] = (uint8_t)crc;
    p[SWAP_SIZE - 1] = (uint8_t)(crc >> 8);
}

/**
  * @brief  验证整帧 CRC、头部 CRC、方向、版本及关键索引图案。
  * @param  p：[输入]恰好一帧的可读缓冲区，不可为空。
  * @param  magic：[输入]期望对端的方向标识。
  * @retval 0：有效；-1：CRC 错；-2：方向或版本错；-3：图案错。
  * @note   整帧 CRC 已覆盖全部字节，图案只抽查头部与载荷三点，避免每帧做一遍全量复算。
  */
static inline int swap_check(const uint8_t *p, uint8_t magic)
{
    uint16_t sid, seq;    /* 从帧里取出会话号与序号，供图案复算使用。 */
    unsigned int i;       /* 抽查索引。 */

    if (swap_u16(p + SWAP_SIZE - 2) != swap_crc(p, SWAP_SIZE - SWAP_TAIL)) return -1;    /* 整帧 CRC 先过。 */
    if (swap_u16(p + 12) != swap_crc(p, SWAP_HDR - SWAP_TAIL)) return -1;                /* 头部 CRC 再过。 */
    if (p[0] != magic || p[1] != SWAP_VER) return -2;                                    /* 方向与版本。 */

    sid = swap_u16(p + 2);                                 /* 取会话号。 */
    seq = swap_u16(p + 4);                                 /* 取序号。 */
    for (i = 6; i < 12; ++i) {                             /* 头部图案逐字节复算。 */
        if (p[i] != swap_pat(magic, sid, seq, i)) return -3;
    }
    /* 载荷抽查：首字节、中点、末字节；整帧 CRC 已经把其余字节兜住。 */
    if (p[SWAP_HDR] != swap_pat(magic, sid, seq, SWAP_HDR)) return -3;
    if (p[(SWAP_SIZE - SWAP_TAIL) / 2] != swap_pat(magic, sid, seq, (SWAP_SIZE - SWAP_TAIL) / 2)) return -3;
    if (p[SWAP_SIZE - 3] != swap_pat(magic, sid, seq, SWAP_SIZE - 3)) return -3;
    return 0;
}
#endif    /* 协议头文件保护结束。 */
