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
 * developer match presses A on controller 1, as a player would.
 *
 * It checks the G6 gate as it goes, and fails with a message when a check fails:
 * - the phase (D_800EB465) reaches 4, the match's end, and never goes past 4: two end checks in one frame skip phase
 *   3, and three push the phase past 4, where the match never ends (runMatchPhaseStateMachine);
 * - the match ends within FRAME_CAP frames;
 * - the match's first loads are resources 0x126 to 0x12A, in that order (resetMatchState);
 * - a watchdog thread stops the program when no frame arrives for 3 seconds (a hang inside one frame). */
#define _POSIX_C_SOURCE 200809L /* nanosleep, which strict C17 leaves out of <time.h> */

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dev_match.h"
#include "dev_records.h"
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

/* The game's variables that it writes. Each is a name for its N64 address in the block of its section
 * (src/game_vars.h), whose bytes are in the PC's byte order. */
extern uint8_t D_8007E228[]; /* the match setup block */
extern uint8_t D_8007E8C0[]; /* the players' controllers and match type */
extern uint8_t D_8007EF30[]; /* the game's options */
extern uint32_t xseed;       /* guRandom's seed */

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
    put(D_8007E228, 0x92, 1, 1);       /* time limit: 5 minutes */
    put(D_8007E228, 0x608, 0x7F, 1);   /* music on */
    put(D_8007E228, 0x624, 1, 1);      /* the song of arena 0 */
    put(D_8007E8C0, 0x010, 0, 4);      /* match type: singles */
    put(D_8007E8C0, 0x66C, 2, 1);      /* ring-out count: 20 */
    put(D_8007EF30, 0x000, 1, 1);      /* the default for the blood option */
    put(D_8007EF30, 0x170, 0x0033, 2); /* rules: the first preset's, with run-ins off */
    put(D_8007EF30, 0x1A0, 3, 1);
    put(D_8007EF30, 0x1A1, 1, 1);
    put(D_8007EF30, 0xCE0, 0, 1); /* arena 0 */
    put(D_8007EF30, 0xCFC, 1, 1);
}

/* ---- The run's state ---- */

#define FRAME_CAP 20000 /* a match that has not ended after this many frames has hung */
#define RESULTS_WAIT 91 /* the results screen takes a new press only after this many frames */
#define WATCHDOG_SECONDS 3

static const DevMatchOptions *g_options;
static uint32_t g_frames;
static uint32_t g_phase4_at; /* the frame when phase 4 began, 0 before */
static int g_press_a;
static int g_failed;
static FILE *g_log;
static FILE *g_res_log;

static uint32_t g_loads[5]; /* the match's first loads */
static unsigned g_load_count;
static int g_in_match;

static atomic_uint g_alive;     /* goes up at every sign of life, for the watchdog */
static atomic_int g_watch_stop; /* 1: the watchdog thread ends */

static void fail(const char *what) {
    printf("dev-match: FAIL: %s\n", what);
    fflush(stdout);
    g_failed = 1;
}

void dev_match_alive(void) {
    atomic_fetch_add(&g_alive, 1);
}

uint32_t dev_match_frames(void) {
    return g_frames;
}

/* ---- The wrestler log ----
 * A binary file, in the PC's byte order (little-endian): the header "TDWRLOG1", then u32 record size, u32 number of
 * records (4), u32 number of fields, and for each field u16 name length, the name, u32 offset and u32 size (the fields
 * of the game's Wrestler, src/dev/dev_records.c); then one entry per frame: u32 frame, u32 match frame (D_800E2FAC),
 * u32 seed (xseed), i32 phase, and the four records (dev_records_snapshot). tools/wr_log.py reads it. */
static void put32(FILE *f, uint32_t v) {
    fwrite(&v, 4, 1, f);
}

static void log_open(const char *path) {
    size_t count, record_size, i;
    const DevField *fields = dev_records_fields(&count, &record_size);

    g_log = fopen(path, "wb");
    if (g_log == NULL) {
        port_fatal("dev-match: cannot write the wrestler log %s", path);
    }
    fwrite("TDWRLOG1", 8, 1, g_log);
    put32(g_log, (uint32_t)record_size);
    put32(g_log, DEV_WRESTLERS);
    put32(g_log, (uint32_t)count);
    for (i = 0; i < count; i++) {
        uint16_t n = (uint16_t)strlen(fields[i].name);

        fwrite(&n, 2, 1, g_log);
        fwrite(fields[i].name, n, 1, g_log);
        put32(g_log, fields[i].offset);
        put32(g_log, fields[i].size);
    }
}

static void log_frame(const DevMatch *m) {
    static uint8_t *buf;
    size_t n = dev_records_snapshot_size();

    if (buf == NULL) {
        buf = malloc(n);
        if (buf == NULL) {
            port_fatal("dev-match: no memory for the wrestler log");
        }
    }
    put32(g_log, g_frames);
    put32(g_log, m->match_frame);
    put32(g_log, m->seed);
    put32(g_log, (uint32_t)(int32_t)m->phase);
    dev_records_snapshot(buf);
    fwrite(buf, n, 1, g_log);
}

/* ---- The watchdog ---- */

