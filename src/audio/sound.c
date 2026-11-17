/* The port's sound (src/audio/sound.h). */
#include "sound.h"

#include <stdio.h>
#include <string.h>

/* Game data in the ROM: a main-segment RAM address minus 0x7FFFF400. In the decompilation, BIGROOM_PARAMS_N (the
 * big room's parameters, which n_alFxNew reads) and n_eqpower (the pan gains, which the envelope mixer reads). */
#define MAIN_RAM_TO_ROM 0x7FFFF400u
#define ROM_BIGROOM (0x80036FC8u - MAIN_RAM_TO_ROM)
#define ROM_EQPOWER (0x800370F0u - MAIN_RAM_TO_ROM)
/* The audio microcode's resampling filter, in its data (which the game's ROM holds beside the microcode). */
#define ROM_RESAMPLE 0x3B270u

static SoundBank g_banks[2];
static Synth g_synth;
static Music g_music;
static int g_started;
static void (*g_feed)(void);

static int32_t be32s(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
}

/* The table: the number of sections and the delay line's length, then 8 words a section: the input tap, the output
 * tap, the feedback and feed-forward coefficients, the output gain, the chorus's rate and depth, and the low-pass
 * filter's pole (all 32-bit, big-endian). */
int sound_read_reverb(const uint8_t *rom, size_t rom_size, SynthReverb *reverb) {
    const uint8_t *t = rom + ROM_BIGROOM;
    int32_t sections, length;
    int s;

    memset(reverb, 0, sizeof(*reverb));
    if (rom_size < ROM_BIGROOM + 8) {
        return -1;
    }
    sections = be32s(t);
    length = be32s(t + 4);
    if (sections < 1 || sections > SYNTH_REVERB_SECTIONS || length < SYNTH_SUBFRAME || length > 0x10000 ||
        rom_size < ROM_BIGROOM + 8 + (size_t)sections * 32) {
        return -1;
    }
    reverb->length = (uint32_t)length;
    reverb->sections = sections;
    for (s = 0; s < sections; s++) {
        const uint8_t *e = t + 8 + 32 * s;
        SynthReverbSection *d = &reverb->section[s];
        int32_t input = be32s(e), output = be32s(e + 4);

        if (input < 0 || output < 0 || input >= length || output >= length || be32s(e + 20) != 0) {
            return -1; /* a tap outside the line, or a chorus */
        }
        d->input = (uint32_t)input;
        d->output = (uint32_t)output;
        d->fbcoef = (int16_t)be32s(e + 8);
        d->ffcoef = (int16_t)be32s(e + 12);
        d->gain = (int16_t)be32s(e + 16);
        d->lowpass = (int16_t)be32s(e + 28);
    }
    return 0;
}

int sound_read_eqpower(const uint8_t *rom, size_t rom_size, int16_t eqpower[128]) {
    int i;

    if (rom_size < ROM_EQPOWER + 256) {
        return -1;
    }
    for (i = 0; i < 128; i++) {
        eqpower[i] = (int16_t)(uint16_t)((unsigned)rom[ROM_EQPOWER + 2 * i] << 8 | rom[ROM_EQPOWER + 2 * i + 1]);
    }
    /* From 1 at the left to 0 at the right. */
    return eqpower[0] == 32767 && eqpower[127] == 0 ? 0 : -1;
}

int sound_read_resample(const uint8_t *rom, size_t rom_size, int16_t resample[256]) {
    int i;

    if (rom_size < ROM_RESAMPLE + 512) {
        return -1;
    }
    for (i = 0; i < 256; i++) {
        resample[i] = (int16_t)(uint16_t)((unsigned)rom[ROM_RESAMPLE + 2 * i] << 8 | rom[ROM_RESAMPLE + 2 * i + 1]);
    }
    /* Each row's weights add up to about 1 (32768). */
    for (i = 0; i < 64; i++) {
        int32_t sum = resample[4 * i] + resample[4 * i + 1] + resample[4 * i + 2] + resample[4 * i + 3];

        if (sum < 32768 - 64 || sum > 32768 + 64) {
            return -1;
        }
    }
    return 0;
}

int sound_start(const uint8_t *rom, size_t rom_size) {
    SynthReverb reverb;
    int16_t eqpower[128];
    int16_t resample[256];

    if (g_started) {
        return 0;
    }
    if (sound_read_eqpower(rom, rom_size, eqpower) != 0 || sound_read_reverb(rom, rom_size, &reverb) != 0 ||
        sound_read_resample(rom, rom_size, resample) != 0) {
        fprintf(stderr, "sound: the pan gains, the reverb or the resampling filter are not where they should be in the ROM\n");
        return -1;
    }
    if (sound_bank_load(&g_banks[SOUND_BANK_MUSIC], rom, rom_size, SOUND_BANK_MUSIC_START, SOUND_BANK_MUSIC_END,
                        SOUND_BANK_MUSIC_WAVES) != 0) {
        return -1;
    }
    if (sound_bank_load(&g_banks[SOUND_BANK_EFFECTS], rom, rom_size, SOUND_BANK_EFFECT_START, SOUND_BANK_EFFECT_END,
                        SOUND_BANK_EFFECT_WAVES) != 0 ||
        synth_init(&g_synth, rom, rom_size, eqpower, resample, &reverb) != 0 ||
        music_init(&g_music, &g_synth, &g_banks[SOUND_BANK_MUSIC], &g_banks[SOUND_BANK_EFFECTS], rom, rom_size) != 0) {
        synth_free(&g_synth);
        sound_bank_free(&g_banks[SOUND_BANK_MUSIC]);
        sound_bank_free(&g_banks[SOUND_BANK_EFFECTS]);
        return -1;
    }
    g_started = 1;
    return 0;
}

void sound_end(void) {
    if (g_started) {
        synth_free(&g_synth);
        sound_bank_free(&g_banks[SOUND_BANK_MUSIC]);
        sound_bank_free(&g_banks[SOUND_BANK_EFFECTS]);
        g_started = 0;
    }
    g_feed = NULL;
}

const SoundBank *sound_bank(int which) {
    return g_started && (which == SOUND_BANK_MUSIC || which == SOUND_BANK_EFFECTS) ? &g_banks[which] : NULL;
}

Synth *sound_synth(void) {
    return g_started ? &g_synth : NULL;
}

Music *sound_music(void) {
    return g_started ? &g_music : NULL;
}

void sound_set_output(void (*feed)(void)) {
    g_feed = feed;
}

void sound_present(void) {
    if (g_started && g_feed != NULL) {
        g_feed();
    }
}
