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

#ifdef __cplusplus
}
#endif

#endif
