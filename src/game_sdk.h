/* The port's versions of the N64 SDK functions that the game's files call (src/game_sdk.c), and the hooks through which
 * a program that runs the game gives them its input and sees its frames. C and C++. */
#ifndef TD_GAME_SDK_H
#define TD_GAME_SDK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
#include "game.h" /* port_present and its kinds, GAME_PRESENT_... (C only: game.h is a C header) */
#endif

/* One controller's state as the game reads it: the N64's button bits and stick. */
typedef struct {
    uint16_t buttons;
    int8_t stick_x;
    int8_t stick_y;
} GamePad;

/* What a program that runs the game provides. Every member may be NULL. */
typedef struct {
    /* Called at each hand-off the game makes (port_present), with its kind. */
    void (*present)(int kind);
    /* Fills the state of controller `channel` (0 to 3) for the read the game is making. */
    void (*read_pad)(int channel, GamePad *pad);
    /* Called just before and just after the game's render work builds a frame's display list (port_render_frame),
     * while the game waits at the frame's hand-off (the developer match's draw switches). */
    void (*before_render)(void);
    void (*after_render)(void);
} GameSdkHooks;

/* Sets the hooks, and which controllers are plugged in: bit n for controller n + 1. */
void game_sdk_set(const GameSdkHooks *hooks, unsigned plugged);

/* How many random numbers (guRandom) each frame hand-off and each wait for the retrace draws, in place of the N64's
 * idle thread: 1 by default, 0 to compare with the original game run without its idle thread's calls (docs/GAME_CODE.md,
 * "Comparing with the original game"). */
void game_sdk_set_idle_randoms(int count);

/* How many times the game has handed over a frame (GAME_PRESENT_FRAME) since the program started. */
uint32_t game_sdk_frames(void);

/* The display list of the last frame the game handed over: the commands of every task the frame's render work
 * submitted, one task after another, ending with G_ENDDL; *count is the number of commands (0 when the hand-off drew
 * nothing) and *tasks the number of tasks. The list stays valid, and what it points to unchanged, until the game
 * runs on after the hand-off; a present hook can draw it as often as it likes. */
const void *game_sdk_frame(uint32_t *count, uint32_t *tasks);

#ifdef __cplusplus
}
#endif

#endif
