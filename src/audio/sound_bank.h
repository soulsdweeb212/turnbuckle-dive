/* The game's two sound banks, read from the ROM into the PC's own records (src/audio/sound_bank.c). C and C++.
 *
 * The game has a bank of samples for its music and one for its sound effects, each in two parts in the ROM: a table
 * that describes the samples ("N64 PtrTablesV2", the format of the music library libmus) and the samples' data, which
 * the N64 reads from the ROM a little at a time while it plays them. The table holds 32-bit offsets, which the N64's
 * libmus turns into addresses in place (__MusIntRemapPtrBank in the decompilation); on PC the bank is read once into
 * the records below, with host pointers and the PC's byte order. The sample data stays in the ROM, read where it lies. */
#ifndef TD_SOUND_BANK_H
#define TD_SOUND_BANK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where the banks are in the ROM: the table from START to END, the samples' data from WAVES (in the decompilation,
 * func_80003120 reads them; include/port.h names them ROM_MUS_BANK and the others). */
#define SOUND_BANK_MUSIC_START 0xCE9F00u
#define SOUND_BANK_MUSIC_END   0xCEC610u
#define SOUND_BANK_MUSIC_WAVES 0xCEC610u
#define SOUND_BANK_EFFECT_START 0xE35070u
#define SOUND_BANK_EFFECT_END   0xE3B300u
#define SOUND_BANK_EFFECT_WAVES 0xE3B300u

/* The sample types of the N64's audio library: ADPCM (9-byte frames of 16 samples) or plain 16-bit samples. */
#define SOUND_WAVE_ADPCM 0
#define SOUND_WAVE_RAW16 1

/* One sample, as the N64's ALWaveTable describes it. */
typedef struct {
    uint32_t rom;    /* the ROM address of its data */
    uint32_t length; /* its data's size in bytes */
    uint8_t type;    /* SOUND_WAVE_ADPCM or SOUND_WAVE_RAW16 */
    /* The loop, when has_loop: from sample loop_start to loop_end, loop_count times (0xFFFFFFFF: for ever), with the
     * ADPCM decoder's state at loop_start, so that each pass decodes as the first. */
    int has_loop;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t loop_count;
    int16_t loop_state[16];
    /* The ADPCM codebook: `predictors` sets of `order` x 8 coefficients (book[p * order * 8 + k * 8 + i]). */
    int32_t order;
    int32_t predictors;
    const int16_t *book;
    /* The sample's tuning in semitones, which libmus adds to a note's: the bank's detune byte minus 48, plus its fine
     * tune in hundredths (__MusIntRemapPtrBank). A note whose tuned pitch is 0 plays the sample at the output rate. */
    float tune;
} SoundWave;

typedef struct {
    uint32_t count;
    SoundWave *waves;
    int16_t *books; /* the waves' codebooks, one block */
} SoundBank;

/* Reads the bank whose table is rom[start..end) and whose samples' data starts at rom[waves] into bank. Returns 0, or
 * -1 with a message on stderr when the table is not what the game's is. Free the bank with sound_bank_free. */
int sound_bank_load(SoundBank *bank, const uint8_t *rom, size_t rom_size, uint32_t start, uint32_t end,
                    uint32_t waves);
void sound_bank_free(SoundBank *bank);

/* Decodes the whole of a sample (its loop once) into out, which must hold sound_wave_samples(wave) samples. Returns the
 * number written. */
uint32_t sound_wave_samples(const SoundWave *wave);
uint32_t sound_wave_decode(const SoundWave *wave, const uint8_t *rom, size_t rom_size, int16_t *out);

#ifdef __cplusplus
}
#endif

#endif
