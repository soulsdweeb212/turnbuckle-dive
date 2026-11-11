/* game_vars_check: links every game file of the developer program with the game's variables, and checks that the
 * variables start as the N64 starts them (docs/GAME_CODE.md, "The game's variables").
 *
 * 1. The link itself: this program is linked from every file of td_game_all, the variables (td_game_vars) and the SDK
 *    stand-ins (td_game_sdk), so it only builds when no game file uses a name that the port lacks.
 * 2. It fills the variables from the ROM (game_vars_load for main and both overlays), then compares every section the
 *    ROM fills with the ROM, independently of how game_vars_load did it: each value of a swap pattern, read as a PC
 *    integer of its width, must equal the ROM's bytes at its place read big-endian; every other byte must equal the
 *    ROM's byte.
 * 3. Loading overlay 2 again starts its variables afresh: a changed data byte comes back, and its BSS and own storage
 *    go back to zeros.
 * 4. A function's old name (set by the linker from the map) is the same function: func_800E2400 is
 *    tryBeginBlockReaction.
 *
 * It needs the ROM, so CI never runs it; with no ROM it exits with code 77, which CTest reports as "skipped". It finds
 * the ROM like the program does (rom_find in src/rom.c).
 *
 * Usage: game_vars_check [--unswap]
 * - --unswap turns one 4-byte value back into the N64's byte order after the fill, so the comparison must fail with
 *   "FAIL: main.data". The test game_vars_detects_change runs it this way, which shows that the comparison really
 *   looks at the values.
 * Exit code: 0 passed, 1 failed, 2 wrong arguments, 77 skipped (no ROM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "game_vars.h"
#include "rom.h"

#define EXIT_SKIP 77

/* Two names of one game function (the map's "function" lines). */
void tryBeginBlockReaction(void);
void func_800E2400(void);

static uint64_t native_value(const uint8_t *p, unsigned width) {
    uint16_t v16;
    uint32_t v32;
    uint64_t v64;

    switch (width) {
    case 2:
        memcpy(&v16, p, 2);
        return v16;
    case 4:
        memcpy(&v32, p, 4);
        return v32;
    default:
        memcpy(&v64, p, 8);
        return v64;
    }
}

