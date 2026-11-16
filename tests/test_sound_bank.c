/* Reads the game's two sound banks from the ROM (src/audio/sound_bank.c) and decodes every sample with the port's
 * ADPCM decoder (src/audio/vadpcm.c). It checks the banks' sizes and a checksum of all the decoded samples, which was
 * made when the decoder matched, sample for sample, a decode written separately from the textbook form of the format
 * (each half-frame as one 8 x (order + 8) matrix). Without the ROM it reports the test as skipped (77). */
#include <stdio.h>
#include <stdlib.h>

#include "audio/sound_bank.h"
#include "rom.h"

#define MUSIC_SAMPLES 54
#define EFFECT_SAMPLES 147
#define EXPECTED_CHECKSUM 0x3EA87DAAu

/* FNV-1a over every decoded sample's two bytes, low byte first. */
static uint32_t checksum_bank(const Rom *rom, uint32_t start, uint32_t end, uint32_t waves, uint32_t *count,
                              uint32_t hash) {
    SoundBank bank;
    uint32_t i;

    if (sound_bank_load(&bank, rom->data, rom->size, start, end, waves) != 0) {
        *count = 0;
        return 0;
    }
    *count = bank.count;
    for (i = 0; i < bank.count; i++) {
        int16_t *samples = malloc((sound_wave_samples(&bank.waves[i]) + 1) * sizeof(int16_t));
        uint32_t n = samples != NULL ? sound_wave_decode(&bank.waves[i], rom->data, rom->size, samples) : 0;
        uint32_t k;

        for (k = 0; k < n; k++) {
            uint16_t v = (uint16_t)samples[k];

            hash = (hash ^ (v & 0xFF)) * 16777619u;
            hash = (hash ^ (v >> 8)) * 16777619u;
        }
        free(samples);
    }
    sound_bank_free(&bank);
    return hash;
}

int main(void) {
    Rom rom;
    RomStatus status = rom_load(rom_find(NULL), &rom);
    uint32_t music, effects;
    uint32_t hash = 2166136261u;

    if (status == ROM_NOT_FOUND) {
        printf("no ROM: skipped\n");
        return 77;
    }
    if (status != ROM_OK) {
        printf("the ROM is not the supported one: skipped\n");
        return 77;
    }
    hash = checksum_bank(&rom, SOUND_BANK_MUSIC_START, SOUND_BANK_MUSIC_END, SOUND_BANK_MUSIC_WAVES, &music, hash);
    hash = checksum_bank(&rom, SOUND_BANK_EFFECT_START, SOUND_BANK_EFFECT_END, SOUND_BANK_EFFECT_WAVES, &effects,
                         hash);
    rom_free(&rom);
    printf("music samples %u, effect samples %u, checksum 0x%08X\n", (unsigned)music, (unsigned)effects,
           (unsigned)hash);
    if (music != MUSIC_SAMPLES || effects != EFFECT_SAMPLES) {
        printf("FAIL: expected %u music and %u effect samples\n", MUSIC_SAMPLES, EFFECT_SAMPLES);
        return 1;
    }
    if (hash != EXPECTED_CHECKSUM) {
        printf("FAIL: expected checksum 0x%08X\n", (unsigned)EXPECTED_CHECKSUM);
        return 1;
    }
    return 0;
}
