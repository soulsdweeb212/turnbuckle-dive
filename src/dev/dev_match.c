/* The developer match (turnbuckle-dive --dev-match): starts the game straight into a match, without the menus, and
 * runs it with no picture yet (docs/GAME_CODE.md, "The developer match").
 *
 * It does what the N64 does before a match, in the same order:
 * 1. The game's variables of main start from the ROM, and overlay 2's are loaded as the boot loop's func_80000700
 *    loads overlay 2 (game_vars_load).
 * 2. The four calls that open the boot loop (func_80013108): func_80001260 (graphics), func_80003120 (audio),
 *    func_80003D20 (controllers) and initializeAssetSizeTable (the table of resource sizes).
 * 3. func_8002B750, which builds the table of wrestler names; on the N64 the menus call it.
 * 4. The settings the menus would write for an exhibition match, or with battle_royal for a battle royal of four
 *    (write_settings below).
 * 5. Overlay 2's entry, func_80090000_ovl2, which runs the whole match and returns when it ends.
 *
 * By default the match is CPU against CPU, so it plays itself; when its results screen waits for a button (phase 4),
 * the developer match presses A on controller 1, as a player would. With players 1, Sting is played on controller 1:
 * from the window's controllers, a pad file, or the pad bot (DevMatchOptions); the developer match can save every pad
 * read for a replay.
 *
 * It checks the G6 gate as it goes, and fails with a message when a check fails:
 * - the phase (D_800EB465) reaches 4, the match's end, and never goes past 4: two end checks in one frame skip phase
 *   3, and three push the phase past 4, where the match never ends (runMatchPhaseStateMachine);
 * - the match ends within FRAME_CAP frames, or more for a long time limit (frame_cap);
 * - the match's first loads are resources 0x126 to 0x12A, in that order (resetMatchState);
 * - a watchdog thread stops the program when no frame arrives for 3 seconds (a hang inside one frame);
 * - in a headless run, the heap holds no more than LEAK_LIMIT above what it held one minute into the match (the memory
 *   check: a leak). */
#define _POSIX_C_SOURCE 200809L /* nanosleep, which strict C17 leaves out of <time.h> */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dev_match.h"
#include "dev_memory.h"
#include "dev_pads.h"
#include "dev_records.h"
#include "game.h"
#include "game_sdk.h"
#include "game_vars.h"
#include "rom.h"

/* The watchdog's thread: Windows' own threads there, POSIX threads elsewhere. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

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
/* The time limit's index in the game's table of minutes (D_800E399C: none, 5, 10, 15, 30, 60). */
static int time_limit_index(int minutes) {
    static const int table[] = { -1, 5, 10, 15, 30, 60 };
    int i;

    for (i = 0; i < 6; i++) {
        if (table[i] == minutes) {
            return i;
        }
    }
    return 1;
}

/* The wrestlers of each slot, by character id (0xBNN: the bank, then the index + 1, as src/dev/dev_records.h says);
 * -1 for an empty slot. */
static const int32_t kSinglesWrestlers[4] = { 0x001, 0x201, -1, -1 };   /* Sting, Hollywood Hogan */
static const int32_t kRoyalWrestlers[4] = { 0x001, 0x201, 0x002, 0x301 }; /* and Giant, Ultimo Dragon */

