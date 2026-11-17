/* --music-dump DIR: each of the game's songs played by the port's music player, written to DIR as a stereo WAV file
 * (src/audio/music_dump.c). C and C++. Only in builds with game code (it loads the songs with the game's loader). */
#ifndef TD_MUSIC_DUMP_H
#define TD_MUSIC_DUMP_H

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes DIR/song_NN.wav for each song number NN the game plays (1 to 21; 0 is silence), `seconds` long or until the
 * song ends, and DIR/songs.txt listing them. Returns 0, or 1 when a file could not be written. */
int music_dump(const Rom *rom, const char *dir, int seconds);

#ifdef __cplusplus
}
#endif

#endif
