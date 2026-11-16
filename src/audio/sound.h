/* The port's sound: the game's two sound banks and the synthesizer that plays them, set up from the ROM, and the
 * hand-off of its output to the sound device (src/audio/sound.c). C and C++.
 *
 * The music player (libmus on the N64) will drive the synthesizer; until it is ported, the developer's Sound Test window
 * (F1) plays single samples on it. */
#ifndef TD_SOUND_H
#define TD_SOUND_H

#include <stddef.h>
#include <stdint.h>

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

/* The sound device's side: `feed` is called at each of the game's frames (port_present), to hand the device as much
 * output as it wants (src/audio/sound_output.cpp). */
void sound_set_output(void (*feed)(void));
void sound_present(void);

/* The game's reverb, read from the ROM: the big room (fxType 2), from its audio library's parameter table. Returns 0,
 * or -1 when the table is not one the synthesizer can run. Used by sound_start; here for the tests. */
int sound_read_reverb(const uint8_t *rom, size_t rom_size, SynthReverb *reverb);
int sound_read_eqpower(const uint8_t *rom, size_t rom_size, int16_t eqpower[128]);

#ifdef __cplusplus
}
#endif

#endif