static void write_settings(const DevMatchOptions *o) {
    static const uint8_t body[8] = { 0xFF, 0x32, 0x32, 0x32, 0x32, 0x32, 0x00, 0x00 };
    static const uint8_t buttons[9] = { 0x00, 0x01, 0x05, 0x03, 0x02, 0x04, 0x07, 0x06, 0x0A };
    const int royal = o->battle_royal;
    const int32_t *characters = royal ? kRoyalWrestlers : kSinglesWrestlers;
    const int index = time_limit_index(o->time_limit == 0 ? 5 : o->time_limit);
    int32_t controllers[4];
    uint32_t kind = royal ? 0x400 : 0x120;
    int i;

    for (i = 0; i < 4; i++) {
        /* 1..4: a player on that controller; 0: the CPU plays the slot; -1: empty. Each slot the CPU plays, or that is
         * empty, gets a bit (0x1 to 0x8), as func_800AF978 sets them. */
        controllers[i] = characters[i] == -1 ? -1 : (i == 0 && o->players > 0) ? 1 : 0;
        if (controllers[i] <= 0) {
            kind |= 1u << i;
        }
    }
    /* singles (0x100) with slot 2 on the other team (0x20), or a battle royal (0x400), where every wrestler is on their
     * own; and the CPU's bits */
    put(D_8007E228, 0x58, (int32_t)kind, 2);
    for (i = 0; i < 4; i++) {
        memcpy(D_8007E228 + 0x60 + 8 * i, body, 8); /* health and the five body parts at full, nothing carried */
        put(D_8007E228, 0x610 + 4 * i, 5, 4);        /* damage handicap: x1.0 */
        put(D_8007E228, 0x688 + 4 * i, characters[i], 4);
        put(D_8007E8C0, 0x000 + 4 * i, controllers[i], 4);
        memcpy(D_8007E8C0 + 0x48 + 9 * i, buttons, 9); /* the default button mapping */
    }
    put(D_8007E228, 0x92, index, 1);           /* the time limit */
    put(D_8007E228, 0x608, 0x7F, 1);           /* music on */
    put(D_8007E228, 0x624, 1, 1);              /* the song of arena 0 */
    put(D_8007E8C0, 0x010, royal ? 3 : 0, 4);  /* match type: singles (0) or battle royal (3) */
    put(D_8007E8C0, 0x66C, royal ? 0 : 2, 1);  /* ring-out count: 20; none in a battle royal */
    put(D_8007EF30, 0x000, 1, 1);              /* the default for the blood option */
    /* rules: the first preset's, with run-ins off; a battle royal adds 0x400, which func_800AF978 sets for its ring-out
     * choice 4: a wrestler thrown over the top rope is out */
    put(D_8007EF30, 0x170, royal ? 0x0433 : 0x0033, 2);
    put(D_8007EF30, 0x1A0, royal ? (index == 0 ? 5 : index - 1) : 3, 1); /* the menus' time-limit choice */
    put(D_8007EF30, 0x1A1, royal ? 4 : 1, 1);                            /* and their ring-out choice */
    put(D_8007EF30, 0xCE0, 0, 1); /* arena 0 */
    put(D_8007EF30, 0xCFC, royal ? 6 : 1, 1);
    if (royal) {
        /* The other values that a run of the menus to a battle royal (Special Match, Battle Royal) leaves different
         * from a run to a singles match, but +0x0A of D_8007EF30, which seems to count the menus' frames. A singles
         * match keeps the values the boot gives them. */
        for (i = 0; i < 4; i++) {
            put(D_8007E228, 0x08 + 4 * i, i, 4); /* the slots in the match, in order (singles: 0, 1, -1, -1) */
            put(D_8007E8C0, 0x70 + 4 * i, i, 4); /* again (singles: 0, 1, 0, 0) */
            /* the menus' own record of each slot: its wrestler; its number, with its controller above; the damage
             * handicap; health and the five body parts at full */
            put(D_8007EF30, 0x4C + 0x10 * i, 0, 2);
            put(D_8007EF30, 0x4E + 0x10 * i, characters[i], 2);
            put(D_8007EF30, 0x50 + 0x10 * i, i | (controllers[i] > 0 ? controllers[i] << 8 : 0), 2);
            put(D_8007EF30, 0x52 + 0x10 * i, 5, 2);
            memcpy(D_8007EF30 + 0x54 + 0x10 * i, body, 6);
            put(D_8007EF30, 0x5A + 0x10 * i, 0, 2);
            put(D_8007EF30, 0xC90 + 2 * i, characters[i], 2); /* the battle royal's line-up */
        }
        put(D_8007EF30, 0x008, 0x0183, 2); /* the menus' match mode: 0x80 is the battle royal (func_800AF978) */
        put(D_8007EF30, 0x00C, 4, 4);      /* the number of wrestlers */
        put(D_8007EF30, 0x012, 0, 2);
        put(D_8007EF30, 0x01A, 4, 2);
        put(D_8007EF30, 0x1A8, 1, 1); /* the menus' run-ins choice: off */
        put(D_8007EF30, 0x1AA, 6, 1); /* the menus' choice that func_800AF978 copies to +0xCFC */
    }
}