static void *watchdog(void *arg) {
    unsigned last = atomic_load(&g_alive);
    int still = 0;

    (void)arg;
    while (!atomic_load(&g_watch_stop)) {
        struct timespec second = { 1, 0 };
        unsigned now;

        nanosleep(&second, NULL);
        now = atomic_load(&g_alive);
        still = now == last ? still + 1 : 0;
        last = now;
        if (still >= WATCHDOG_SECONDS) {
            fflush(stdout);
            fprintf(stderr, "dev-match: watchdog: no frame for %d seconds after frame %u: the game hangs inside a "
                            "frame (run it in the debugger and pause to see where)\n",
                    WATCHDOG_SECONDS, g_frames);
            abort();
        }
    }
    return NULL;
}

/* ---- What the developer match does at each hand-off ---- */

static void on_present(int kind) {
    DevWrestler w[DEV_WRESTLERS];
    DevMatch m;

    dev_match_alive();
    if (kind != GAME_PRESENT_FRAME) {
        return;
    }
    g_frames++;
    dev_records_read(w, &m);
    if (g_log != NULL) {
        log_frame(&m);
    }
    if (g_options->on_frame == NULL && g_frames % 300 == 0) {
        printf("dev-match: frame %u, match frame %u, phase %d, health %d / %d\n", g_frames, m.match_frame,
               (int)m.phase, w[0].health, w[1].health);
        fflush(stdout);
    }
    if (m.phase > 4 && !g_failed) {
        fail("the phase went past 4, where the match never ends (two end checks in one frame)");
    }
    if (m.phase >= 4 && g_phase4_at == 0) {
        g_phase4_at = g_frames;
        printf("dev-match: the match ended at frame %u (match frame %u)\n", g_frames, m.match_frame);
        fflush(stdout);
    }
    /* A at the results screen: pressed for one frame, released for one, so the game sees a new press. */
    g_press_a = g_phase4_at != 0 && g_frames - g_phase4_at > RESULTS_WAIT && (g_frames & 1) == 0;
    if (g_frames >= FRAME_CAP) {
        port_fatal("dev-match: the match has not ended after %u frames (phase %d)", g_frames, (int)m.phase);
    }
    if (g_options->on_frame != NULL) {
        g_options->on_frame(g_options->user);
    }
}

static void read_pad(int channel, GamePad *pad) {
    memset(pad, 0, sizeof(*pad));
    if (channel == 0 && g_press_a) {
        pad->buttons = 0x8000; /* A */
    }
}

/* The resource log (--res-log): "frame N: resource 0xID" for each resource the game loads, frame 0 being boot. */
static void on_load(uint32_t id) {
    if (g_in_match && g_load_count < sizeof(g_loads) / sizeof(g_loads[0])) {
        g_loads[g_load_count++] = id;
    }
    if (g_res_log != NULL) {
        fprintf(g_res_log, "frame %u: resource 0x%04X\n", g_frames, id);
    }
}

int dev_match_run(const Rom *rom, const DevMatchOptions *options) {
    static const GameSdkHooks hooks = { on_present, read_pad };
    static const DevMatchOptions none = { NULL, NULL, 0, 0, 0, NULL, NULL };
    pthread_t dog;
    unsigned i;

    g_options = options != NULL ? options : &none;
    rom_attach(rom);
    game_sdk_set(&hooks, 0x1); /* controller 1 plugged in, for the results screen */
    port_res_hook(on_load);
    game_vars_use_functions(game_var_functions, game_var_function_count);
    game_vars_load(0);
    game_vars_load(2);
    if (g_options->has_seed) {
        xseed = g_options->seed;
    }
    if (g_options->wr_log != NULL) {
        log_open(g_options->wr_log);
    }
    if (g_options->res_log != NULL) {
        g_res_log = fopen(g_options->res_log, "w");
        if (g_res_log == NULL) {
            port_fatal("dev-match: cannot write the resource log %s", g_options->res_log);
        }
    }
    if (!g_options->no_watchdog && pthread_create(&dog, NULL, watchdog, NULL) != 0) {
        port_fatal("dev-match: cannot start the watchdog thread");
    }

    printf("dev-match: boot\n");
    func_80001260();
    func_80003120();
    func_80003D20();
    initializeAssetSizeTable();
    func_8002B750();
    write_settings();

    printf("dev-match: Sting vs Hollywood Hogan, CPU vs CPU, arena 0, 5 minutes\n");
    fflush(stdout);
    g_in_match = 1;
    func_80090000_ovl2();
    g_in_match = 0;

    if (!g_options->no_watchdog) {
        atomic_store(&g_watch_stop, 1);
        pthread_join(dog, NULL);
    }
    if (g_log != NULL) {
        fclose(g_log);
        g_log = NULL;
    }
    if (g_res_log != NULL) {
        fclose(g_res_log);
        g_res_log = NULL;
    }
    port_res_hook(NULL);
    game_sdk_set(NULL, 0x1);
    rom_attach(NULL);

    if (g_phase4_at == 0) {
        fail("the match returned before phase 4");
    }
    for (i = 0; i < 5; i++) {
        if (i >= g_load_count || g_loads[i] != 0x126 + i) {
            char what[96];

            snprintf(what, sizeof(what), "the match's load %u is resource 0x%X, not 0x%X", i,
                     i < g_load_count ? g_loads[i] : 0, 0x126 + i);
            fail(what);
            break;
        }
    }
    printf("dev-match: %s after %u frames\n", g_failed ? "FAILED" : "passed", g_frames);
    return g_failed ? 1 : 0;
}
