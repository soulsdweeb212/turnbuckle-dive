/* The developer boot (turnbuckle-dive --dev-boot FRAMES): starts the game as the N64 does, from its boot loop, and
 * stops after FRAMES frames (src/dev/dev_boot.c). C and C++. */
#ifndef TD_DEV_BOOT_H
#define TD_DEV_BOOT_H

#include <stdint.h>

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the game from its boot loop (func_80013108) for `frames` frame hand-offs, then ends the program with exit code
 * 0. on_frame, when not NULL, is called with user after every frame the game hands over (the Match Inspector's window,
 * dev_inspector_frame, which draws it); NULL for a run without a window. It returns only when the boot loop does,
 * which the N64's never does: then it returns 1. */
int dev_boot_run(const Rom *rom, uint32_t frames, void (*on_frame)(void *user), void *user);
/* Where controller 1 comes from in the boot (src/dev/dev_pads.h), before dev_boot_run: a pad file to replay, else a
 * pad script, else the window's controllers when there is a window; with none, the controller reads as released.
 * record, when not NULL, saves every pad read. Each may be NULL. A script's frames are the boot's frames. */
void dev_boot_pads(const char *replay, const char *record, const char *script);
/* With no_idle_randoms 1, no random numbers are drawn in place of the N64's idle thread (game_sdk_set_idle_randoms), to
 * compare with the original game run with its idle thread's random calls turned off, as the developer match's
 * --no-idle-randoms does. Before dev_boot_run. */
void dev_boot_idle_randoms(int no_idle_randoms);
/* The frames the game has handed over since the boot began. */
uint32_t dev_boot_frames(void);
/* The wrestler log (src/dev/dev_records.h) to FILE while the match is loaded, and a function to call before the program
 * ends at the last frame. Before dev_boot_run. */
void dev_boot_wr_log(const char *path);
void dev_boot_at_end(void (*at_end)(void));

#ifdef __cplusplus
}
#endif

#endif