static uint64_t big_endian_value(const uint8_t *p, unsigned width) {
    uint64_t v = 0;
    unsigned i;

    for (i = 0; i < width; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* Compares one section with the ROM; returns the number of values compared, or -1 after printing the first
 * difference. */
static long check_section(const GameVarSection *sec) {
    uint8_t *rom = malloc(sec->size);
    uint8_t *covered = calloc(sec->size, 1);
    long values = 0;
    uint32_t i;
    uint32_t b;

    if (rom == NULL || covered == NULL) {
        printf("FAIL: out of memory\n");
        free(rom);
        free(covered);
        return -1;
    }
    port_rom_read(rom, sec->rom, sec->size);
    for (i = 0; i < sec->var_count; i++) {
        const GameVar *v = &sec->vars[i];
        uint32_t base;
        uint32_t k;

        for (base = 0; base < v->extent; base += v->period) {
            for (k = 0; k < v->count; k++) {
                const GameVarSwap *s = &game_var_swaps[v->first + k];
                uint32_t at = v->start + base + s->offset;

                if (base + s->offset + s->width > v->extent) {
                    continue;
                }
                if (native_value(sec->block + at, s->width) != big_endian_value(rom + at, s->width)) {
                    printf("FAIL: %s: %s+0x%X (0x%08X), a %u-byte value, is 0x%llX on PC, 0x%llX in the ROM\n",
                           sec->name, v->name, at - v->start, sec->vram + at, s->width,
                           (unsigned long long)native_value(sec->block + at, s->width),
                           (unsigned long long)big_endian_value(rom + at, s->width));
                    free(rom);
                    free(covered);
                    return -1;
                }
                memset(covered + at, 1, s->width);
                values++;
            }
        }
    }
    for (b = 0; b < sec->size; b++) {
        if (!covered[b] && sec->block[b] != rom[b]) {
            printf("FAIL: %s: the byte at 0x%08X is 0x%02X on PC, 0x%02X in the ROM\n", sec->name, sec->vram + b,
                   sec->block[b], rom[b]);
            free(rom);
            free(covered);
            return -1;
        }
    }
    free(rom);
    free(covered);
    return values;
}

/* Changes overlay 2's variables, loads it again and checks that they start afresh. */
static int check_reload(void) {
    const GameVarSection *data = NULL;
    const GameVarSection *bss = NULL;
    int i;
    uint8_t before;

    for (i = 0; i < GAME_VAR_SECTIONS; i++) {
        const GameVarSection *sec = &game_var_sections[i];

        if (sec->overlay == 2) {
            *(sec->rom == GAME_VAR_NO_ROM ? &bss : &data) = sec;
        }
    }
    if (data == NULL || bss == NULL || bss->own_count == 0) {
        printf("FAIL: overlay 2 has no data, BSS or own storage in the tables\n");
        return 0;
    }
    before = data->block[0];
    data->block[0] ^= 0xFF;
    bss->block[bss->size - 1] = 0x5A;
    bss->own[0].data[0] = 0x5A;
    game_vars_load(2);
    if (data->block[0] != before || bss->block[bss->size - 1] != 0 || bss->own[0].data[0] != 0) {
        printf("FAIL: loading overlay 2 again did not start its variables afresh\n");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *path;
    const char *env = getenv("TD_ROM");
    Rom rom;
    RomStatus status;
    int unswap = 0;
    int i;
    int ok = 1;
    long values = 0;
    unsigned rom_tables = 0;

    if (argc == 2 && strcmp(argv[1], "--unswap") == 0) {
        unswap = 1;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: game_vars_check [--unswap]\n");
        return 2;
    }

    if ((void (*)(void))func_800E2400 != tryBeginBlockReaction) {
        printf("FAIL: func_800E2400 is not tryBeginBlockReaction\n");
        return EXIT_FAILURE;
    }

    path = rom_find(NULL);
    status = rom_load(path, &rom);
    if (status == ROM_NOT_FOUND && (env == NULL || env[0] == '\0')) {
        printf("SKIP: no ROM found (set TD_ROM to your ROM, or put it in the decompilation's folder as "
               "baserom.z64)\n");
        return EXIT_SKIP;
    }
    if (status != ROM_OK) {
        printf("FAIL: ROM %s: %s\n", path, rom_status_text(status));
        rom_free(&rom);
        return EXIT_FAILURE;
    }
    printf("ROM: %s\n", path);
    rom_attach(&rom);

    game_vars_load(0);
    game_vars_load(1);
    game_vars_load(2);
    if (unswap) {
        /* The first 4-byte value of main's data whose bytes do not read the same both ways (a zero would not change),
         * back in the N64's byte order. */
        const GameVarSection *sec = &game_var_sections[0];
        uint32_t v;

        for (v = 0; v < sec->var_count; v++) {
            const GameVar *var = &sec->vars[v];
            uint8_t *p = sec->block + var->start + game_var_swaps[var->first].offset;

            if (var->count != 0 && game_var_swaps[var->first].width == 4 && (p[0] != p[3] || p[1] != p[2])) {
                uint8_t t = p[0];

                p[0] = p[3];
                p[3] = t;
                t = p[1];
                p[1] = p[2];
                p[2] = t;
                break;
            }
        }
    }

    for (i = 0; i < GAME_VAR_SECTIONS && ok; i++) {
        const GameVarSection *sec = &game_var_sections[i];
        uint32_t j;

        for (j = 0; j < sec->own_count; j++) {
            rom_tables += (unsigned)sec->own[j].rom;
        }
        if (sec->rom != GAME_VAR_NO_ROM) {
            long n = check_section(sec);

            if (n < 0) {
                ok = 0;
            } else {
                values += n;
            }
        }
    }
    if (ok) {
        ok = check_reload();
    }
    if (ok) {
        printf("The game's variables start as on the N64: %ld values in their PC byte order, every other byte as in the "
               "ROM; overlay 2 starts afresh when loaded again. Not filled yet: %u address tables.\n",
               values, rom_tables);
    }
    rom_attach(NULL);
    rom_free(&rom);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
