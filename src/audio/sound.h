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

/* The game's reverb, read from the ROM: the big room (fxType 2), from its audio library's parameter table. Returns 0,
 * or -1 when the table is not one the synthesizer can run. Used by sound_start; here for the tests. */
int sound_read_reverb(const uint8_t *rom, size_t rom_size, SynthReverb *reverb);
int sound_read_eqpower(const uint8_t *rom, size_t rom_size, int16_t eqpower[128]);
int sound_read_resample(const uint8_t *rom, size_t rom_size, int16_t resample[256]);

#ifdef __cplusplus
}
#endif

#endif
