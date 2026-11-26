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
/* Changes which controllers are plugged in while the game runs (a game pad plugged in or taken out); the game sees it
 * at its next controller read. */
void game_sdk_set_plugged(unsigned plugged);
/* Which plugged-in controllers have a Rumble Pak (bit n for controller n + 1; none at first), and the function that
 * starts (on 1) or stops (0) controller `channel`'s motor when the game asks (NULL: none). A pak taken out while its
 * motor runs is stopped. */
void game_sdk_set_rumble_paks(unsigned paks);
void game_sdk_set_rumble(void (*rumble)(int channel, int on));

/* How many random numbers (guRandom) each frame hand-off and each wait for the retrace draws, in place of the N64's
 * idle thread: 1 by default, 0 to compare with the original game run without its idle thread's calls (docs/GAME_CODE.md,
 * "Comparing with the original game"). */
void game_sdk_set_idle_randoms(int count);

/* How many matches a championship run has, 1 to 9 (other values are clamped); 9, the original game's, by default. The
 * game reads it (port_championship_matches) when a run starts, and starts the run that many matches before its last,
 * the title match: a shorter run leaves out the first contenders. */
void game_sdk_set_championship_matches(int matches);

/* The Sound Player (src/dev/sound_player.h) taking the songs from the game. While game_sdk_hold_songs(1) is in force,
 * the game's own calls cannot start a song (MusStartSong gives it handle 0, nothing) or stop the held one (MusHandleStop
 * of game_sdk_held_song(), and MusStop's song class), so the song the player starts keeps playing while the screens go
 * on asking for theirs; game_sdk_hold_songs(0) gives the songs back. The player's own calls are made between
 * game_sdk_player_calls(1) and (0), and the first song they start is the held one. */
void game_sdk_hold_songs(int hold);
void game_sdk_player_calls(int on);
uint32_t game_sdk_held_song(void);

/* The player's own costume colours. Each wrestler (the game's character id, bank << 8 | entry + 1) may have its own
 * colour, 0xRRGGBB, for colour 1 and for colour 2 of its costume, in place of the colour the game's Costume Change menu
 * chose; -1 means the game's own. The game reads them (port_costume_ramp) each time it colours a palette, so a change
 * shows on a wrestler already on screen only once the wrestler's palettes are coloured again (dev_records_recolour).
 * The palettes hold 15-bit colours, so a colour is rounded to 5 bits a component. */
void game_sdk_set_costume_colour(unsigned character, int colour, int32_t rgb);
int32_t game_sdk_costume_colour(unsigned character, int colour);

/* Who hears of each animation the game starts (port_anim_started, which the game's func_800D427C calls when it changes
 * a wrestler's animation): slot 0 to 3, the new and the previous animation id, and the caller's mode. NULL, the
 * default, hears nothing. The Player Console (src/dev/player_console.cpp) sets it. */
void game_sdk_set_anim_hook(void (*hook)(int slot, int anim, int previous, int mode));

/* How many times the game has handed over a frame (GAME_PRESENT_FRAME) since the program started. */
uint32_t game_sdk_frames(void);

/* The save memory, the cartridge's SRAM as the game reads and writes it: GAME_SDK_SRAM_SIZE bytes, all zeros at the
 * start. *writes (when not NULL) gets how many times the game has written to it so far, so a program can tell when to
 * save it to a file; it may also fill it from one before the game starts. */
#define GAME_SDK_SRAM_SIZE 0x8000u
uint8_t *game_sdk_sram(uint32_t *writes);

/* Keeps the save memory in a file: fills it from the file at path when there is one (a file of another size than
 * GAME_SDK_SRAM_SIZE is left alone, with a message), and from then on writes the whole save memory to the file after
 * each write the game makes to it, so the game's saves (records, rankings, title holders, costume choices) outlast the
 * program. Call it before the game starts. The file holds the save memory as the PC's game writes it. Returns 1 when
 * it filled the save memory from the file, 0 when there was no file to read, -1 on an error. */
int game_sdk_sram_file(const char *path);

/* The display list of the last frame the game handed over: the commands of every task the frame's render work
 * submitted, one task after another, ending with G_ENDDL; *count is the number of commands (0 when the hand-off drew
 * nothing) and *tasks the number of tasks. The list stays valid, and what it points to unchanged, until the game
 * runs on after the hand-off; a present hook can draw it as often as it likes. */
const void *game_sdk_frame(uint32_t *count, uint32_t *tasks);

/* The addresses of the game's three framebuffers (D_8002EEB0, game.h): its frames are drawn into them in turn, and its
 * monitors read the one the frame before was drawn into, which the window fills with that frame's picture
 * (src/screen_copy.h). */
void game_sdk_framebuffers(uint8_t *out[3]);

#ifdef __cplusplus
}
#endif

#endif
