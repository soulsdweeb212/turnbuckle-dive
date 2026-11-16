/* --sound-dump (src/audio/sound_dump.h). A check of the bank reader and the ADPCM decoder that does not need the rest
 * of the sound code: the files can be listened to. A sample has no pitch of its own: the music library plays it at
 * the pitch of each note, tuned by the sample's tuning (samples.txt). The files are written at DUMP_RATE, the game's
 * output rate, the speed at which a note whose tuned pitch is 0 plays the sample. */
#include "sound_dump.h"

#include <stdio.h>
#include <stdlib.h>

#include "sound_bank.h"

#define DUMP_RATE 28800u /* the game's output rate (func_80003120: 0x7080) */

static void put16(FILE *f, unsigned v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, uint32_t v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

/* A mono 16-bit WAV file. */
static int write_wav(const char *path, const int16_t *samples, uint32_t count) {
    FILE *f = fopen(path, "wb");
    uint32_t i;
    int ok;

    if (f == NULL) {
        return -1;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + count * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);               /* PCM */
    put16(f, 1);               /* mono */
    put32(f, DUMP_RATE);
    put32(f, DUMP_RATE * 2);   /* bytes a second */
    put16(f, 2);               /* bytes a frame */
    put16(f, 16);              /* bits a sample */
    fwrite("data", 1, 4, f);
    put32(f, count * 2);
    for (i = 0; i < count; i++) {
        put16(f, (uint16_t)samples[i]);
    }
    ok = !ferror(f);
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int dump_bank(const uint8_t *rom, size_t size, const char *dir, const char *name, uint32_t start, uint32_t end,
                     uint32_t waves, FILE *list) {
    SoundBank bank;
    uint32_t i;
    int result = 0;

    if (sound_bank_load(&bank, rom, size, start, end, waves) != 0) {
        return 1;
    }
    for (i = 0; i < bank.count; i++) {
        const SoundWave *w = &bank.waves[i];
        int16_t *samples = malloc((sound_wave_samples(w) + 1) * sizeof(int16_t));
        char path[1024];
        uint32_t n;

        if (samples == NULL) {
            result = 1;
            break;
        }
        n = sound_wave_decode(w, rom, size, samples);
        snprintf(path, sizeof(path), "%s/%s_%03u.wav", dir, name, (unsigned)i);
        if (write_wav(path, samples, n) != 0) {
            fprintf(stderr, "sound-dump: could not write %s\n", path);
            result = 1;
        }
        fprintf(list, "%s_%03u.wav rom=0x%06X bytes=%u samples=%u type=%s loop=%s", name, (unsigned)i,
                (unsigned)w->rom, (unsigned)w->length, (unsigned)n, w->type == SOUND_WAVE_ADPCM ? "adpcm" : "raw16",
                w->has_loop ? "yes" : "no");
        if (w->has_loop) {
            fprintf(list, " (%u..%u x%d)", (unsigned)w->loop_start, (unsigned)w->loop_end, (int)w->loop_count);
        }
        fprintf(list, " tune=%+.2f\n", w->tune);
        free(samples);
    }
    printf("sound-dump: %u %s samples\n", (unsigned)bank.count, name);
    sound_bank_free(&bank);
    return result;
}

int sound_dump(const uint8_t *rom, size_t size, const char *dir) {
    char path[1024];
    FILE *list;
    int result;

    snprintf(path, sizeof(path), "%s/samples.txt", dir);
    list = fopen(path, "w");
    if (list == NULL) {
        fprintf(stderr, "sound-dump: could not write %s (does the folder exist?)\n", path);
        return 1;
    }
    result = dump_bank(rom, size, dir, "music", SOUND_BANK_MUSIC_START, SOUND_BANK_MUSIC_END, SOUND_BANK_MUSIC_WAVES,
                       list);
    result |= dump_bank(rom, size, dir, "effect", SOUND_BANK_EFFECT_START, SOUND_BANK_EFFECT_END,
                        SOUND_BANK_EFFECT_WAVES, list);
    fclose(list);
    return result;
}
