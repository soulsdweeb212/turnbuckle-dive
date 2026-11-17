/* --sound-replay DIR: the port's music player made to play what the original game asked of its own, from a recording
 * made in Project64 (src/audio/sound_replay.c; docs/GAME_CODE.md, "Comparing the sound with the original"). C and C++.
 */
#ifndef TD_SOUND_REPLAY_H
#define TD_SOUND_REPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads DIR/calls.txt and the songs DIR/song_F.bin, plays them, and writes DIR/port_calls.txt (the player's calls to
 * the synthesizer, in the recording's form) and DIR/port_audio.raw (the sound, 16-bit stereo, little-endian, 28800 Hz).
 * Returns 0, or 1 with a message. */
int sound_replay(const uint8_t *rom, size_t rom_size, const char *dir);

#ifdef __cplusplus
}
#endif

#endif
