/* The Match Inspector (src/dev/inspector.cpp): the developer match's window. C and C++. */
#ifndef TD_INSPECTOR_H
#define TD_INSPECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the window (a LibUltraShip window with the Match Inspector). Returns 0, or -1 when no window can be opened
 * (no screen); it then prints why, and the match can run without it. */
int dev_inspector_start(void);

/* DevMatchOptions.on_frame (src/dev/dev_match.h): draws the window for one game frame, paced to 30 frames a second, and
 * holds the game while the inspector is paused. Ends the program when the window is closed. */
void dev_inspector_frame(void *user);

/* Closes the window. */
void dev_inspector_stop(void);

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
