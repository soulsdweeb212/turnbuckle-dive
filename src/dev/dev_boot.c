/* The developer boot (turnbuckle-dive --dev-boot FRAMES): starts the game as the N64 does, from its boot loop, with no
 * window, and stops after FRAMES frames. It is the first step towards running the menus (overlay 1) on PC: it shows
 * that the boot, the loading of overlay 1 and the menus' first frames run without stopping the program.
 *
 * What it does, in order:
 * 1. The game's variables of main start from the ROM (game_vars_load), and the functions that its address tables
 *    hold are handed over (game_vars_use_functions).
 * 2. The game's boot loop, func_80013108: it starts the graphics, sound and controllers and the table of resource
 *    sizes, then loads overlay 1 (the menus) with func_80000700 and runs it with func_8000082C, then overlay 2 (the
 *    match), and so on for ever. On PC func_80000700 starts the overlay's variables afresh (port_overlay_load) and
 *    func_8000082C calls the loaded overlay's entry (the decompilation's src/main/func_80000700.c, func_8000082C.c).
 * 3. At every frame hand-off it counts the frame, prints a line every 60 frames and, with a window, has the Match
 *    Inspector draw the frame (and take its screenshots); at the FRAMES-th it prints the count and ends the program.
 *
 * No controller is pressed: controller 1 is plugged in and reads as released. */
#include <stdio.h>
#include <stdlib.h>

#include "dev_boot.h"
#include "game.h"
#include "game_sdk.h"
#include "game_vars.h"

/* The game's boot loop (the decompilation's src/main/func_80013108.c). */
void func_80013108(void);

static uint32_t g_frames;
static uint32_t g_cap;
static void (*g_on_frame)(void *user);
static void *g_user;

static void on_present(int kind) {
    if (kind != GAME_PRESENT_FRAME) {
        return;
    }
    g_frames++;
    if (g_on_frame != NULL) {
        g_on_frame(g_user);
    }
    if (g_frames % 60 == 0) {
        printf("dev-boot: frame %u\n", (unsigned)g_frames);
        fflush(stdout);
    }
    if (g_frames >= g_cap) {
        printf("dev-boot: passed after %u frames\n", (unsigned)g_frames);
        fflush(stdout);
        /* The game never returns from its boot loop, so the program ends here. _Exit leaves out the exit handlers:
         * the game's heap blocks are still in use, which a leak check would report. */
        _Exit(0);
    }
}

uint32_t dev_boot_frames(void) {
    return g_frames;
}

int dev_boot_run(const Rom *rom, uint32_t frames, void (*on_frame)(void *user), void *user) {
    static const GameSdkHooks hooks = { on_present, NULL, NULL, NULL };

    g_cap = frames;
    g_on_frame = on_frame;
    g_user = user;
    rom_attach(rom);
    game_sdk_set(&hooks, 0x1); /* controller 1 plugged in */
    game_vars_use_functions(game_var_functions, game_var_function_count);
    game_vars_load(0);
    printf("dev-boot: boot, %u frames\n", (unsigned)frames);
    fflush(stdout);
    func_80013108();
    printf("dev-boot: the boot loop returned, which the N64's never does\n");
    return 1;
}
