/* Game data that the compiled game files use, defined by hand until a data generator writes the game's data as C
 * (docs/DECISIONS.md, D3); that generator then replaces this file. Each definition has the name the game files use and
 * the type of the file that fills it or reads it. They start as zeros, like the N64's BSS. The repository holds none of
 * the game's values: the ones the game keeps in its ROM are copied from the user's ROM when the game's code starts
 * (game_data_init, below). */
#include <string.h>

#include "game.h"

/* The resource offset table, copied unchanged from ROM by initializeAssetSizeTable: 0x77A8 bytes of big-endian words
 * (lookupResourceDescriptor and func_80003684 declare it as s32 *, the same size). */
uint32_t *D_800482E0;

/* Each resource's size once loaded, in the PC's byte order, filled by initializeAssetSizeTable: 0x77A4 bytes, one
 * word per resource id from 1 to GAME_RES_COUNT. */
uint32_t *D_800482E4;

/* The grayscale switch: when it is 1, func_80004E08 turns each palette it loads into grey levels
 * (convertRgba16ToGrayscale). The menus set it. */
uint16_t D_800307A0;

/* func_80004E08 multiplies a palette's size in bytes by this to count its entries for convertRgba16ToGrayscale. The
 * game files declare it const; it is filled at run time, so here it is not. */
double D_8003B110;

/* convertRgba16ToGrayscale's weights for green and blue, and the factor that turns the sum of red, green and blue into
 * one grey level. */
double D_8003B0F0;
double D_8003B100;
double D_8003B108;

/* func_80096D8C's scale for a picture's height: the height it draws a picture at is this times the height it is asked
 * for, divided by the picture's own height. With 0 every picture it draws would be 0 pixels high. */
double D_800D6070;

/* Where each value above is in the ROM, as an offset into the ROM image, and how big it is. The N64 copies the main
 * segment from ROM 0x1050 to 0x80000450 (so a main address minus 0x7FFFF400 is its ROM offset), and overlay 1's data
 * from ROM 0x72240 to 0x800C5AD0 (the decompilation's build.json lists both). Values are big-endian in the ROM. */
typedef struct {
    void *value;
    uint32_t rom;
    uint32_t size; /* 2: a 16-bit integer; 8: a double */
} RomValue;

static const RomValue kRomValues[] = {
    { &D_800307A0, 0x800307A0u - 0x7FFFF400u, 2 },
    { &D_8003B110, 0x8003B110u - 0x7FFFF400u, 8 },
    { &D_8003B0F0, 0x8003B0F0u - 0x7FFFF400u, 8 },
    { &D_8003B100, 0x8003B100u - 0x7FFFF400u, 8 },
    { &D_8003B108, 0x8003B108u - 0x7FFFF400u, 8 },
    { &D_800D6070, 0x72240u + (0x800D6070u - 0x800C5AD0u), 8 },
};

void game_data_init(void) {
    size_t i;

    for (i = 0; i < sizeof(kRomValues) / sizeof(kRomValues[0]); i++) {
        uint8_t bytes[8];
        uint64_t word = 0;
        uint32_t j;

        port_rom_read(bytes, kRomValues[i].rom, kRomValues[i].size);
        for (j = 0; j < kRomValues[i].size; j++) {
            word = (word << 8) | bytes[j];
        }
        if (kRomValues[i].size == 2) {
            *(uint16_t *)kRomValues[i].value = (uint16_t)word;
        } else {
            /* The 64 bits are the double's IEEE 754 bits, the same format on the N64 and the PC. */
            memcpy(kRomValues[i].value, &word, sizeof(word));
        }
    }
}
