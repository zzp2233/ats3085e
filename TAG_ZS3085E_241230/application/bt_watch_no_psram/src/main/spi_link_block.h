/* V10 block2 transport: one CS/READY/DMA transaction contains two complete
 * 256-byte v3 logical frames. The original CRC and frame format are unchanged.
 * Block b carries logical sequences 2*b and 2*b+1. Both must validate before
 * either is accepted. The next block echoes the last accepted block's pair.
 * Boot reply: sid=0, sequences 65534/65535 (block index 32767).
 * This transport requires a 512-byte slave; never mix it with V9A firmware.
 */
#ifndef SPI_LINK_BLOCK_H_
#define SPI_LINK_BLOCK_H_
#include "spi_link_swap.h"
#define SWAP_BLOCK_FRAMES 2u
#define SWAP_BLOCK_SIZE (SWAP_SIZE * SWAP_BLOCK_FRAMES)
#define SWAP_BLOCK_PAYLOAD ((SWAP_SIZE - SWAP_HDR - SWAP_TAIL) * SWAP_BLOCK_FRAMES)
#define SWAP_BLOCK_IDLE 32767u

static inline void swap_block_frame(uint8_t *p, uint8_t magic, uint16_t sid, uint16_t block)
{
    unsigned int slot;
    for (slot = 0; slot < SWAP_BLOCK_FRAMES; ++slot)
        swap_frame(p + slot * SWAP_SIZE, magic, sid, (uint16_t)(block * 2u + slot));
}

static inline uint16_t swap_block_sequence(const uint8_t *p)
{
    return (uint16_t)(swap_u16(p + 4) >> 1);
}

/* -1/-2/-3 retain logical CRC/version/pattern errors; -4 is pair mismatch.
 * A good first logical frame never authorizes a partial block acceptance. */
static inline int swap_block_check(const uint8_t *p, uint8_t magic)
{
    unsigned int slot;
    uint16_t first;
    for (slot = 0; slot < SWAP_BLOCK_FRAMES; ++slot) {
        int ret = swap_check(p + slot * SWAP_SIZE, magic);
        if (ret) return ret;
    }
    first = swap_u16(p + 4);
    if ((first & 1u) || swap_u16(p + 2) != swap_u16(p + SWAP_SIZE + 2) ||
        swap_u16(p + SWAP_SIZE + 4) != (uint16_t)(first + 1u)) return -4;
    return 0;
}
#endif
