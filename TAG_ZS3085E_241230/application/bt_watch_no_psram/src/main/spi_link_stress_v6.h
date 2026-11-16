/* SPI stress-v6 公共线协议；ATS3085E 与 UMA503 保持相同的字节布局。
 * 每帧固定 16 字节，多字节字段按低字节在前编码：
 * 0：方向标识；1：DATA/END；2～3：会话号；4～5：序号；
 * 6～7：从机接收错误数；8～9：目标帧数；10～11：主机序号缺口；
 * 12～13：随帧变化的测试图案；14～15：前 14 字节的 CRC16/CCITT-FALSE。
 * SPI 在同一组时钟中同时收发，从机预装的 DATA 无法包含本次接收结果。
 * 因此最后一次 DATA 校验后另发 END，其序号字段表示已完成的 DATA 次数，
 * 错误字段包含最后一次接收结果。END 允许重发，接收端必须去重。
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
  * @brief  从两个独立字节解析小端字段，不依赖 CPU 对齐和本机端序。
  * @param  p：[输入]至少 2 字节的可读区域，不可为空。
  * @retval 解析后的无符号 16 位值。
  * @note   不访问硬件、不保存指针。
  */
static inline uint16_t link_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);    /* 合并低字节和高字节，避免依赖 CPU 本机端序。 */
}

/**
  * @brief  把 16 位值拆成低字节和高字节写入帧。
  * @param  p：[输出]至少 2 字节的可写区域，不可为空。
  * @param  n：[输入]待编码值，范围 0～65535。
  * @retval 无。
  * @note   缓冲区由调用者持有，函数不分配内存。
  */
static inline void link_put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;           /* 先写入字段低 8 位。 */
    p[1] = (uint8_t)(n >> 8);    /* 再写入字段高 8 位。 */
}

/**
  * @brief  计算无反射的 CRC16/CCITT-FALSE，检测传输内容损坏。
  * @param  p：[输入]至少 n 字节的可读数据；n 为 0 时允许为空。
  * @param  n：[输入]校验长度，单位为字节。
  * @retval 16 位 CRC；初值 0xFFFF，多项式 0x1021，末异或为 0。
  * @note   逐字节按最高位在先处理；标准向量 123456789 的结果为 0x29B1。
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
  * @brief  生成可由对端复算的变化图案，辅助识别旧数据或固定电平。
  * @param  magic：[输入]方向标识，主机 0xA6、从机 0xB6。
  * @param  sid：[输入]16 位会话号，主机填 0。
  * @param  seq：[输入]当前 DATA 序号或 END 完成次数。
  * @retval 写入字节 12～13 的 16 位测试图案。
  * @note   算术结果显式截断到 16 位，不承载业务数据。
  */
static inline uint16_t link_payload(uint8_t magic, uint16_t sid, uint16_t seq)
{
    return (uint16_t)((seq * 40503u + sid * 97u + magic * 257u) ^ 0xa55au);    /* 混合序号、会话和方向，生成可复算且随帧变化的 16 位图案。 */
}

/**
  * @brief  按字段编码完整测试帧，最后计算并填写 CRC。
  * @param  p：[输出]至少 LINK_FRAME_SIZE（16）字节的可写区域，不可为空。
  * @param  magic：[输入]主机 0xA6 或从机 0xB6。
  * @param  type：[输入]LINK_DATA 或 LINK_END。
  * @param  sid：[输入]从机会话号；主机填 0。
  * @param  seq：[输入]DATA 序号；END 中为已完成 DATA 的次数。
  * @param  bad：[输入]从机累计接收或传输错误次数；主机填 0。
  * @param  target：[输入]从机目标 DATA 次数，1～1000；主机填 0。
  * @param  gaps：[输入]从机观察到的主机序号缺口次数；主机填 0。
  * @retval 无。
  * @note   只编码字段，不检查取值；调用者负责会话和计数范围。
  */
static inline void link_frame(uint8_t *p, uint8_t magic, uint8_t type,
                              uint16_t sid, uint16_t seq, uint16_t bad,
                              uint16_t target, uint16_t gaps)
{
    p[0] = magic;                                         /* 写入主机/从机方向标识。 */
    p[1] = type;                                          /* 写入 DATA 或 END 帧类型。 */
    link_put16(p + 2, sid);                               /* 将会话号编码到字节 2～3。 */
    link_put16(p + 4, seq);                               /* 将序号或 END 完成数编码到字节 4～5。 */
    link_put16(p + 6, bad);                               /* 将从机错误数编码到字节 6～7。 */
    link_put16(p + 8, target);                            /* 将目标数据帧数量编码到字节 8～9。 */
    link_put16(p + 10, gaps);                             /* 将主机序号缺口编码到字节 10～11。 */
    link_put16(p + 12, link_payload(magic, sid, seq));    /* 填充可复算的变化图案到字节 12～13。 */
    link_put16(p + 14, link_crc16(p, 14));                /* 对前 14 字节计算 CRC，按小端写到末尾两字节。 */
}
/* 先验证 CRC，再使用数量、会话和序号字段，避免坏帧污染统计。 */

/**
  * @brief  依次验证 CRC、方向、帧类型和变化图案。
  * @param  p：[输入]至少 16 字节的完整接收帧，不可为空。
  * @param  magic：[输入]期望的对端方向标识。
  * @retval 0：校验通过；-1：CRC 不符；-2：方向或帧类型不符；-3：测试图案不符。
  * @note   函数不接收长度参数；调用者必须先保证完整帧，并另查会话范围和序号连续性。
  */
static inline int link_check(const uint8_t *p, uint8_t magic)
{
    if (link_u16(p + 14) != link_crc16(p, 14)) return -1;                                        /* 先验证 CRC，失败时不信任其余字段。 */
    if (p[0] != magic || (p[1] != LINK_DATA && p[1] != LINK_END)) return -2;                     /* 拒绝错误方向或未知帧类型。 */
    if (link_u16(p + 12) != link_payload(magic, link_u16(p + 2), link_u16(p + 4))) return -3;    /* 重新计算测试图案，确认内容符合当前会话和序号。 */
    return 0;                                                                                    /* CRC、方向、帧类型和变化图案均符合协议；连续性由调用者检查。 */
}
#endif    /* 结束当前条件编译分支。 */
