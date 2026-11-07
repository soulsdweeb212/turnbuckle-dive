/* SHA-1 as specified in FIPS 180-4. */
#include "sha1.h"

#include <string.h>

static uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(Sha1 *s, const uint8_t *p) {
    uint32_t w[80];
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void sha1_init(Sha1 *s) {
    s->h[0] = 0x67452301;
    s->h[1] = 0xEFCDAB89;
    s->h[2] = 0x98BADCFE;
    s->h[3] = 0x10325476;
    s->h[4] = 0xC3D2E1F0;
    s->length = 0;
    s->used = 0;
}

void sha1_update(Sha1 *s, const void *data, size_t size) {
    const uint8_t *p = data;

    s->length += size;
    while (size > 0) {
        size_t n = sizeof(s->block) - s->used;

        if (n > size) {
            n = size;
        }
        memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        size -= n;
        if (s->used == sizeof(s->block)) {
            sha1_block(s, s->block);
            s->used = 0;
        }
    }
}

void sha1_final(Sha1 *s, uint8_t digest[20]) {
    uint64_t bits = s->length * 8;
    int i;

    /* Padding: a 1 bit, zeros up to 56 bytes into a block, then the message length in bits, big-endian. */
    s->block[s->used++] = 0x80;
    if (s->used > 56) {
        memset(s->block + s->used, 0, sizeof(s->block) - s->used);
        sha1_block(s, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (i = 0; i < 8; i++) {
        s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1_block(s, s->block);
    for (i = 0; i < 20; i++) {
        digest[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

void sha1_hex(const void *data, size_t size, char out[41]) {
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[20];
    Sha1 s;
    int i;

    sha1_init(&s);
    sha1_update(&s, data, size);
    sha1_final(&s, digest);
    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
}
