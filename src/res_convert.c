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
 * wrestler. They sit in the resource table as triples: the profile at 0x12B + 3n (n = 0 to 75), its move table at the
 * next id, its animation list at the one after, up to 0x20E. The game's character table D_80038A64 (resource ids by
 * character group) names all 76 profiles, from 0x12B to 0x20C.
 * - Profile, 0x40 bytes (the wrestler's +0x364): bytes up to +0x23; five 16-bit animation ids at +0x24
 *   (requestAttachmentAction reads two of them); five 16-bit character ids at +0x2E, the wrestlers that can run in
 *   (func_800BFCF0); a 64-bit set of ability bits at +0x38.
 * - Move table: entries of six 32-bit words, read only as 32-bit words (unpackAnimationParameters).
 * - Animation list: 16-bit animation ids (D_8007C5B8, one row per player).
 * And one more move table: resource 0x126, the moves that every wrestler shares, which resetMatchState loads to
 * D_8007BD64 + 0x14 and func_800A7D1C reads (32-bit words). Resources 0x127 to 0x12A, loaded next to it, are read as
 * bytes (func_800A8D14) and stay as they are.
 *
 * And the menus' pictures, by their content rather than their id. A picture starts with a 16-byte header: "TEX\0", the
 * width (+4) and height (+6) as 16-bit values, the bits per texel (+8, a byte: 4 or 8), a byte (+9), the palette's
 * colour count (+0xA, 16-bit: 16 or 256) and four zero bytes; the palette and the texels follow, and stay as they are.
 * The menus read the three 16-bit values as plain integers (func_80097E1C the size, func_800B6F88 the colour count,
 * and some thirty more), so those three are converted. A picture is a resource of its own (0x3 to 0x125), or an entry
 * of a pack (a big-endian count, then that many big-endian offsets from the pack's start): the pack's count and
 * offsets stay in the N64's order (func_800B64EC reads them so), and each entry that is a picture has its header
 * converted. In the ROM every "TEX\0" inside a resource is at a pack's entry. */
#include <stdint.h>

#include "game.h"
#include "dev/order_check.h"

#define SHARED_MOVES 0x126u
#define CHAR_FIRST 0x12Bu /* the first profile */
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

/* 1 when the `room` bytes at p start with a picture's header (above). */
static int is_picture(const uint8_t *p, int64_t room) {
    unsigned width, height, colours;

    if (room < 16 || p[0] != 'T' || p[1] != 'E' || p[2] != 'X' || p[3] != 0) {
        return 0;
    }
    width = (unsigned)p[4] << 8 | p[5];
    height = (unsigned)p[6] << 8 | p[7];
    colours = (unsigned)p[10] << 8 | p[11];
    return width != 0 && height != 0 && (p[8] == 4 || p[8] == 8) && (colours == 16 || colours == 256);
}

static void convert_picture(uint8_t *p) {
    swap(p + 4, 2);
    swap(p + 6, 2);
    swap(p + 0xA, 2);
}

/* The pictures among a pack's entries; a resource that is not a pack (its count and offsets do not fit in it) has
 * none. */
static void convert_pack_pictures(uint8_t *buf, int32_t size) {
    uint32_t n, i;

    if (size < 8) {
        return;
    }
    n = (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 | (uint32_t)buf[2] << 8 | buf[3];
    if (n == 0 || n > (uint32_t)(size - 4) / 4) {
        return;
    }
    for (i = 0; i < n; i++) {
        const uint8_t *o = buf + 4 + 4 * i;
        int32_t at = (int32_t)((uint32_t)o[0] << 24 | (uint32_t)o[1] << 16 | (uint32_t)o[2] << 8 | o[3]);

        if (at < (int32_t)(4 + 4 * n) || at >= size) {
            return;
        }
    }
    for (i = 0; i < n; i++) {
        const uint8_t *o = buf + 4 + 4 * i;
        int32_t at = (int32_t)((uint32_t)o[0] << 24 | (uint32_t)o[1] << 16 | (uint32_t)o[2] << 8 | o[3]);

        if (is_picture(buf + at, (int64_t)size - at)) {
            convert_picture(buf + at);
        }
    }
}

void res_convert(uint32_t id, uint8_t *buf, int32_t size) {
    unsigned i;

    if (is_picture(buf, size)) {
        convert_picture(buf);
        return;
    }
    convert_pack_pictures(buf, size);
    if (id == SHARED_MOVES) {
        swap_all(buf, size, 4, id, "move table");
        return;
    }
    if (id < CHAR_FIRST || id > CHAR_LAST) {
        return;
    }
    switch ((id - CHAR_FIRST) % 3) {
    case 2: /* an animation list */
        swap_all(buf, size, 2, id, "character's animation list");
        break;
    case 0: /* a profile */
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
