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

/* --sound-lockstep DIR, with the developer match or the developer boot: DIR holds a recording of the same match
 * (td_sound.js with td_lockstep.js) or the same boot (td_sound.js with td_route.js). Starts the sound, makes it up to
 * the match's start (for a boot, the start), and then watches the game's calls to its music
 * library (sound_watch), making the sound on demand so that each call meets the player at the frame where the
 * original's did. Writes DIR/port_calls.txt (the game's calls and the answers, the hand-offs, and the player's calls to
 * the synthesizer), DIR/port_audio.raw and DIR/port_song_F.bin. Returns 0, or 1 with a message. */
int sound_lockstep_start(const uint8_t *rom, size_t rom_size, const char *dir);
/* Stops watching, prints how far the port's game followed the recording, closes the files and stops the sound. */
void sound_lockstep_end(void);

#ifdef __cplusplus
}
#endif

#endif
