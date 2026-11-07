/* SHA-1, used to check that the user's ROM is the supported one. */
#ifndef TD_SHA1_H
#define TD_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[5];
    uint64_t length;   /* bytes hashed so far */
    uint8_t block[64];
    size_t used;       /* bytes waiting in block */
} Sha1;

void sha1_init(Sha1 *s);
void sha1_update(Sha1 *s, const void *data, size_t size);
void sha1_final(Sha1 *s, uint8_t digest[20]);

/* Hashes data in one call and writes the digest as 40 lowercase hex digits plus a terminating zero. */
void sha1_hex(const void *data, size_t size, char out[41]);

#endif
