/* The port's versions of the N64 SDK functions that the game's files call (src/game_sdk.c), and the hooks through which
 * a program that runs the game gives them its input and sees its frames. C only. */
#ifndef TD_GAME_SDK_H
#define TD_GAME_SDK_H

#include <stdint.h>

#include "game.h" /* port_present and its kinds, GAME_PRESENT_... */

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
} GameSdkHooks;

/* Sets the hooks, and which controllers are plugged in: bit n for controller n + 1. */
void game_sdk_set(const GameSdkHooks *hooks, unsigned plugged);

/* How many times the game has handed over a frame (GAME_PRESENT_FRAME) since the program started. */
uint32_t game_sdk_frames(void);

#endif
