/* Fills the game's variables from the user's ROM (src/game_vars.h; docs/GAME_CODE.md, "The game's variables"). */
#include <string.h>

#include "game.h"
#include "game_vars.h"

/* Turns the w-byte value at p from the N64's byte order (most significant byte first) into the PC's (least
 * significant byte first) by reversing its bytes. */
static void swap_value(uint8_t *p, unsigned w) {
    unsigned i;

    for (i = 0; i < w / 2; i++) {
        uint8_t t = p[i];
        p[i] = p[w - 1 - i];
        p[w - 1 - i] = t;
    }
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
}
