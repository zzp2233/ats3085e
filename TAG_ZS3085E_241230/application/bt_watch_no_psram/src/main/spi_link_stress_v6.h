/* SPI stress-v6 wire format, shared verbatim by ATS3085E and UMA503.
 * 16 bytes, little endian:
 * 0: direction magic; 1: DATA/END; 2..3: session; 4..5: sequence;
 * 6..7: slave RX errors; 8..9: target frame count; 10..11: master seq gaps;
 * 12..13: changing test payload; 14..15: CRC16/CCITT-FALSE over bytes 0..13.
 * A slave END carries the number of completed DATA transfers in sequence.
 * It reports the last DATA receive result (SPI is full duplex, not immediate
 * request/response). END can be repeated; receivers must not double count it.
 */
#ifndef SPI_LINK_STRESS_V6_H      /* 头文件包含保护，避免同一编译单元重复定义。 */
#define SPI_LINK_STRESS_V6_H      /* 标记协议头已被包含。 */
#include <stdint.h>               /* 使用固定宽度整数，保证两端字段宽度一致。 */
#define LINK_FRAME_SIZE 16        /* 当前协议固定帧长为 16 字节。 */
#define LINK_MASTER_MAGIC 0xa6    /* 主机发送方向标识 A6。 */
#define LINK_SLAVE_MAGIC 0xb6     /* 从机发送方向标识 B6。 */
#define LINK_DATA 1               /* 普通连续测试数据帧类型。 */
#define LINK_END 2                /* 测试结束及最终统计帧类型。 */
#define LINK_MAX_FRAMES 1000      /* 单次命令最大允许 1000 个数据帧。 */
/**
  * @brief  读取协议中的小端 16 位字段。
  * @param  p 指向至少 2 个可读字节，低字节在前。
  * @retval 解析后的字段值。
  */
static inline uint16_t link_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);    /* 合并低字节和高字节，避免依赖 CPU 本机端序。 */
}
/**
  * @brief  将 16 位字段写为小端字节序。
  * @param  p 指向至少 2 个可写字节。
  * @param  n 待写入的字段值。
  * @retval 无。
  */
static inline void link_put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;                                   /* 先写入字段低 8 位。 */
    p[1] = (uint8_t)(n >> 8);                            /* 再写入字段高 8 位。 */
}
/**
  * @brief  计算 CRC16/CCITT-FALSE 校验值。
  * @param  p 待校验的字节序列，n 非零时不可为 NULL。
  * @param  n 参与校验的字节数。
  * @retval 16 位 CRC；初值 FFFF，多项式 1021，无反射及末异或。
  */
static inline uint16_t link_crc16(const uint8_t *p, unsigned int n)
{
    uint16_t crc = 0xffff;                                                   /* CRC16/CCITT-FALSE 的规定初值。 */
    unsigned int i;                                                          /* 字节/位循环索引。 */
    while (n--) {                                                            /* 逐个处理参与校验的字节。 */
        crc ^= (uint16_t)*p++ << 8;                                          /* 把当前字节异或到 CRC 的高 8 位。 */
        for (i = 0; i < 8; ++i)                                              /* 每个输入字节执行 8 次多项式迭代。 */
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));    /* 最高位为 1 时移位后异或 1021，并截断为 16 位。 */
    }
    return crc;                                                              /* 返回最终 CRC，不做额外异或。 */
}
/**
  * @brief  根据方向、会话和序号生成变化的测试数据。
  * @param  magic 主机 A6 或从机 B6 标识。
  * @param  sid 从机会话号；主机使用 0。
  * @param  seq 当前方向的帧序号。
  * @retval 帧内字节 12～13 的测试值，不是业务载荷。
  */
static inline uint16_t link_payload(uint8_t magic, uint16_t sid, uint16_t seq)
{
    return (uint16_t)((seq * 40503u + sid * 97u + magic * 257u) ^ 0xa55au);       /* 混合序号、会话和方向，生成可复算且随帧变化的 16 位图案。 */
}
/**
  * @brief  构造一帧完整的 16 字节 SPI 测试报文。
  * @param  p 输出缓冲区，至少 LINK_FRAME_SIZE 字节。
  * @param  magic 主机 A6 或从机 B6 方向标识。
  * @param  type LINK_DATA 数据帧或 LINK_END 汇总帧。
  * @param  sid 从机会话号；主机使用 0。
  * @param  seq 数据帧序号；END 表示已完成的数据帧数量。
  * @param  bad 从机累计接收/传输错误数；主机填写 0。
  * @param  target 从机目标帧数 1～1000；主机填写 0。
  * @param  gaps 从机观察到的主机序号缺口；主机填写 0。
  * @retval 无。
  */
static inline void link_frame(uint8_t *p, uint8_t magic, uint8_t type,
                              uint16_t sid, uint16_t seq, uint16_t bad,
                              uint16_t target, uint16_t gaps)
{
    p[0] = magic;                                                          /* 写入主机/从机方向标识。 */
    p[1] = type;                                                           /* 写入 DATA 或 END 帧类型。 */
    link_put16(p + 2, sid);                                                /* 将会话号编码到字节 2～3。 */
    link_put16(p + 4, seq);                                                /* 将序号或 END 完成数编码到字节 4～5。 */
    link_put16(p + 6, bad);                                                /* 将从机错误数编码到字节 6～7。 */
    link_put16(p + 8, target);                                             /* 将目标数据帧数量编码到字节 8～9。 */
    link_put16(p + 10, gaps);                                              /* 将主机序号缺口编码到字节 10～11。 */
    link_put16(p + 12, link_payload(magic, sid, seq));                     /* 填充可复算的变化图案到字节 12～13。 */
    link_put16(p + 14, link_crc16(p, 14));                                 /* 对前 14 字节计算 CRC，按小端写到末尾两字节。 */
}
/* Validate CRC before trusting any count, session or sequence fields. */
/**
  * @brief  检查 CRC、方向、帧类型及变化的测试数据。
  * @param  p 指向完整的 16 字节接收帧。
  * @param  magic 本端期望的对端方向标识。
  * @retval 0：通过；-1：CRC 错；-2：方向/类型错；-3：测试数据错。
  * @note   调用者另外检查帧长度、会话范围及序号连续性。
  */
static inline int link_check(const uint8_t *p, uint8_t magic)
{
    if (link_u16(p + 14) != link_crc16(p, 14)) return -1;                                        /* 先验证 CRC，失败时不信任其余字段。 */
    if (p[0] != magic || (p[1] != LINK_DATA && p[1] != LINK_END)) return -2;                     /* 拒绝错误方向或未知帧类型。 */
    if (link_u16(p + 12) != link_payload(magic, link_u16(p + 2), link_u16(p + 4))) return -3;    /* 重新计算测试图案，确认内容符合当前会话和序号。 */
    return 0;                                                                                    /* 当前函数或检查路径成功完成。 */
}
#endif    /* 结束当前条件编译分支。 */
