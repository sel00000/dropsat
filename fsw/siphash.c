#include "siphash.h"

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND                                                   \
    do {                                                           \
        v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32);  \
        v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2;                     \
        v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0;                     \
        v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32);  \
    } while (0)

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

uint64_t siphash24(const uint8_t key[16], const uint8_t *msg, size_t len)
{
    const uint64_t k0 = rd64(key);
    const uint64_t k1 = rd64(key + 8);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

    const size_t blocks = len / 8u;
    for (size_t i = 0; i < blocks; i++) {
        const uint64_t m = rd64(msg + 8u * i);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    /* 마지막 블록: 남은 바이트 + 상위 바이트에 길이 */
    uint64_t b = ((uint64_t)len) << 56;
    const uint8_t *tail = msg + 8u * blocks;
    for (size_t i = 0; i < (len & 7u); i++) {
        b |= ((uint64_t)tail[i]) << (8u * i);
    }
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    v2 ^= 0xffu;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}
