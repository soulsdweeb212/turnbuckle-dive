/* The synthesizer (src/audio/synth.c), with the game's sound banks, pan gains and reverb from the ROM
 * (src/audio/sound.c). It checks:
 * - a voice at pitch 1, all to the left and dry, gives the sample as decoded, scaled by its volume;
 * - a looping sample goes round its loop with no seam: after the loop's end, the output is the sample from the loop's
 *   start again;
 * - at pitch 0.5 a sample lasts twice as long, and at 2 half as long;
 * - the pan: all to the left leaves the right silent, and the middle gives both sides the same;
 * - the reverb: a sound sent all to it comes out the same on both sides, and fuller once its delays have gone by (its
 *   all-pass sections let a little through at once);
 * - a player asking to be called every 16666 microseconds (the music player, 480 samples) is called 60 times in a
 *   second of output.
 * Without the ROM it reports the test as skipped (77). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/sound.h"
#include "rom.h"

static int g_failed;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: " __VA_ARGS__);                                                                              \
            printf("\n");                                                                                              \
            g_failed = 1;                                                                                              \
        }                                                                                                              \
    } while (0)

/* A fresh synthesizer for each check, so that none hears another's reverb. */
static Synth *fresh(void) {
    Synth *synth = sound_synth();
    SynthReverb reverb;
    int16_t eqpower[128];
    const uint8_t *rom = synth->rom;
    size_t size = synth->rom_size;

    memcpy(eqpower, synth->eqpower, sizeof(eqpower));
    reverb = synth->reverb;
    synth_free(synth);
    synth_init(synth, rom, size, eqpower, &reverb);
    return synth;
}

/* Renders voice 0 until it ends (at most `limit` frames) into a new buffer; returns the number of frames it played. */
static int16_t *play(Synth *synth, int limit, int *frames) {
    int16_t *out = calloc((size_t)limit * 2, sizeof(int16_t));
    int n = 0;

    while (n < limit && synth_voice_playing(synth, 0)) {
        int step = limit - n < SYNTH_SUBFRAME ? limit - n : SYNTH_SUBFRAME;

        synth_render(synth, out + 2 * n, step);
        n += step;
    }
    *frames = n;
    return out;
}

static const SoundWave *first_wave(const SoundBank *bank, int looping) {
    uint32_t i;

    for (i = 0; i < bank->count; i++) {
        if (bank->waves[i].has_loop == looping && sound_wave_samples(&bank->waves[i]) > 4000) {
            return &bank->waves[i];
        }
    }
    return NULL;
}

static void check_exact(const Rom *rom, const SoundWave *wave) {
    Synth *synth = fresh();
    uint32_t n = sound_wave_samples(wave);
    int16_t *decoded = malloc(n * sizeof(int16_t));
    int16_t *out;
    int frames;
    uint32_t i, bad = 0;

    sound_wave_decode(wave, rom->data, rom->size, decoded);
    synth_voice_start(synth, 0, wave, 1.0f, 32767, 0, 0, 0);
    out = play(synth, (int)n, &frames);
    for (i = 0; i < (uint32_t)frames; i++) {
        /* Full volume (its square, 32766), all to the left and dry is the sample times 32766 / 32768 and (32767 /
         * 32768) squared: up to 4 less. */
        if (abs(out[2 * i] - decoded[i]) > 4 || out[2 * i + 1] != 0) {
            bad++;
        }
    }
    CHECK(bad == 0, "pitch 1: %u of %d samples differ from the decoded sample", (unsigned)bad, frames);
    free(out);
    free(decoded);
}

static void check_loop(const Rom *rom, const SoundWave *wave) {
    Synth *synth = fresh();
    uint32_t n = sound_wave_samples(wave);
    uint32_t length = wave->loop_end - wave->loop_start;
    int16_t *decoded = malloc(n * sizeof(int16_t));
    int16_t *out;
    int frames;
    uint32_t i, bad = 0;

    sound_wave_decode(wave, rom->data, rom->size, decoded);
    synth_voice_start(synth, 0, wave, 1.0f, 32767, 0, 0, 0);
    out = play(synth, (int)(wave->loop_end + 2 * length), &frames);
    CHECK(frames == (int)(wave->loop_end + 2 * length), "a looping sample stopped after %d samples", frames);
    for (i = wave->loop_end; i < (uint32_t)frames; i++) {
        if (abs(out[2 * i] - decoded[wave->loop_start + (i - wave->loop_end) % length]) > 4) {
            bad++;
        }
    }
    CHECK(bad == 0, "loop: %u samples after the loop's end are not the loop's again", (unsigned)bad);
    free(out);
    free(decoded);
}

