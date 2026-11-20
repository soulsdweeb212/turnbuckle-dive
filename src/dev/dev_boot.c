/* The developer boot (turnbuckle-dive --dev-boot FRAMES): starts the game as the N64 does, from its boot loop, with no
 * window, and stops after FRAMES frames. The player's game (turnbuckle-dive with no developer mode) is the same boot,
 * with no frame limit and no progress lines (dev_boot_play). It is the first step towards running the menus (overlay 1) on PC: it shows
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
 * With --wr-log FILE it writes the wrestler log (src/dev/dev_records.h) at every hand-off while overlay 2, the match,
 * is loaded; dev_boot_at_end names what to do before the program ends (the sound comparison's files).
 *
 * Controller 1 is plugged in. Its pads come from a pad file (--replay-pads), else a pad script (--pad-script, frames
 * counted from the boot), else the window's controllers (as the Match Inspector reads them); without any, it reads
 * as released. --record-pads saves every read (src/dev/dev_pads.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev_boot.h"
#include "dev_pads.h"
#include "dev_records.h"
#include "game.h"
#include "game_sdk.h"
#include "game_vars.h"

/* The game's boot loop (the decompilation's src/main/func_80013108.c). */
void func_80013108(void);

static uint32_t g_frames;
static uint32_t g_cap;
static void (*g_on_frame)(void *user);
static void *g_user;
static const char *g_replay, *g_record, *g_script;
static int g_replay_ended;
static FILE *g_log;
static void (*g_at_end)(void);
static int g_play; /* dev_boot_play: the player's game, which prints no progress lines */

void dev_boot_wr_log(const char *path) {
    g_log = dev_records_log_open(path);
    if (g_log == NULL) {
        port_fatal("dev-boot: cannot write the wrestler log %s", path);
    }
}

void dev_boot_at_end(void (*at_end)(void)) {
    g_at_end = at_end;
}

static void on_present(int kind) {
    if (kind != GAME_PRESENT_FRAME) {
        return;
    }
    g_frames++;
    if (g_log != NULL && game_vars_overlay() == 2) {
        DevWrestler w[DEV_WRESTLERS];
        DevMatch m;

        dev_records_read(w, &m);
        dev_records_log_frame(g_log, g_frames, &m);
    }
    if (dev_pads_scripted()) {
        dev_pads_script_frame(g_frames);
    }
    if (g_on_frame != NULL) {
        g_on_frame(g_user);
    }
    if (!g_play && g_frames % 60 == 0) {
        printf("dev-boot: frame %u\n", (unsigned)g_frames);
        fflush(stdout);
    }
    if (g_cap != 0 && g_frames >= g_cap) {
        printf(g_play ? "Stopped after %u frames (--frames)\n" : "dev-boot: passed after %u frames\n",
               (unsigned)g_frames);
        fflush(stdout);
        /* The game never returns from its boot loop, so the program ends here. _Exit leaves out the exit handlers:
         * the game's heap blocks are still in use, which a leak check would report; the pad files are closed first. */
        dev_pads_close();
        if (g_log != NULL) {
            fclose(g_log);
        }
        if (g_at_end != NULL) {
            g_at_end();
        }
        fflush(stdout);
        _Exit(0);
    }
}

/* The game's pad reads (game_sdk.h's read_pad hook). */
static void read_pad(int channel, GamePad *pad) {
    memset(pad, 0, sizeof(*pad));
    if (dev_pads_replaying()) {
        if (!dev_pads_replay(channel, pad) && !g_replay_ended) {
            g_replay_ended = 1;
            printf("dev-boot: the pad file ended at frame %u; the controller reads as released from here\n",
                   (unsigned)g_frames);
            fflush(stdout);
        }
    } else if (dev_pads_scripted()) {
        if (channel == 0) {
            dev_pads_script(pad);
        }
    } else if (g_on_frame != NULL) {
        dev_pads_window(channel, pad);
    }
    dev_pads_record(g_frames, channel, pad);
}

void dev_boot_pads(const char *replay, const char *record, const char *script) {
    g_replay = replay;
    g_record = record;
    g_script = script;
}

void dev_boot_idle_randoms(int no_idle_randoms) {
    game_sdk_set_idle_randoms(no_idle_randoms ? 0 : 1);
}

void dev_boot_play(void) {
    g_play = 1;
}

uint32_t dev_boot_frames(void) {
    return g_frames;
}

int dev_boot_run(const Rom *rom, uint32_t frames, void (*on_frame)(void *user), void *user) {
    static const GameSdkHooks hooks = { on_present, read_pad, NULL, NULL };

    g_cap = frames;
    g_on_frame = on_frame;
    g_user = user;
    rom_attach(rom);
    dev_pads_open("dev-boot", g_replay, g_record, g_script);
    game_sdk_set(&hooks, 0x1); /* controller 1 plugged in */
    game_vars_use_functions(game_var_functions, game_var_function_count);
    game_vars_load(0);
    if (!g_play) {
        printf("dev-boot: boot, %u frames\n", (unsigned)frames);
        fflush(stdout);
    }
    func_80013108();
    printf("dev-boot: the boot loop returned, which the N64's never does\n");
    dev_pads_close();
    return 1;
}
