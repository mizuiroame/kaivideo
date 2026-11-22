#include "md4.h"

#include <string.h>

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void block(uint32_t state[4], const uint8_t chunk[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
        x[i] = le32(chunk + 4 * i);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

    static const int R1[4] = {3, 7, 11, 19};
    for (int i = 0; i < 16; i++) {
        uint32_t t = a + F(b, c, d) + x[i];
        a = d, d = c, c = b, b = ROTL(t, R1[i % 4]);
    }
    static const int R2[4] = {3, 5, 9, 13}, ORDER2[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
    for (int i = 0; i < 16; i++) {
        uint32_t t = a + G(b, c, d) + x[ORDER2[i]] + 0x5A827999u;
        a = d, d = c, c = b, b = ROTL(t, R2[i % 4]);
    }
    static const int R3[4] = {3, 9, 11, 15}, ORDER3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    for (int i = 0; i < 16; i++) {
        uint32_t t = a + H(b, c, d) + x[ORDER3[i]] + 0x6ED9EBA1u;
        a = d, d = c, c = b, b = ROTL(t, R3[i % 4]);
    }
    state[0] += a, state[1] += b, state[2] += c, state[3] += d;
    memset(x, 0, sizeof(x));
}

void md4(const void *data, size_t size, uint8_t out[MD4_DIGEST_SIZE])
{
    uint32_t state[4] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u};
    const uint8_t *p = data;
    size_t left = size;
    for (; left >= 64; p += 64, left -= 64)
        block(state, p);
    // The rest, a 1 bit, zeros, and the length in bits: one or two blocks.
    uint8_t tail[128] = {0};
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t tail_size = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    for (int i = 0; i < 8; i++)
        tail[tail_size - 8 + i] = (uint8_t)(bits >> (8 * i));
    block(state, tail);
    if (tail_size == 128)
        block(state, tail + 64);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            out[4 * i + k] = (uint8_t)(state[i] >> (8 * k));
    memset(tail, 0, sizeof(tail));
    memset(state, 0, sizeof(state));
}
