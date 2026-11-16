/* The splash screen's cube faces (src/splash_faces.h), made from Sting's head textures in the user's ROM.
 *
 * Sting is the wrestler with id 1. His costumes wear three heads, whose records are at 0x800319C0, 0x800319D8 and
 * 0x800319F0 in the game's data (docs/VIEWER.md, "Where a model's texture comes from"). Each record names a texture and
 * a palette for the face (body part 5) and for the hair (body part 20). A face texture is 32 texels wide and 64 high:
 * it wraps around the front of the head. To make a square side of the cube, each face goes in the middle of a 64x64
 * picture, with 16 columns of the same head's hair on each side.
 *
 * A texture starts with an 8-byte header: byte 2 is 1 for 8-bit palette indices (CI8), else they are 4-bit (CI4);
 * bytes 6 and 7 give the width and height as powers of two. A palette is RGBA16 colours, most significant byte first.
 * Both stay in the N64's byte order when they are loaded (src/res_convert.c). */
#include "splash_faces.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resources.h"

typedef struct {
    uint16_t face, face_palette, hair, hair_palette;
    int hair_rows; /* the hair's rows that are hair: the third head's hair texture is blond at the top only */
} SplashHead;

static const SplashHead g_heads[SPLASH_FACES] = {
    { 0xA6A, 0xA68, 0xA64, 0xA62, 64 }, /* record 0x800319C0: the white face paint */
    { 0xA73, 0xA71, 0xA6D, 0xA6B, 64 }, /* record 0x800319D8: the red face paint */
    { 0xA7C, 0xA7A, 0xA76, 0xA74, 16 }, /* record 0x800319F0: the green face paint */
};

/* A texture decoded to palette colours: w x h RGBA16 values. */
typedef struct {
    int w, h;
    uint16_t *texels;
} Decoded;

static int decode(uint16_t id, uint16_t palette_id, int want_w, int want_h, Decoded *out) {
    GameResDesc desc, pdesc;
    uint8_t *tex = res_load(id, &desc);
    uint8_t *pal = res_load(palette_id, &pdesc);
    int ok = 0;

    if (desc.size >= 8 && tex[6] <= 7 && tex[7] <= 7) {
        int ci8 = tex[2] == 1;
        int w = 1 << tex[6], h = 1 << tex[7];
        int entries = ci8 ? 256 : 16;
        int bytes = ci8 ? w * h : w * h / 2;

        if (w == want_w && h == want_h && desc.size >= 8 + bytes && pdesc.size >= entries * 2) {
            out->w = w;
            out->h = h;
            out->texels = malloc((size_t)(w * h) * sizeof(uint16_t));
            if (out->texels != NULL) {
                for (int i = 0; i < w * h; i++) {
                    int index = ci8 ? tex[8 + i] : (i % 2 == 0 ? tex[8 + i / 2] >> 4 : tex[8 + i / 2] & 0xF);
                    out->texels[i] = (uint16_t)(pal[index * 2] << 8 | pal[index * 2 + 1]);
                }
                ok = 1;
            }
        }
    }
    if (!ok) {
        printf("splash: resource 0x%X with palette 0x%X is not the %dx%d texture the splash expects; no splash\n", id,
               palette_id, want_w, want_h);
    }
    free(tex);
    free(pal);
    return ok ? 0 : -1;
}

int splash_faces_load(const Rom *rom, uint8_t out[SPLASH_FACES * SPLASH_FACE_BYTES]) {
    int result = 0;

    rom_attach(rom);
    res_init();
    for (int k = 0; k < SPLASH_FACES && result == 0; k++) {
        const SplashHead *head = &g_heads[k];
        Decoded face = { 0, 0, NULL }, hair = { 0, 0, NULL };

        if (decode(head->face, head->face_palette, 32, 64, &face) != 0 ||
            decode(head->hair, head->hair_palette, 32, 64, &hair) != 0) {
            result = -1;
        } else {
            uint8_t *dst = out + k * SPLASH_FACE_BYTES;

            for (int y = 0; y < SPLASH_FACE_SIZE; y++) {
                for (int x = 0; x < SPLASH_FACE_SIZE; x++) {
                    uint16_t c;

                    if (x >= 16 && x < 48) {
                        c = face.texels[y * 32 + x - 16];
                    } else {
                        /* The hair's columns 0 to 15 on the left, 16 to 31 on the right. */
                        c = hair.texels[(y % head->hair_rows) * 32 + (x < 16 ? x : x - 32)];
                    }
                    c |= 1; /* opaque */
                    dst[(y * SPLASH_FACE_SIZE + x) * 2] = (uint8_t)(c >> 8);
                    dst[(y * SPLASH_FACE_SIZE + x) * 2 + 1] = (uint8_t)(c & 0xFF);
                }
            }
        }
        free(face.texels);
        free(hair.texels);
    }
    /* The developer boot starts the game's variables and tables again itself (dev_boot_run). */
    res_shutdown();
    return result;
}
