/* SPI FILE V1, shared wire layout. All multibyte fields are little endian. */
#ifndef SPI_LINK_FILE_PROTOCOL_H
#define SPI_LINK_FILE_PROTOCOL_H

#define SFILE_MASTER 0xacu
#define SFILE_SLAVE 0xcau
#define SFILE_VERSION 1u
#define SFILE_HEADER 16u
#define SFILE_DATA_BYTES 480u
#define SFILE_GROUP 100u
#define SFILE_DELIVERY_MAX_HOLD_MS 15000u
#define SFILE_BITMAP 13u
#define SFILE_ACK_BYTES (2u + SFILE_BITMAP)
#define SFILE_HTTPS_SEED_BYTES 48u
#define SFILE_HTTPS_START_FIXED (4u + SFILE_HTTPS_SEED_BYTES)
#define SFILE_HTTPS_URL_MAX (SFILE_DATA_BYTES - SFILE_HTTPS_START_FIXED)
#define SFILE_HTTP_RESULT_HEAD (SFILE_ACK_BYTES + 2u)
#define SFILE_HTTP_RESPONSE_MAX 192u
#define SFILE_HTTP_META_MAX 3072u
#define SFILE_HTTP_BODY_MAX 16384u
#define SFILE_HTTP_PAGE_BYTES 400u
#define SFILE_NAME_MAX 31u
#define SFILE_MAX_SIZE 0x7fffffffu
#define SFILE_CRC_OFFSET (SWAP_BLOCK_SIZE - 2u)
#define SFILE_PAIR_WITNESS_OFFSET (SWAP_BLOCK_SIZE - 8u)
#define SFILE_PAIR_WITNESS 0xa35ce719u

enum sfile_command {
    SFILE_IDLE, SFILE_START, SFILE_DATA, SFILE_GROUP_END,
    SFILE_QUERY, SFILE_FINISH, SFILE_ABORT, SFILE_ACK, SFILE_START_RAM, SFILE_START_PAIR, SFILE_START_DELIVERY, SFILE_START_STREAM, SFILE_START_HTTPS, SFILE_START_HTTP, SFILE_HTTP_META, SFILE_HTTP_READ
};
enum sfile_status {
    SFILE_OK, SFILE_MISSING, SFILE_BUSY, SFILE_INVALID,
    SFILE_STORAGE, SFILE_CHECKSUM, SFILE_EXISTS, SFILE_SEND_FAILED, SFILE_HTTP_FAILED
};

static inline uint32_t sfile_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void sfile_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void sfile_put32(uint8_t *p, uint32_t value)
{
    sfile_put16(p, (uint16_t)value);
    sfile_put16(p + 2, (uint16_t)(value >> 16));
}

/* Header: direction, version, command, reserved, transfer ID, index,
 * request ID, payload length. Only header and live payload are checksummed. */
static inline void sfile_frame(uint8_t *p, uint8_t direction, uint8_t command,
                               uint32_t tid, uint32_t index, uint16_t request,
                               uint16_t len)
{
    p[0] = direction;
    p[1] = SFILE_VERSION;
    p[2] = command;
    p[3] = 0;
    sfile_put32(p + 4, tid);
    sfile_put32(p + 8, index);
    sfile_put16(p + 12, request);
    sfile_put16(p + 14, len);
    sfile_put16(p + SFILE_CRC_OFFSET, swap_crc(p, SFILE_HEADER + len));
}

static inline int sfile_valid(const uint8_t *p, uint8_t direction)
{
    uint16_t len = swap_u16(p + 14);
    return p[0] == direction && p[1] == SFILE_VERSION && !p[3] &&
        len <= SFILE_DATA_BYTES &&
        swap_u16(p + SFILE_CRC_OFFSET) == swap_crc(p, SFILE_HEADER + len);
}

static inline unsigned int sfile_group_count(uint32_t blocks, uint32_t first)
{
    uint32_t remaining = blocks - first;
    return remaining >= SFILE_GROUP ? SFILE_GROUP : (unsigned int)remaining;
}

static inline int sfile_bit(const uint8_t *bitmap, unsigned int i)
{
    return (bitmap[i / 8u] >> (i % 8u)) & 1u;
}

static inline int sfile_name_valid(const uint8_t *name, unsigned int len)
{
    unsigned int i;
    if (!len || len > SFILE_NAME_MAX || name[0] == '.' || name[len - 1] == '.') return 0;
    for (i = 0; i < len; ++i) {
        uint8_t c = name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-') return 0;
    }
    return 1;
}

static inline uint32_t sfile_crc32(uint32_t crc, const uint8_t *p, unsigned int len)
{
    static const uint32_t table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 15u];
        crc = (crc >> 4) ^ table[crc & 15u];
    }
    return crc;
}

#endif
