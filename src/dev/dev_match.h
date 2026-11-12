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

/* How many frames the game has handed over since the match started. */
uint32_t dev_match_frames(void);

#ifdef __cplusplus
}
#endif

#endif
