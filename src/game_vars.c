/* Fills the game's variables from the user's ROM (src/game_vars.h; docs/GAME_CODE.md, "The game's variables"). */
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "game_vars.h"
#include "dev/order_check.h"

/* Turns the w-byte value at p from the N64's byte order (most significant byte first) into the PC's (least
 * significant byte first) by reversing its bytes. */
static void swap_value(uint8_t *p, unsigned w) {
    unsigned i;

    for (i = 0; i < w / 2; i++) {
        uint8_t t = p[i];
        p[i] = p[w - 1 - i];
        p[w - 1 - i] = t;
    }
    ORDER_CHECK_VALUE(p, w);
}

static void swap_var(uint8_t *block, const GameVar *v) {
    uint32_t base;
    uint32_t i;

    for (base = 0; base < v->extent; base += v->period) {
        for (i = 0; i < v->count; i++) {
            const GameVarSwap *s = &game_var_swaps[v->first + i];

            if (base + s->offset + s->width <= v->extent) {
                swap_value(block + v->start + base + s->offset, s->width);
            }
        }
    }
}

static const GameVarFunction *g_functions;
static uint32_t g_function_count;

void game_vars_use_functions(const GameVarFunction *functions, uint32_t count) {
    g_functions = functions;
    g_function_count = count;
}

void *game_vars_address(uint32_t n64, int overlay) {
    uint32_t lo = 0;
    uint32_t hi = game_var_target_count;
    int i;

    if (n64 == 0) {
        return NULL;
    }
    while (lo < hi) { /* binary search for the first target at n64: the targets are sorted by N64 address */
        uint32_t mid = lo + (hi - lo) / 2;

        if (game_var_targets[mid].n64 < n64) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < game_var_target_count && game_var_targets[lo].n64 == n64; lo++) {
        if (game_var_targets[lo].overlay == 0 || game_var_targets[lo].overlay == overlay) {
            return game_var_targets[lo].pc;
        }
    }
    for (i = 0; i < GAME_VAR_SECTIONS; i++) {
        const GameVarSection *sec = &game_var_sections[i];

        if ((sec->overlay == 0 || sec->overlay == overlay) && n64 >= sec->vram && n64 - sec->vram < sec->size) {
            return sec->block + (n64 - sec->vram);
        }
    }
    port_fatal("game_vars_address: 0x%08X names no game variable of overlay %d", n64, overlay);
}

