/* The game's variables on PC (docs/GAME_CODE.md, "The game's variables"): the tables that tools/game_vars.py writes
 * into the build folder (game_vars.c, from the decompilation's port/game_vars.tsv), and src/game_vars.c, which fills
 * the variables from the user's ROM with them. C only. */
#ifndef TD_GAME_VARS_H
#define TD_GAME_VARS_H

#include <stdint.h>

/* One 2-, 4- or 8-byte value of a variable's swap pattern: its offset in the pattern, and its width. */
typedef struct {
    uint16_t offset;
    uint8_t width;
} GameVarSwap;

/* A variable that sits in its section's block at its N64 offset. Its swap pattern is the `count` values from
 * game_var_swaps[first]; they repeat every `period` bytes over the variable's `extent` bytes, as long as a value fits
 * whole. Only the variables of sections the ROM fills are listed. */
typedef struct {
    const char *name;
    uint32_t start;  /* offset in the section */
    uint32_t extent; /* bytes, up to the next variable */
    uint32_t period;
    uint32_t first;
    uint32_t count;
} GameVar;

/* A variable with storage of its own (it holds addresses, so its PC layout differs from the N64's). rom is 1 when the
 * N64 starts it with data from the ROM, 0 when it starts as zeros. */
typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t size;
    int rom;
} GameVarOwn;

#define GAME_VAR_NO_ROM 0xFFFFFFFFu /* a section of zeros (BSS) */
#define GAME_VAR_SECTIONS 5

/* An N64 section: the block of PC memory that holds it, its N64 address and size, its ROM offset, the overlay it
 * belongs to (0: main), its block variables and its own-storage variables. */
typedef struct {
    const char *name;
    uint8_t *block;
    uint32_t vram;
    uint32_t size;
    uint32_t rom;
    int overlay;
    const GameVar *vars;
    uint32_t var_count;
    const GameVarOwn *own;
    uint32_t own_count;
} GameVarSection;

extern const GameVarSection game_var_sections[GAME_VAR_SECTIONS];
extern const GameVarSwap game_var_swaps[];

/* Starts the variables of main (overlay 0) or of overlay 1 or 2 as the N64 does when it loads that code: each of its
 * sections that the ROM fills is copied from the ROM, and each value of the swap patterns turned into the PC's byte
 * order; its BSS and the own storage of its variables are set to zeros. Loading an overlay again starts it afresh, as
 * on the N64. The address-holding variables that the N64 starts from the ROM (rom = 1) stay zeros for now. */
void game_vars_load(int overlay);

#endif
