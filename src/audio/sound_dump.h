/* --sound-dump DIR: every sample of the game's two sound banks as a WAV file (src/audio/sound_dump.c). C only. */
#ifndef TD_SOUND_DUMP_H
#define TD_SOUND_DUMP_H

#include <stddef.h>
#include <stdint.h>

/* Writes DIR/music_NNN.wav and DIR/effect_NNN.wav, one a sample, decoded from the ROM (rom, size bytes, .z64 order),
 * and DIR/samples.txt, a line a sample: its file, ROM address, size, type, loop and tuning. The folder must exist.
 * Returns 0, or 1 when something could not be read or written. */
int sound_dump(const uint8_t *rom, size_t size, const char *dir);

#endif
