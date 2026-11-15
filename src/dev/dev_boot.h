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
/* The frames the game has handed over since the boot began. */
uint32_t dev_boot_frames(void);

#ifdef __cplusplus
}
#endif

#endif
