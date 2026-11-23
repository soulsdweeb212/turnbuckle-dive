/* The player's launch: where the game's data comes from when the program is started to play rather than for a
 * developer mode. C and C++.
 *
 * The port runs from the game's archive (src/archive.h), which it writes itself, once, from the player's ROM, into the
 * folder it keeps its files in: LibUltraShip's app folder (Ship::Context::GetAppDirectoryPath), which is the folder
 * that holds the program, or $SHIP_HOME on Linux (an AppImage's launcher sets it, since the AppImage's own folder can
 * never be written to). The port's log, settings and saves are kept there too.
 *
 * 1. With a ROM given (the first argument, or $TD_ROM): that ROM is loaded and checked. The archive is written from it
 *    when the folder has none, or has one that cannot be used (written in another format, from another ROM, or
 *    damaged), and the game then runs from the archive.
 * 2. With no ROM given: the archive in the folder, when it can be used. If not, the port looks for the ROM, under any
 *    name ending in .z64, .n64 or .v64, in the folder, in the program's folder and in the current folder, then (builds
 *    made beside the decompilation) the decompilation's baserom.z64; the first one that is the supported ROM is used as
 *    in 1.
 * 3. When the archive cannot be written (a folder the port cannot write to, a full disk), the game runs from the ROM
 *    this time, and the port says why. */
#ifndef TD_FIRST_LAUNCH_H
#define TD_FIRST_LAUNCH_H

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the game's data as above, into rom. rom_arg is the ROM given on the command line, or NULL. Returns:
 * - ROM_OK: rom is the archive's ROM image (rom->sparse is set), or the ROM itself when the archive could not be
 *   written; *shown is the file it came from, to show the player.
 * - ROM_WRONG_HASH: the ROM given is not the supported one; rom holds it, for the caller to report, and *shown is it.
 * - any other status: no data. *shown is the ROM given, for the caller to report, or NULL when no ROM was given and
 *   none was found; first_launch_open has then said where to put the ROM. */
RomStatus first_launch_open(const char *rom_arg, Rom *rom, const char **shown);

/* A player who starts the program by double-clicking it has no terminal to read its messages in. With
 * first_launch_boxes(1) (the player's launch without --headless), first_launch_tell also shows its text in a message
 * box; first_launch_open tells "No ROM found" this way, and src/main.c the other problems of the player's launch.
 * first_launch_tell always writes the text to stderr. */
void first_launch_boxes(int on);
void first_launch_tell(const char *text);

#ifdef __cplusplus
}
#endif

#endif