static void check_pitch(const SoundWave *wave) {
    static const float pitches[] = { 0.5f, 2.0f };
    uint32_t n = sound_wave_samples(wave);
    int k;

    for (k = 0; k < 2; k++) {
        Synth *synth = fresh();
        int frames;
        int16_t *out;
        double expected = n / (pitches[k] > SYNTH_MAX_PITCH ? SYNTH_MAX_PITCH : pitches[k]);

        synth_voice_start(synth, 0, wave, pitches[k], 32767, 64, 0, 0);
        out = play(synth, (int)n * 3, &frames);
        /* The voice ends within the sub-frame after its last sample. */
        CHECK(fabs(frames - expected) <= SYNTH_SUBFRAME + 4, "pitch %.1f: %d samples, expected about %.0f",
              pitches[k], frames, expected);
        free(out);
    }
}

static void check_pan(const SoundWave *wave) {
    Synth *synth = fresh();
    int frames, i, unequal = 0;
    int16_t *out;

    synth_voice_start(synth, 0, wave, 1.0f, 32767, 64, 0, 0);
    out = play(synth, 4000, &frames);
    for (i = 0; i < frames; i++) {
        /* eqpower[64] and eqpower[63] differ slightly; the middle is 64. */
        if (abs(out[2 * i] - out[2 * i + 1]) > abs(out[2 * i]) / 50 + 2) {
            unequal++;
        }
    }
    CHECK(unequal == 0, "pan 64: %d samples differ between the sides", unequal);
    free(out);
}

static void check_reverb(const SoundWave *wave) {
    Synth *synth = fresh();
    int frames, i, unequal = 0, early = 0, loudest = 0;
    int16_t *out;
    uint32_t delay = (uint32_t)-1;
    int s;

    /* The first section that gives output: until its output tap's delay, only the all-pass sections' feed-forward
     * parts are heard. */
    for (s = 0; s < synth->reverb.sections; s++) {
        if (synth->reverb.section[s].gain != 0 && synth->reverb.section[s].output < delay) {
            delay = synth->reverb.section[s].output;
        }
    }
    synth_voice_start(synth, 0, wave, 1.0f, 32767, 64, 127, 0);
    out = play(synth, 20000, &frames);
    for (i = 0; i < frames; i++) {
        int level = abs(out[2 * i]);

        if (i < (int)delay - SYNTH_SUBFRAME && level > early) {
            early = level;
        }
        if (level > loudest) {
            loudest = level;
        }
        if (out[2 * i] != out[2 * i + 1]) {
            unequal++;
        }
    }
    CHECK(loudest > 0, "reverb: no sound came out");
    CHECK(early * 2 < loudest, "reverb: as loud before its delay (%d) as after (%d)", early, loudest);
    CHECK(unequal == 0, "reverb: %d samples differ between the sides", unequal);
    free(out);
}

static int g_calls;

static int32_t every_frame(void *context) {
    (void)context;
    g_calls++;
    return 16666;
}

static void check_player(void) {
    Synth *synth = fresh();
    int16_t *out = malloc(SYNTH_RATE * 2 * sizeof(int16_t));

    g_calls = 0;
    synth_set_player(synth, every_frame, NULL);
    synth_render(synth, out, SYNTH_RATE);
    synth_set_player(synth, NULL, NULL);
    /* Each call's time is rounded to whole samples (480), and calls run up to a sub-frame ahead, so one more may come. */
    CHECK(g_calls >= 60 && g_calls <= 61, "player: called %d times in a second, expected 60", g_calls);
    free(out);
}

int main(void) {
    Rom rom;
    RomStatus status = rom_load(rom_find(NULL), &rom);
    const SoundWave *plain, *looping;

    if (status != ROM_OK) {
        printf(status == ROM_NOT_FOUND ? "no ROM: skipped\n" : "the ROM is not the supported one: skipped\n");
        return 77;
    }
    if (sound_start(rom.data, rom.size) != 0) {
        printf("FAIL: sound_start\n");
        return 1;
    }
    plain = first_wave(sound_bank(SOUND_BANK_EFFECTS), 0);
    looping = first_wave(sound_bank(SOUND_BANK_MUSIC), 1);
    CHECK(plain != NULL && looping != NULL, "no sample to play");
    if (plain != NULL && looping != NULL) {
        check_exact(&rom, plain);
        check_loop(&rom, looping);
        check_pitch(plain);
        check_pan(plain);
        check_reverb(plain);
        check_player();
    }
    sound_end();
    rom_free(&rom);
    if (!g_failed) {
        printf("all synthesizer checks passed\n");
    }
    return g_failed;
}
