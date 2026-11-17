/* The port's sound: the game's two sound banks, the synthesizer that plays them and the music player that drives it,
 * set up from the ROM, and the hand-off of the synthesizer's output to the sound device (src/audio/sound.c). C and C++.
 * The game's calls to its music library (src/game_sdk.c) go to the music player. */
#ifndef TD_SOUND_H
#define TD_SOUND_H

#include <stddef.h>
#include <stdint.h>

#include "music.h"
#include "sound_bank.h"
#include "synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOUND_BANK_MUSIC 0
#define SOUND_BANK_EFFECTS 1

/* Reads the banks, the pan gains and the game's reverb (the big room) from rom, which must stay loaded, and sets up the
 * synthesizer. Returns 0, or -1 with a message on stderr. */
int sound_start(const uint8_t *rom, size_t rom_size);
void sound_end(void);

/* NULL before sound_start. */
const SoundBank *sound_bank(int which);
Synth *sound_synth(void);
Music *sound_music(void);

/* The sound device's side: `feed` is called at each of the game's frames (port_present), to hand the device as much
 * output as it wants (src/audio/sound_output.cpp). */
void sound_set_output(void (*feed)(void));
void sound_present(void);

/* Someone watching the game's calls to its music library (src/game_sdk.c), as --sound-lockstep does
 * (src/audio/sound_replay.c): `call` before each call, with the library's name for it and its arguments (for
 * MusStartSong, the song); `answer` after each question (MusAsk, MusHandleAsk) with the answer; `hand_off` at each frame
 * the game hands over (port_present's GAME_PRESENT_FRAME). NULL stops watching. */
typedef struct {
    void (*call)(void *context, const char *name, int count, const int32_t *args, const void *song);
    void (*answer)(void *context, int32_t value);
    void (*hand_off)(void *context);
    void *context;
} SoundWatch;

void sound_watch(const SoundWatch *watch);
void sound_watch_call(const char *name, int count, const int32_t *args, const void *song);
void sound_watch_answer(int32_t value);
void sound_watch_hand_off(void);

/* The game's reverb, read from the ROM: the big room (fxType 2), from its audio library's parameter table. Returns 0,
 * or -1 when the table is not one the synthesizer can run. Used by sound_start; here for the tests. */
int sound_read_reverb(const uint8_t *rom, size_t rom_size, SynthReverb *reverb);
int sound_read_eqpower(const uint8_t *rom, size_t rom_size, int16_t eqpower[128]);
int sound_read_resample(const uint8_t *rom, size_t rom_size, int16_t resample[256]);

#ifdef __cplusplus
}
#endif

#endif
