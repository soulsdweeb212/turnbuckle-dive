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
 * N64 starts it with data from the ROM (an address table, built by its GameVarFill), 0 when it starts as zeros. */
typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t size;
    int rom;
} GameVarOwn;

/* One field of an address table's element: where it is in the N64 element and in the PC element, and its kind: '1',
 * '2', '4' or '8' (a value of that many bytes, turned into the PC's byte order), 'p' (an N64 address, turned into the
 * PC address of what it names), 'k' (a word that holds a number or an address, the decompilation's IdOrAddr: an N64
 * address, negative as an s32, becomes the PC address, any other value stays a number, -1 as all ones), 'f' (the N64 address of a
 * function, turned into the PC function) or 'z' (an entry the game never reads, NULL on PC). */
typedef struct {
    uint16_t n64_offset;
    uint16_t pc_offset;
    char kind;
} GameVarField;

/* An address table, or an array or record that a table's addresses reach: its PC storage, its N64 address, and count
 * elements of n64_size bytes on the N64 and pc_size bytes on PC, each made of the `field_count` fields from
 * game_var_fields[first_field]. */
typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t n64;
    uint32_t count;
    uint32_t n64_size;
    uint32_t pc_size;
    uint32_t first_field;
    uint32_t field_count;
} GameVarFill;

/* A display list in the ROM's data (the map's "gfx" lines): its PC copy, its N64 address and its number of commands.
 * The N64's commands are 8 bytes, two big-endian 32-bit words; the PC's are 16, two pointer-wide words (LUS's Gfx),
 * so game_vars_load builds the copy command by command: each word widened, and the address in a command that holds
 * one (G_VTX, G_DL, G_SETTIMG, G_MOVEMEM) turned into the PC address of what it names (game_vars_address). */
typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t n64;
    uint32_t count;
} GameVarGfx;

/* An object at N64 address n64 of overlay `overlay` (0: main) is at pc on PC (game_var_targets, sorted by n64, then
 * overlay). The two overlays share addresses, so an address names an object of main or of the overlay whose data
 * holds it. */
typedef struct {
    uint32_t n64;
    int overlay;
    void *pc;
} GameVarTarget;

/* The function at N64 address n64 (the generated game_functions.c, sorted by n64). */
typedef struct {
    uint32_t n64;
    void (*pc)(void);
} GameVarFunction;

#define GAME_VAR_NO_ROM 0xFFFFFFFFu /* a section of zeros (BSS) */
#define GAME_VAR_SECTIONS 6

/* An N64 section: the block of PC memory that holds it, its N64 address and size, its ROM offset, the overlay it
 * belongs to (0: main), its block variables, its own-storage variables, its address tables and its display lists. The
 * overlays' sections share one area of PC memory by N64 address, as they share the N64's memory: overlay 1's BSS lies
 * where overlay 2's data lies, and loading either overlay fills or zeroes its own sections there. */
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
    const GameVarFill *fills;
    uint32_t fill_count;
    const GameVarGfx *gfx;
    uint32_t gfx_count;
} GameVarSection;

extern const GameVarSection game_var_sections[GAME_VAR_SECTIONS];
extern const GameVarSwap game_var_swaps[];
extern const GameVarField game_var_fields[];
extern const GameVarTarget game_var_targets[];
extern const uint32_t game_var_target_count;

/* The functions the address tables hold (game_functions.c in the build folder). Only programs that link every game
 * file have them; such a program hands them over with game_vars_use_functions before it loads overlay 2, whose tables
 * hold functions. */
extern const GameVarFunction game_var_functions[];
extern const uint32_t game_var_function_count;
void game_vars_use_functions(const GameVarFunction *functions, uint32_t count);

/* The PC address of the data that the N64 address n64 names, for the code of `overlay` (0: main, 1 or 2): an object
 * of game_var_targets of main or that overlay, or the place in a section's block. 0 gives NULL; any other address
 * stops the program. */
void *game_vars_address(uint32_t n64, int overlay);
/* The PC function at the N64 address n64 (game_vars_use_functions); any other address stops the program. */
void (*game_vars_function(uint32_t n64))(void);

/* Starts the variables of main (overlay 0) or of overlay 1 or 2 as the N64 does when it loads that code: each of its
 * sections that the ROM fills is copied from the ROM, and each value of the swap patterns turned into the PC's byte
 * order; its BSS and the own storage of its variables are set to zeros. Loading an overlay again starts it afresh, as
 * on the N64. The address tables of its sections are built in their PC layout, each N64 address turned into the PC
 * address of what it names (game_vars_address), and so are the PC copies of its display lists (GameVarGfx). */
void game_vars_load(int overlay);

#endif
