/* The developer match (src/dev/dev_match.c). C and C++. */
#ifndef TD_DEV_MATCH_H
#define TD_DEV_MATCH_H

#include <stdint.h>

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* A file to write the wrestler log to (tools/wr_log.py reads it), or NULL for none. */
    const char *wr_log;
    /* A text file to list the resources the game loads in, one line each with the frame, or NULL for none. */
    const char *res_log;
    /* 1: no watchdog (a debugger stopped at a breakpoint for longer is fine without it as well). */
    int no_watchdog;
    /* 1: start guRandom's seed (xseed) at `seed` instead of the ROM's value, for a different match. */
    int has_seed;
    uint32_t seed;
    /* Called after every frame the game hands over, while the game waits; it may take its time (a window that is
     * paused keeps drawing in it), but must call dev_match_alive() at least once a second while it does. NULL for a
     * headless run, which runs as fast as it can. */
    void (*on_frame)(void *user);
    void *user;
    /* The draw groups not to draw from the start, bit n for group n (dev_records_hide_groups); 0 for none. */
    uint32_t hide_groups;
    /* 1: no random numbers drawn in place of the N64's idle thread (game_sdk_set_idle_randoms), to compare with the
     * original game run by Project64's td_lockstep.js, which turns the idle thread's calls off. */
    int no_idle_randoms;
    /* 1: wrestler slot 0 (Sting) is played on controller 1, against the CPU; 0: CPU against CPU. */
    int players;
    /* Where a player's controller comes from: the pads of a file that --record-pads wrote (replay_pads), else the pad
     * script below, else the pad bot when has_pad_bot (button presses and stick moves drawn from pad_bot_seed, never Start), else the window's
     * controllers (dev_match_set_pad). record_pads, when not NULL, saves every pad read, for a replay. */
    const char *replay_pads;
    const char *record_pads;
    int has_pad_bot;
    uint32_t pad_bot_seed;
    /* A pad script (--pad-script), which comes before the bot and the window: lines "FRAME BUTTONS [FRAMES]" press
     * BUTTONS (names joined with +: A, B, Z, START, L, R, DUP, DDOWN, DLEFT, DRIGHT, CUP, CDOWN, CLEFT, CRIGHT) on
     * controller 1 from that frame of the match for FRAMES frames (1 if left out); # starts a comment. */
    const char *pad_script;
    /* How many matches to play one after the other (0 is 1). Before each one after the first, overlay 2 is loaded
     * afresh, as the N64's boot loop does, the settings are written again, guRandom's seed goes back to its value at
     * the first match's start, and the pads start again from that match's first read; so each match plays as the
     * first. Match n's wrestler log is wr_log with ".n" after it. */
    int matches;
    /* The time limit: 0 for the default, 5 minutes; -1 for none (the match then ends only by a fall, a count-out and
     * the like); otherwise 10, 15, 30 or 60 minutes. */
    int time_limit;
} DevMatchOptions;

/* Runs one CPU-vs-CPU match on the checked ROM and checks the G6 gate (docs/GAME_CODE.md, "The developer match"):
 * 0 when the match ended in phase 4 and every check passed, 1 when one failed (it says which). */
int dev_match_run(const Rom *rom, const DevMatchOptions *options);

/* Tells the watchdog the program is alive while on_frame keeps the game waiting. */
void dev_match_alive(void);

/* The draw groups not drawn from the next frame on, bit n for group n (the Match Inspector's switches), and those
 * hidden now. Only the picture changes: the game's code never sees it. */
void dev_match_hide_groups(uint32_t mask);
uint32_t dev_match_hidden_groups(void);

/* The state of controller `channel` (0 to 3) for the game's next pad reads: N64 button bits (A 0x8000, B 0x4000, Z
 * 0x2000, Start 0x1000, the d-pad 0x0F00, L 0x20, R 0x10, the C buttons 0x0F) and the stick, -80 to 80 each way. The
 * Match Inspector's window sets it from LibUltraShip's controllers at every frame. */
void dev_match_set_pad(int channel, uint16_t buttons, int8_t stick_x, int8_t stick_y);

/* How many frames the game has handed over since the match started. */
uint32_t dev_match_frames(void);

#ifdef __cplusplus
}
#endif

#endif