/* The time limit, in words. */
static const char *time_limit_text(int time_limit, char *text, size_t size) {
    if (time_limit < 0) {
        return "no time limit";
    }
    snprintf(text, size, "%d minutes", time_limit == 0 ? 5 : time_limit);
    return text;
}

/* ---- The run's state ---- */

#define FRAME_CAP 20000          /* a match with a time limit that has not ended after this many frames has hung */
#define FRAME_CAP_UNTIMED 216000 /* one without: two hours of the game's time */
#define FRAMES_A_MINUTE 1800     /* the game's clock: 30 frames a second */
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

/* The memory report (src/dev/dev_memory.h): read every 300 frames. The heap one minute into the first match, when
 * the match has loaded what it starts with, is the base; a heap that keeps growing after it is a leak. */
#define MEMORY_EVERY 300
/* In a headless match the heap holds the same after its first minute (2026-11-26, Linux: 18,820 KiB at every reading
 * of the singles match, 18,813 KiB through the 10-minute battle royal), so a headless match fails when the heap holds
 * more than this above the base: a leak of 40 bytes a frame passes it within the 5-minute match, one of 20 bytes within
 * the 10-minute battle royal. A window's match only reports, since its windows (F1's menus, the inspector) take memory
 * as they open. */
#define LEAK_LIMIT (256u * 1024u)
static DevMemory g_memory;   /* the last reading */
static void *volatile g_leaked; /* the leak on purpose (leak_per_frame): only the last block, the others are lost */
static int g_have_base;
static uint64_t g_heap_base; /* the heap one minute into the first match */
static uint64_t g_heap_most; /* the most it held after that */
static uint32_t g_all_frames; /* the frames of every match */

static atomic_uint g_alive;     /* goes up at every sign of life, for the watchdog */
static atomic_int g_watch_stop; /* 1: the watchdog thread ends */

/* The frames after which a match has hung: FRAME_CAP, or for a time limit longer than that allows, the limit's frames
 * and 6000 more, for the match's start and its end. */
static uint32_t frame_cap(int time_limit) {
    uint32_t cap;

    if (time_limit < 0) {
        return FRAME_CAP_UNTIMED;
    }
    cap = (uint32_t)(time_limit == 0 ? 5 : time_limit) * FRAMES_A_MINUTE + 6000;
    return cap > FRAME_CAP ? cap : FRAME_CAP;
}

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

/* ---- The watchdog ---- */

