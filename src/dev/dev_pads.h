/* The controllers of the developer runs (src/dev/dev_pads.c): where the developer match (--dev-match) and the
 * developer boot (--dev-boot) get the pads the game reads. C and C++.
 *
 * The sources, which each run puts in its own order of precedence:
 * - a pad file that --record-pads wrote (--replay-pads), read back one entry per pad read;
 * - a pad script (--pad-script): lines "FRAME BUTTONS [FRAMES]" press BUTTONS (names joined with +: A, B, Z, START,
 *   L, R, DUP, DDOWN, DLEFT, DRIGHT, CUP, CDOWN, CLEFT, CRIGHT) on controller 1 from frame FRAME for FRAMES frames (1
 *   if left out); # starts a comment;
 * - the window's controllers, which the Match Inspector sets at every frame from LibUltraShip's control deck.
 * A pad file can also be written as the game reads (--record-pads), whatever the source. */
#ifndef TD_DEV_PADS_H
#define TD_DEV_PADS_H

#include <stdint.h>

#include "game_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the pad file to replay, the pad file to record and the pad script; any may be NULL. `who` starts the error
 * messages ("dev-match", "dev-boot"). A file that cannot be read or written stops the program. */
void dev_pads_open(const char *who, const char *replay, const char *record, const char *script);
/* Closes the pad files. */
void dev_pads_close(void);

/* 1 when a pad file is being replayed, or a pad script was given. */
int dev_pads_replaying(void);
int dev_pads_scripted(void);

/* The next entry of the replayed file, which must be a read of `channel`: 1 with *pad set, 0 when the file has ended
 * (*pad released). */
int dev_pads_replay(int channel, GamePad *pad);
/* The replay's position, kept and gone back to (the developer match plays each of its matches from the same pads). */
long dev_pads_tell(void);
void dev_pads_seek(long at);

/* Works out controller 1's pad from the script for frame `frame`, and gives it. */
void dev_pads_script_frame(uint32_t frame);
void dev_pads_script(GamePad *pad);

/* The window's controller `channel` (0 to 3), as dev_pads_set_window last set it. */
void dev_pads_window(int channel, GamePad *pad);
/* The state of controller `channel` (0 to 3) for the game's next pad reads: N64 button bits (A 0x8000, B 0x4000, Z
 * 0x2000, Start 0x1000, the d-pad 0x0F00, L 0x20, R 0x10, the C buttons 0x0F) and the stick, -80 to 80 each way. The
 * Match Inspector's window sets it from LibUltraShip's controllers at every frame. */
void dev_pads_set_window(int channel, uint16_t buttons, int8_t stick_x, int8_t stick_y);

/* Writes one pad read to the recorded file, if there is one: "TDPADS02", then 10 bytes per read, little-endian: u32
 * frame, u8 channel, u8 0, u16 buttons, s8 stick x, s8 stick y. */
void dev_pads_record(uint32_t frame, int channel, const GamePad *pad);

#ifdef __cplusplus
}
#endif

#endif
