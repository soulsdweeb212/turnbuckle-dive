/* Turns loaded resources into the PC's byte order, by kind (docs/GAME_CODE.md, "Resources in the PC's byte order").
 *
 * Resources come from the ROM in the N64's byte order, most significant byte first. Where the game's code reads a
 * resource's 16-, 32- or 64-bit values as plain C integers, those values must be in the PC's order, least significant
 * byte first, so port_res_loaded (src/port.c) reverses the bytes of each such value once, as the resource arrives. The
 * other kinds stay as they are: the meshes, textures and palettes, which the game's display-list code and LibUltraShip's
 * renderer read in the N64's order, and every kind not converted yet. tools/res_digest.py makes the same conversion,
 * so the rom_loader test's digest covers it.
 *
 * The kinds converted so far are the three resources of a character, which loadPlayerResourceSet loads for each
 * wrestler. They sit in the resource table as triples: the profile at 0x140 + 3n (n = 0 to 68), its move table at the
 * next id, its animation list at the one after; animation list 0x13F comes before the first profile.
 * - Profile, 0x40 bytes (the wrestler's +0x364): bytes up to +0x23; five 16-bit animation ids at +0x24
 *   (requestAttachmentAction reads two of them); five 16-bit character ids at +0x2E, the wrestlers that can run in
 *   (func_800BFCF0); a 64-bit set of ability bits at +0x38.
 * - Move table: entries of six 32-bit words, read only as 32-bit words (unpackAnimationParameters).
 * - Animation list: 16-bit animation ids (D_8007C5B8, one row per player).
 * And one more move table: resource 0x126, the moves that every wrestler shares, which resetMatchState loads to
 * D_8007BD64 + 0x14 and func_800A7D1C reads (32-bit words). Resources 0x127 to 0x12A, loaded next to it, are read as
 * bytes (func_800A8D14) and stay as they are. */
#include <stdint.h>

#include "game.h"
#include "dev/order_check.h"

#define SHARED_MOVES 0x126u
#define CHAR_FIRST 0x13Fu /* the first animation list */
#define CHAR_LAST 0x20Eu  /* the last animation list */

static void swap(uint8_t *p, unsigned width) {
    unsigned i;

    for (i = 0; i < width / 2; i++) {
        uint8_t t = p[i];
        p[i] = p[width - 1 - i];
        p[width - 1 - i] = t;
    }
    ORDER_CHECK_VALUE(p, width);
}

static void swap_all(uint8_t *buf, int32_t size, unsigned width, uint32_t id, const char *kind) {
    int32_t i;

    if (size % (int32_t)width != 0) {
        port_fatal("resource 0x%X (a %s) has %d bytes, not a whole number of %u-byte values", id, kind, (int)size,
                   width);
    }
    for (i = 0; i < size; i += (int32_t)width) {
        swap(buf + i, width);
    }
}

void res_convert(uint32_t id, uint8_t *buf, int32_t size) {
    unsigned i;

    if (id == SHARED_MOVES) {
        swap_all(buf, size, 4, id, "move table");
        return;
    }
    if (id < CHAR_FIRST || id > CHAR_LAST) {
        return;
    }
    switch ((id - CHAR_FIRST) % 3) {
    case 0: /* an animation list */
        swap_all(buf, size, 2, id, "character's animation list");
        break;
    case 1: /* a profile */
        if (size != 0x40) {
            port_fatal("resource 0x%X (a character's profile) has %d bytes, not 64", id, (int)size);
        }
        for (i = 0x24; i < 0x38; i += 2) {
            swap(buf + i, 2);
        }
        swap(buf + 0x38, 8);
        break;
    default: /* a move table */
        swap_all(buf, size, 4, id, "character's move table");
        break;
    }
}
