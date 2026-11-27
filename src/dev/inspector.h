/* The Match Inspector (src/dev/inspector.cpp): the developer match's window. C and C++. */
#ifndef TD_INSPECTOR_H
#define TD_INSPECTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the window (a LibUltraShip window with the Match Inspector). Returns 0, or -1 when no window can be opened
 * (no screen); it then prints why, and the match can run without it. */
int dev_inspector_start(void);

/* DevMatchOptions.on_frame (src/dev/dev_match.h): draws the window for one game frame, paced to 30 frames a second, and
 * holds the game while the inspector is paused. Ends the program when the window is closed. */
void dev_inspector_frame(void *user);

/* Shows the splash screen (src/splash.h) in the window before the game boots: the cube with the faces that
 * splash_faces_load made (src/splash_faces.h). Returns when it is over; ends the program when the window is closed. */
void dev_inspector_splash(const uint8_t *faces);

/* Closes the window. */
void dev_inspector_stop(void);

/* How the game boots, the F1 menu's Settings, Boot (saved in the settings file; call it after dev_inspector_start):
 * ORIGINAL, as the N64, with the game's legal screen; TURNBUCKLE_DIVE, the splash screen in place of the legal screen. */
#define DEV_BOOT_SCREEN_ORIGINAL 0
#define DEV_BOOT_SCREEN_TURNBUCKLE_DIVE 1
int dev_inspector_boot_screen(void);

/* Keeps the game's save memory in turnbuckle-dive.sram beside the program (game_sdk_sram_file); call it after
 * dev_inspector_start and before the game starts. */
void dev_inspector_use_save_file(void);

/* Hands the sound (src/audio/sound.h, started) to the window's sound device. Returns 0, or -1 when there is none. */
int dev_inspector_open_sound(void);

/* Leaves the game frames before `until` (by the frame count of dev_inspector_frames_from) out of the window: the game
 * runs them at once, without drawing or pacing them, and reads no controller during them. */
void dev_inspector_skip_frames(uint32_t until);

/* The program's command line, which the F1 menu's Reset starts again: the window closes, and the program starts over
 * with the same options, from the boot as after a console's reset button (src/dev/inspector.cpp, Restart). Call it
 * first in main. */
void dev_inspector_set_command(int argc, char **argv);
/* The game frame count that the screenshots below go by: by default the developer match's (dev_match_frames); the
 * developer boot hands over its own. */
void dev_inspector_frames_from(uint32_t (*frames)(void));

/* Saves the window, as drawn at game frame `frame`, to a PNG file at path (OpenGL only, the default on Linux). The
 * picture shows the match's state, made from the user's ROM: keep it out of the repository. */
void dev_inspector_screenshot(const char *path, unsigned frame);

/* Saves the window every `every` game frames, from frame `every` on, to PNG files named prefix-NNNNN.png (NNNNN the
 * frame). As dev_inspector_screenshot, OpenGL only, and the pictures stay out of the repository. */
void dev_inspector_screenshots(const char *prefix, unsigned every);

/* At each match frame (D_800E2FAC) in frames[0..count), saves the window as DIR/port_NNNNN.png and the frame's display
 * list as DIR/port_NNNNN.txt (NNNNN the match frame), as Project64's td_lockstep.js does for the original game. */
void dev_inspector_capture(const char *dir, const unsigned *frames, unsigned count);

/* Writes every command of game frame `frame`'s display list, nested lists included, to a text file at path (frame.h's
 * DumpDisplayList): the address, the two words and the command's name, indented by depth. */
void dev_inspector_dump(const char *path, unsigned frame);

#ifdef __cplusplus
}
#endif

#endif
