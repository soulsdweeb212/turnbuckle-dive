/* The developer match (turnbuckle-dive --dev-match): starts the game straight into a match, without the menus, and
 * runs it with no picture yet (docs/GAME_CODE.md, "The developer match").
 *
 * It does what the N64 does before a match, in the same order:
 * 1. The game's variables of main start from the ROM, and overlay 2's are loaded as the boot loop's func_80000700
 *    loads overlay 2 (game_vars_load).
 * 2. The four calls that open the boot loop (func_80013108): func_80001260 (graphics), func_80003120 (audio),
 *    func_80003D20 (controllers) and initializeAssetSizeTable (the table of resource sizes).
 * 3. func_8002B750, which builds the table of wrestler names; on the N64 the menus call it.
 * 4. The settings the menus would write for an exhibition match (write_settings below).
 * 5. Overlay 2's entry, func_80090000_ovl2, which runs the whole match and returns when it ends.
 *
 * The match is CPU against CPU, so it plays itself. When its results screen waits for a button (phase 4), the
 * developer match presses A on controller 1, as a player would. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev_match.h"
#include "game.h"
#include "game_sdk.h"
#include "game_vars.h"
#include "rom.h"

/* The game's functions that the developer match calls (the decompilation's src/main and src/ovl2_80090000). */
void func_80001260(void);
void func_80003120(void);
int32_t func_80003D20(void);
int32_t func_8002B750(void);
void func_80090000_ovl2(void);

/* The game's variables that it writes or watches. Each is a name for its N64 address in the block of its section
 * (src/game_vars.h), whose bytes are in the PC's byte order. */
extern uint8_t D_8007E228[]; /* the match setup block */
extern uint8_t D_8007E8C0[]; /* the players' controllers and match type */
extern uint8_t D_8007EF30[]; /* the game's options */
extern int8_t D_800EB465;    /* the match phase: 0 to 4 (runMatchPhaseStateMachine) */
extern uint32_t D_800E2FAC;  /* the match frame, counted by func_8009A374 */

/* Writes a value of `size` bytes at `offset` in a block. */
static void put(uint8_t *block, unsigned offset, int32_t value, unsigned size) {
    if (size == 1) {
        int8_t v = (int8_t)value;
        memcpy(block + offset, &v, 1);
    } else if (size == 2) {
        int16_t v = (int16_t)value;
        memcpy(block + offset, &v, 2);
    } else {
        memcpy(block + offset, &value, 4);
    }
}

/* The settings of an exhibition match that the menus (overlay 1) write and the match (overlay 2) reads. Each offset and
 * value is explained in the decompilation's notes on the hand-over; the first word of D_8007E228 is not a setting and
 * is never written (func_80001260 sets it at boot). */
static void write_settings(void) {
    static const uint8_t body[8] = { 0xFF, 0x32, 0x32, 0x32, 0x32, 0x32, 0x00, 0x00 };
    static const uint8_t buttons[9] = { 0x00, 0x01, 0x05, 0x03, 0x02, 0x04, 0x07, 0x06, 0x0A };
    static const int32_t characters[4] = { 0x001, 0x201, -1, -1 }; /* Sting, Hollywood Hogan, no one, no one */
    static const int32_t controllers[4] = { 0, 0, -1, -1 };          /* 0: the CPU plays the slot; -1: empty */
    int i;

    put(D_8007E228, 0x58, 0x012F, 2); /* singles (0x100), slot 2 on the other team (0x20), CPU slots 1-4 (0xF) */
    for (i = 0; i < 4; i++) {
        memcpy(D_8007E228 + 0x60 + 8 * i, body, 8); /* health and the five body parts at full, nothing carried */
        put(D_8007E228, 0x610 + 4 * i, 5, 4);        /* damage handicap: x1.0 */
        put(D_8007E228, 0x688 + 4 * i, characters[i], 4);
        put(D_8007E8C0, 0x000 + 4 * i, controllers[i], 4);
        memcpy(D_8007E8C0 + 0x48 + 9 * i, buttons, 9); /* the default button mapping */
    }
    put(D_8007E228, 0x92, 1, 1);     /* time limit: 5 minutes */
    put(D_8007E228, 0x608, 0x7F, 1); /* music on */
    put(D_8007E228, 0x624, 1, 1);    /* the song of arena 0 */
    put(D_8007E8C0, 0x010, 0, 4);    /* match type: singles */
    put(D_8007E8C0, 0x66C, 2, 1);    /* ring-out count: 20 */
    put(D_8007EF30, 0x000, 1, 1);    /* the default for the blood option */
    put(D_8007EF30, 0x170, 0x0033, 2); /* rules: the first preset's, with run-ins off */
    put(D_8007EF30, 0x1A0, 3, 1);
    put(D_8007EF30, 0x1A1, 1, 1);
    put(D_8007EF30, 0xCE0, 0, 1);    /* arena 0 */
    put(D_8007EF30, 0xCFC, 1, 1);
}

/* ---- What the developer match does at each hand-off ---- */

#define FRAME_CAP 20000 /* a match that has not ended after this many frames has hung */
#define RESULTS_WAIT 91 /* the results screen takes a new press only after this many frames */

static uint32_t g_frames;
static uint32_t g_phase4_at; /* the hand-off count when phase 4 began, 0 before */
static int g_press_a;

static void on_present(int kind) {
    if (kind != GAME_PRESENT_FRAME) {
        return;
    }
    g_frames++;
    if (g_frames % 300 == 0) {
        printf("dev-match: frame %u, match frame %u, phase %d\n", g_frames, D_800E2FAC, (int)D_800EB465);
        fflush(stdout);
    }
    if (D_800EB465 >= 4 && g_phase4_at == 0) {
        g_phase4_at = g_frames;
        printf("dev-match: the match ended at frame %u (match frame %u)\n", g_frames, D_800E2FAC);
    }
    /* A at the results screen: pressed for one frame, released for one, so the game sees a new press. */
    g_press_a = g_phase4_at != 0 && g_frames - g_phase4_at > RESULTS_WAIT && (g_frames & 1) == 0;
    if (g_frames >= FRAME_CAP) {
        port_fatal("dev-match: the match has not ended after %u frames (phase %d)", g_frames, (int)D_800EB465);
    }
}

static void read_pad(int channel, GamePad *pad) {
    memset(pad, 0, sizeof(*pad));
    if (channel == 0 && g_press_a) {
        pad->buttons = 0x8000; /* A */
    }
}

int dev_match_run(const Rom *rom) {
    static const GameSdkHooks hooks = { on_present, read_pad };

    rom_attach(rom);
    game_sdk_set(&hooks, 0x1); /* controller 1 plugged in, for the results screen */
    game_vars_use_functions(game_var_functions, game_var_function_count);
    game_vars_load(0);
    game_vars_load(2);

    printf("dev-match: boot\n");
    func_80001260();
    func_80003120();
    func_80003D20();
    initializeAssetSizeTable();
    func_8002B750();
    write_settings();

    printf("dev-match: Sting vs Hollywood Hogan, CPU vs CPU, arena 0, 5 minutes\n");
    fflush(stdout);
    func_80090000_ovl2();

    printf("dev-match: done after %u frames (match frame %u, phase %d)\n", g_frames, D_800E2FAC, (int)D_800EB465);
    rom_attach(NULL);
    return g_phase4_at != 0 ? 0 : 1;
}
