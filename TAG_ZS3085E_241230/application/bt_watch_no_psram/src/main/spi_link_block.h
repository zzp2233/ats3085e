/* V10 双帧块：一次片选、READY 授权及 DMA 传输覆盖 512 字节。块 b 内含逻辑帧 2*b 和 2*b+1，下一块回报上一对已接收序号。启动空闲块的会话为 0，逻辑序号为 65534/65535。两端须使用相同物理块长。 */
#ifndef SPI_LINK_BLOCK_H_
#define SPI_LINK_BLOCK_H_
#include "spi_link_swap.h"                                                             /* 引入单个 V3 逻辑帧的格式、编码与校验。 */
#define SWAP_BLOCK_FRAMES 2u                                                           /* 一个物理块固定含两个逻辑槽。 */
#define SWAP_BLOCK_SIZE (SWAP_SIZE * SWAP_BLOCK_FRAMES)                                /* 一笔片选可交换 512 字节。测试路径把它分成两个 v3 逻辑帧，文件路径则在同样大小的物理块内放一份文件协议帧。 */
#define SWAP_BLOCK_PAYLOAD ((SWAP_SIZE - SWAP_HDR - SWAP_TAIL) * SWAP_BLOCK_FRAMES)    /* 剔除两个帧头和两个帧尾，测试载荷合计 480 字节。 */
#define SWAP_BLOCK_IDLE 32767u                                                         /* 启动回包约定的块号，展开为 65534 和 65535。 */

/**
  * @brief  在同一次片选内构造两帧连续序号的 V3 测试数据。
  * @param  p：[输出]至少 SWAP_BLOCK_SIZE 字节的可写区。
  * @param  magic：[输入]该模式的请求或应答方向字节。
  * @param  sid：[输入]两个逻辑帧共用的会话号。
  * @param  block：[输入]块号，逻辑序号为 2*block 和 2*block+1 的低 16 位。
  * @retval 无。
  * @note   一个 512 字节物理块对应两个 256 字节逻辑帧，分别写自己的 CRC。
  */
static inline void swap_block_frame(uint8_t *p, uint8_t magic, uint16_t sid, uint16_t block)
{
    unsigned int slot;                                                                  /* 从首槽至次槽依次写入，索引决定缓冲偏移和逻辑序号。 */
    for (slot = 0; slot < SWAP_BLOCK_FRAMES; ++slot)                                    /* 固定两个槽，各自调用单帧封装函数。 */
        swap_frame(p + slot * SWAP_SIZE, magic, sid, (uint16_t)(block * 2u + slot));    /* 槽地址按 256 字节递增，两个槽取连续的逻辑序号。 */
}

/**
  * @brief  从首槽逻辑序号还原物理块号。
  * @param  p：[输入]至少能读到首帧序号字段的块缓冲。
  * @retval 首帧序号右移一位得到的块号。
  * @note   本函数只提取字段，调用者负责先校验块。
  */
static inline uint16_t swap_block_sequence(const uint8_t *p)
{
    return (uint16_t)(swap_u16(p + 4) >> 1);    /* 首槽使用偶数逻辑序号，除二即可恢复块号。 */
}

/* 两个逻辑帧分别检查 CRC、版本和图案，通过后还需核对配对关系。 */
/**
  * @brief  逐槽验证 CRC 和图案，再验证两帧的会话与顺序。
  * @param  p：[输入]可读 512 字节的完整块。
  * @param  magic：[输入]预期的对端方向字节。
  * @retval 0 有效，-1 为 CRC 错，-2 为方向或版本错，-3 为图案错，-4 为配对错。
  * @note   只有两槽都有效、会话相同、偶数序号后紧跟奇数序号才接受整块。
  */
static inline int swap_block_check(const uint8_t *p, uint8_t magic)
{
    unsigned int slot;
    uint16_t first;                                                          /* 保存首槽序号，待次槽单帧检查通过后比对。 */
    for (slot = 0; slot < SWAP_BLOCK_FRAMES; ++slot) {                       /* 同一块中如有一帧校验失败，整个块都不能接受。 */
        int ret = swap_check(p + slot * SWAP_SIZE, magic);                   /* 两槽分别验证自己的头、整帧 CRC、方向和图案。 */
        if (ret) return ret;                                                 /* 任一槽失败时保留原来的错误分类。 */
    }
    first = swap_u16(p + 4);                                                 /* 先读首槽序号用于检查配对。 */
    if ((first & 1u) || swap_u16(p + 2) != swap_u16(p + SWAP_SIZE + 2) ||
        swap_u16(p + SWAP_SIZE + 4) != (uint16_t)(first + 1u)) return -4;    /* 第二槽必须有同一会话及紧邻的下一序号。 */
    return 0;                                                                /* 两个槽已通过独立 CRC，槽间会话和顺序也通过。 */
}
#endif
