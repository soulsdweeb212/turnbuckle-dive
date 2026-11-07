/* Known-answer tests for the SHA-1 code (FIPS 180-4 examples). */
#include <stdio.h>
#include <string.h>

#include "sha1.h"

static int check(const char *label, const char *got, const char *expected) {
    if (strcmp(got, expected) != 0) {
        printf("FAIL %s: got %s, expected %s\n", label, got, expected);
        return 1;
    }
    return 0;
}

int main(void) {
    static const char *const msg448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const char hex[] = "0123456789abcdef";
    char out[41];
    char block[1000];
    uint8_t digest[20];
    Sha1 s;
    int failures = 0;
    int i;

    sha1_hex("", 0, out);
    failures += check("empty", out, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha1_hex("abc", 3, out);
    failures += check("abc", out, "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha1_hex(msg448, strlen(msg448), out);
    failures += check("448 bits", out, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    /* One million 'a', fed in pieces to exercise the block buffering. */
    memset(block, 'a', sizeof(block));
    sha1_init(&s);
    for (i = 0; i < 1000; i++) {
        sha1_update(&s, block, sizeof(block));
    }
    sha1_final(&s, digest);
    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
    failures += check("million a", out, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    if (failures == 0) {
        printf("sha1: all 4 tests passed\n");
    }
    return failures != 0;
}