static void watch(void) {
    unsigned last = atomic_load(&g_alive);
    int still = 0;

    while (!atomic_load(&g_watch_stop)) {
        unsigned now;

#ifdef _WIN32
        Sleep(1000);
#else
        struct timespec second = { 1, 0 };
        nanosleep(&second, NULL);
#endif
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
}

/* Starts the watchdog's thread (0 if it cannot), and waits for it to end once g_watch_stop is set. */
#ifdef _WIN32
static HANDLE g_dog;
static DWORD WINAPI watchdog(LPVOID arg) {
    (void)arg;
    watch();
    return 0;
}
static int watchdog_start(void) {
    g_dog = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    return g_dog != NULL;
}
static void watchdog_join(void) {
    WaitForSingleObject(g_dog, INFINITE);
    CloseHandle(g_dog);
}
#else
static pthread_t g_dog;
static void *watchdog(void *arg) {
    (void)arg;
    watch();
    return NULL;
}
static int watchdog_start(void) {
    return pthread_create(&g_dog, NULL, watchdog, NULL) == 0;
}
static void watchdog_join(void) {
    pthread_join(g_dog, NULL);
}
#endif

/* ---- What the developer match does at each hand-off ---- */

static void bot_frame(void);    /* the pad bot (the pads, below) */
static int g_last_phase;

static void on_present(int kind) {
    DevWrestler w[DEV_WRESTLERS];
    DevMatch m;

    dev_match_alive();
    if (kind != GAME_PRESENT_FRAME) {
        return;
    }
    g_frames++;
    g_all_frames++;
    if (g_options->leak_per_frame > 0) {
        g_leaked = malloc((size_t)g_options->leak_per_frame);
    }
    dev_records_read(w, &m);
    if (g_log != NULL) {
        dev_records_log_frame(g_log, g_frames, &m);
    }
    if (g_frames % MEMORY_EVERY == 0) {
        dev_memory_read(&g_memory);
        if (!g_have_base && g_frames >= FRAMES_A_MINUTE) {
            g_have_base = 1;
            g_heap_base = g_memory.heap;
        }
        if (g_have_base && g_memory.heap > g_heap_most) {
            g_heap_most = g_memory.heap;
        }
    }
    if (g_options->on_frame == NULL && g_frames % 300 == 0) {
        int k;

        printf("dev-match: frame %u, match frame %u, phase %d, health %d", g_frames, m.match_frame, (int)m.phase,
               w[0].health);
        for (k = 1; k < DEV_WRESTLERS; k++) {
            if (w[k].character != -1) {
                printf(" / %d", w[k].health);
            }
        }
        printf(", heap %llu KiB\n", (unsigned long long)(g_memory.heap / 1024));
        fflush(stdout);
    } else if (g_options->on_frame != NULL && g_frames % FRAMES_A_MINUTE == 0) {
        printf("dev-match: frame %u, heap %llu KiB, resident %llu KiB\n", g_frames,
               (unsigned long long)(g_memory.heap / 1024), (unsigned long long)(g_memory.resident / 1024));
        fflush(stdout);
    }

    if (m.phase > 4 && !g_failed) {
        fail("the phase went past 4, where the match never ends (two end checks in one frame)");
    }
    if (m.phase == 3 && g_last_phase == 2) {
        /* How the match ended, as the wrestlers stand when it does: a pinned wrestler, one counted out on the floor. */
        int k;

        printf("dev-match: the match is over at frame %u:", g_frames);
        for (k = 0; k < DEV_WRESTLERS; k++) {
            if (w[k].character != -1) {
                printf(" wrestler %d state %02X (%s), health %d, %s;", k, w[k].state, dev_state_name(w[k].state),
                       w[k].health, (w[k].flags & 0x40) ? "in the ring" : (w[k].flags & 0x80) ? "on the apron"
                                                                                               : "on the floor");
            }
        }
        printf("\n");
        fflush(stdout);
    }
    if (m.phase >= 4 && g_phase4_at == 0) {
        g_phase4_at = g_frames;
        printf("dev-match: the match ended at frame %u (match frame %u)\n", g_frames, m.match_frame);
        fflush(stdout);
    }
    /* The pause menu's restart runs the match again without loading overlay 2 (func_80090000_ovl2 loops while
     * D_800F99B0 is set): the phase goes back to the intro, while the match frame D_800E2FAC counts on. */
    if (m.phase < g_last_phase) {
        printf("dev-match: the match started again at frame %u, match frame %u (the pause menu's restart)\n", g_frames,
               m.match_frame);
        fflush(stdout);
        g_phase4_at = 0;
    }
    g_last_phase = m.phase;
    /* A at the results screen: pressed for one frame, released for one, so the game sees a new press. */
    g_press_a = g_phase4_at != 0 && g_frames - g_phase4_at > RESULTS_WAIT && (g_frames & 1) == 0;
    if (g_frames >= frame_cap(g_options->time_limit)) {
        port_fatal("dev-match: the match has not ended after %u frames (phase %d)", g_frames, (int)m.phase);
    }
    if (g_options->pad_script != NULL) {
        dev_pads_script_frame(g_frames);
    } else if (g_options->has_pad_bot) {
        bot_frame();
    }
    if (g_options->on_frame != NULL) {
        g_options->on_frame(g_options->user);
    }
}

/* ---- The pads ----
 * The pad files, the pad script and the window's controllers are dev_pads.c's; the pad bot is the match's own. */
static GamePad g_bot_pad;
static uint32_t g_bot_state, g_bot_hold;
static int g_replay_ended;

static uint32_t bot_random(void) {
    uint32_t x = g_bot_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_bot_state = x;
    return x;
}

/* The pad bot, once a frame: the stick held in one of eight directions (or let go) for 10 to 40 frames at a time, and
 * now and then a press of A, B, R, Z, L or a C button for a frame or a few. It never presses Start, which pauses. */
static void bot_frame(void) {
    static const uint16_t presses[] = { 0x8000, 0x8000, 0x8000, 0x4000, 0x4000, 0x4000, 0x0010, 0x2000, 0x0020,
                                        0x0008, 0x0004, 0x0002, 0x0001 };
    static const int8_t dirs[9][2] = { { 0, 0 }, { 0, 80 }, { 56, 56 }, { 80, 0 }, { 56, -56 },
                                       { 0, -80 }, { -56, -56 }, { -80, 0 }, { -56, 56 } };

    if (g_bot_hold == 0) {
        unsigned d = bot_random() % 9;

        g_bot_pad.stick_x = dirs[d][0];
        g_bot_pad.stick_y = dirs[d][1];
        g_bot_hold = 10 + bot_random() % 31;
    }
    g_bot_hold--;
    g_bot_pad.buttons = 0;
    if (bot_random() % 12 == 0) {
        g_bot_pad.buttons = presses[bot_random() % (sizeof(presses) / sizeof(presses[0]))];
    }
}

static void pads_open(void) {
    dev_pads_open("dev-match", g_options->replay_pads, g_options->record_pads, g_options->pad_script);
    g_bot_state = g_options->pad_bot_seed != 0 ? g_options->pad_bot_seed : 1;
}

/* The pads at the start of a match: where the replay is, and the pad bot's seed; pads_rewind goes back there. */
static long g_replay_at_match;

static void pads_mark(void) {
    g_replay_at_match = dev_pads_tell();
}

static void pads_rewind(void) {
    dev_pads_seek(g_replay_at_match);
    g_bot_state = g_options->pad_bot_seed != 0 ? g_options->pad_bot_seed : 1;
    g_bot_hold = 0;
    memset(&g_bot_pad, 0, sizeof(g_bot_pad));
}

static void pads_close(void) {
    dev_pads_close();
}

static void read_pad(int channel, GamePad *pad) {
    memset(pad, 0, sizeof(*pad));
    if (dev_pads_replaying()) {
        if (!dev_pads_replay(channel, pad) && !g_replay_ended) {
            g_replay_ended = 1;
            fail("the pad file ended before the game stopped reading pads");
        }
    } else if (g_options->players > 0 && g_options->pad_script == NULL && !g_options->has_pad_bot) {
        dev_pads_window(channel, pad);
    } else if (channel == 0) {
        /* The script or the bot plays controller 1 (nobody plays CPU against CPU), and the results screen gets its A
         * as a player would press it. */
        if (g_options->players > 0) {
            if (g_options->pad_script != NULL) {
                dev_pads_script(pad);
            } else {
                *pad = g_bot_pad;
            }
        }
        if (g_press_a) {
            pad->buttons |= 0x8000; /* A */
        }
    }
    dev_pads_record(g_frames, channel, pad);
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

/* The draw switches: the groups to hide, applied only while the port builds a frame (dev_records.h). */
static uint32_t g_hide_groups;

void dev_match_hide_groups(uint32_t mask) {
    g_hide_groups = mask;
}

uint32_t dev_match_hidden_groups(void) {
    return g_hide_groups;
}

static void before_render(void) {
    if (g_hide_groups != 0) {
        dev_records_hide_groups(g_hide_groups);
    }
}

static void after_render(void) {
    dev_records_unhide_groups();
}

/* The report at the end: how long the matches took for their frames, and the memory. */
static void report_memory(const struct timespec *started, const struct timespec *ended) {
    double seconds = (double)(ended->tv_sec - started->tv_sec) + (double)(ended->tv_nsec - started->tv_nsec) / 1e9;
    DevMemory now;

    dev_memory_read(&now);
    printf("dev-match: %u frames in %.2f s, %.0f microseconds a frame\n", g_all_frames, seconds,
           g_all_frames > 0 ? seconds * 1e6 / g_all_frames : 0.0);
    if (g_have_base) {
        printf("dev-match: memory: the heap held %llu KiB one minute into the match and %llu KiB at the end (%+lld "
               "KiB), at most %llu KiB; the program's resident memory reached %llu KiB\n",
               (unsigned long long)(g_heap_base / 1024), (unsigned long long)(now.heap / 1024),
               ((long long)now.heap - (long long)g_heap_base) / 1024, (unsigned long long)(g_heap_most / 1024),
               (unsigned long long)(now.resident_peak / 1024));
    } else {
        printf("dev-match: memory: the match ended within a minute; the heap held %llu KiB at the end, the program's "
               "resident memory reached %llu KiB\n",
               (unsigned long long)(now.heap / 1024), (unsigned long long)(now.resident_peak / 1024));
    }
    fflush(stdout);
    if (g_options->on_frame == NULL && g_have_base && g_heap_most > g_heap_base + LEAK_LIMIT) {
        char what[160];

        snprintf(what, sizeof(what), "the heap grew by %llu KiB after the match's first minute: memory taken and "
                                     "never given back (a leak)",
                 (unsigned long long)((g_heap_most - g_heap_base) / 1024));
        fail(what);
    }
}

int dev_match_run(const Rom *rom, const DevMatchOptions *options) {
    static const GameSdkHooks hooks = { on_present, read_pad, before_render, after_render };
    static const DevMatchOptions none = { NULL, NULL, 0, 0, 0, NULL, NULL, 0, 0, 0, NULL, NULL, 0, 0, NULL, 0, 0, 0, 0 };
    unsigned i;
    uint32_t seed;
    int match;
    struct timespec started, ended;

    g_options = options != NULL ? options : &none;
    g_hide_groups = g_options->hide_groups;
    game_sdk_set_idle_randoms(g_options->no_idle_randoms ? 0 : 1);
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
        g_log = dev_records_log_open(g_options->wr_log);
        if (g_log == NULL) {
            port_fatal("dev-match: cannot write the wrestler log %s", g_options->wr_log);
        }
    }
    pads_open();
    if (g_options->res_log != NULL) {
        g_res_log = fopen(g_options->res_log, "w");
        if (g_res_log == NULL) {
            port_fatal("dev-match: cannot write the resource log %s", g_options->res_log);
        }
    }
    if (!g_options->no_watchdog && !watchdog_start()) {
        port_fatal("dev-match: cannot start the watchdog thread");
    }

    printf("dev-match: boot\n");
    func_80001260();
    func_80003120();
    func_80003D20();
    initializeAssetSizeTable();
    func_8002B750();
    write_settings(g_options);

    {
        char limit[32];

        printf("dev-match: %s, %s, arena 0, %s\n",
               g_options->battle_royal ? "battle royal of Sting, Hollywood Hogan, Giant and Ultimo Dragon, each on "
                                         "their own"
                                       : "Sting vs Hollywood Hogan",
               g_options->players > 0 ? "Sting played on controller 1" : "CPU vs CPU",
               time_limit_text(g_options->time_limit, limit, sizeof(limit)));
    }
    fflush(stdout);
    seed = xseed;
    pads_mark();
    timespec_get(&started, TIME_UTC);
    for (match = 1; match <= (g_options->matches > 1 ? g_options->matches : 1); match++) {
        if (match > 1) {
            /* As the N64's boot loop before each match: overlay 2 afresh (func_80000700); then the menus' settings,
             * which the last match changed (it writes the end health into them), and the first match's seed and
             * pads, so that this match plays as the first. */
            game_vars_load(2);
            write_settings(g_options);
            xseed = seed;
            pads_rewind();
            g_frames = 0;
            g_phase4_at = 0;
            g_press_a = 0;
            g_last_phase = 0;
            if (g_log != NULL) {
                char path[1024];

                fclose(g_log);
                snprintf(path, sizeof(path), "%s.%d", g_options->wr_log, match);
                g_log = dev_records_log_open(path);
                if (g_log == NULL) {
                    port_fatal("dev-match: cannot write the wrestler log %s", path);
                }
            }
            printf("dev-match: match %d\n", match);
            fflush(stdout);
        }
        g_in_match = 1;
        func_80090000_ovl2();
        g_in_match = 0;
        if (g_phase4_at == 0) {
            fail("the match returned before phase 4");
        }
    }
    timespec_get(&ended, TIME_UTC);
    report_memory(&started, &ended);

    if (!g_options->no_watchdog) {
        atomic_store(&g_watch_stop, 1);
        watchdog_join();
    }
    if (g_log != NULL) {
        fclose(g_log);
        g_log = NULL;
    }
    if (g_res_log != NULL) {
        fclose(g_res_log);
        g_res_log = NULL;
    }
    pads_close();
    port_res_hook(NULL);
    game_sdk_set(NULL, 0x1);
    rom_attach(NULL);

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