void (*game_vars_function(uint32_t n64))(void) {
    uint32_t lo = 0;
    uint32_t hi = g_function_count;

    if (g_functions == NULL) {
        port_fatal("game_vars_function: 0x%08X is a function, but this program has no function table "
                   "(game_vars_use_functions)",
                   n64);
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;

        if (g_functions[mid].n64 == n64) {
            return g_functions[mid].pc;
        }
        if (g_functions[mid].n64 < n64) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    port_fatal("game_vars_function: 0x%08X is not a function of the address tables", n64);
}

/* Builds one address table in its PC layout from its N64 bytes in the ROM. */
static void fill_table(const GameVarSection *sec, const GameVarFill *f) {
    uint32_t n64_bytes = f->count * f->n64_size;
    uint8_t *rom = malloc(n64_bytes);
    uint32_t k;
    uint32_t i;

    if (rom == NULL) {
        port_fatal("game_vars_load: no memory for %s", f->name);
    }
    port_rom_read(rom, sec->rom + (f->n64 - sec->vram), n64_bytes);
    memset(f->data, 0, (size_t)f->count * f->pc_size);
    for (k = 0; k < f->count; k++) {
        for (i = 0; i < f->field_count; i++) {
            const GameVarField *fd = &game_var_fields[f->first_field + i];
            const uint8_t *from = rom + k * f->n64_size + fd->n64_offset;
            uint8_t *to = f->data + (size_t)k * f->pc_size + fd->pc_offset;
            uint32_t word = 0; /* an address field's N64 word, big-endian */

            if (fd->kind == 'p' || fd->kind == 'f' || fd->kind == 'k') {
                word = ((uint32_t)from[0] << 24) | ((uint32_t)from[1] << 16) | ((uint32_t)from[2] << 8) | from[3];
            }
            switch (fd->kind) {
            case 'p': {
                void *p = game_vars_address(word, sec->overlay);
                memcpy(to, &p, sizeof(p));
                break;
            }
            case 'k': {
                /* a number or an address: the game tells them apart by the sign of the s32, so on PC an address must
                 * be 0x80000000 or more and a number stays below (the decompilation's IdOrAddr) */
                uintptr_t v = (uintptr_t)(intptr_t)(int32_t)word; /* a number; -1 stays -1 (all ones) */

                if ((word & 0x80000000u) && word != 0xFFFFFFFFu) {
                    v = (uintptr_t)game_vars_address(word, sec->overlay);
                    if (v < 0x80000000u) {
                        port_fatal("game_vars_load: %s: the PC address %p of 0x%08X could be taken for a number",
                                   f->name, (void *)v, word);
                    }
                }
                memcpy(to, &v, sizeof(v));
                break;
            }
            case 'f': {
                void (*fn)(void) = word != 0 ? game_vars_function(word) : NULL;
                memcpy(to, &fn, sizeof(fn));
                break;
            }
            case 'z':
                break; /* never read: NULL, from the memset */
            default: {
                unsigned w = (unsigned)(fd->kind - '0');

                memcpy(to, from, w); /* a value: its bytes, turned into the PC's byte order */
                swap_value(to, w);
                break;
            }
            }
        }
    }
    free(rom);
}

static uint32_t big_endian_word(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* The commands whose second word is an address: G_VTX (vertices), G_DL (another list), G_SETTIMG (a texture or
 * palette) and G_MOVEMEM (the viewport). The decompilation's tools/port_data.py (GBI_OPS) has checked every command of
 * every list against these: each such word is the KSEG0 or physical address of data that the ROM fills. */
static int gfx_holds_address(uint32_t w0) {
    uint32_t op = w0 >> 24;

    return op == 0x01 || op == 0xDE || op == 0xFD || op == 0xDC;
}

/* Builds the PC copy of one display list from its N64 commands in the ROM (GameVarGfx). */
static void build_gfx(const GameVarSection *sec, const GameVarGfx *g) {
    uint8_t *rom = malloc((size_t)g->count * 8);
    uintptr_t *to = (uintptr_t *)g->data;
    uint32_t k;

    if (rom == NULL) {
        port_fatal("game_vars_load: no memory for %s", g->name);
    }
    port_rom_read(rom, sec->rom + (g->n64 - sec->vram), g->count * 8);
    for (k = 0; k < g->count; k++) {
        uint32_t w0 = big_endian_word(rom + k * 8);
        uint32_t w1 = big_endian_word(rom + k * 8 + 4);

        to[k * 2] = w0;
        /* A physical address (0x000C5C30) names the same place as its KSEG0 address (0x800C5C30). */
        to[k * 2 + 1] = gfx_holds_address(w0) ? (uintptr_t)game_vars_address(w1 | 0x80000000u, sec->overlay) : w1;
        ORDER_CHECK_VALUE(&to[k * 2], sizeof(uintptr_t));
        ORDER_CHECK_VALUE(&to[k * 2 + 1], sizeof(uintptr_t));
    }
    free(rom);
}

void game_vars_load(int overlay) {
    int i;
    uint32_t j;

    for (i = 0; i < GAME_VAR_SECTIONS; i++) {
        const GameVarSection *sec = &game_var_sections[i];

        if (sec->overlay != overlay) {
            continue;
        }
        if (sec->rom == GAME_VAR_NO_ROM) {
            memset(sec->block, 0, sec->size);
        } else {
            port_rom_read(sec->block, sec->rom, sec->size);
            for (j = 0; j < sec->var_count; j++) {
                swap_var(sec->block, &sec->vars[j]);
            }
        }
        for (j = 0; j < sec->own_count; j++) {
            memset(sec->own[j].data, 0, sec->own[j].size);
        }
    }
    /* The tables and display lists last: their addresses may name data of any section of this overlay, now in
     * place. */
    for (i = 0; i < GAME_VAR_SECTIONS; i++) {
        const GameVarSection *sec = &game_var_sections[i];

        if (sec->overlay == overlay) {
            for (j = 0; j < sec->fill_count; j++) {
                fill_table(sec, &sec->fills[j]);
            }
            for (j = 0; j < sec->gfx_count; j++) {
                build_gfx(sec, &sec->gfx[j]);
            }
        }
    }
}

int32_t port_overlay_of(uint32_t rom_start, uint32_t rom_end) {
    int i;

    /* The overlay whose data the range holds: each overlay's code and data are one range of the ROM, and its data
     * section's ROM offset lies in it (overlay 1: 0x3C770 to 0x834A0, its data at 0x72240). */
    for (i = 0; i < GAME_VAR_SECTIONS; i++) {
        const GameVarSection *sec = &game_var_sections[i];

        if (sec->overlay != 0 && sec->rom != GAME_VAR_NO_ROM && sec->rom >= rom_start && sec->rom < rom_end) {
            return sec->overlay;
        }
    }
    port_fatal("port_overlay_of: the ROM range 0x%X to 0x%X holds no overlay's data", rom_start, rom_end);
}

void port_overlay_load(int32_t overlay) {
    if (overlay != 1 && overlay != 2) {
        port_fatal("port_overlay_load: no overlay %d", (int)overlay);
    }
    game_vars_load(overlay);
}
