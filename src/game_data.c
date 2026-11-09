/* Game data that the compiled game files use, defined by hand until a data generator writes the game's data as C
 * (docs/DECISIONS.md, D3); that generator then replaces this file. Each definition has the name the game files use and
 * the type of the file that fills it. They start as zeros, like the N64's BSS. */
#include "game.h"

/* The resource offset table, copied unchanged from ROM by initializeAssetSizeTable: 0x77A8 bytes of big-endian words
 * (lookupResourceDescriptor and func_80003684 declare it as s32 *, the same size). */
uint32_t *D_800482E0;

/* Each resource's size once loaded, in the PC's byte order, filled by initializeAssetSizeTable: 0x77A4 bytes, one
 * word per resource id from 1 to GAME_RES_COUNT. */
uint32_t *D_800482E4;
