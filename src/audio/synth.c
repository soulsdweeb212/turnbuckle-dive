/* The synthesizer (src/audio/synth.h). */
#include "synth.h"

#include <stdlib.h>
#include <string.h>

#include "vadpcm.h"

/* How the reverb takes the wet pair in: one channel, the left times -0.293 plus the right times 0.707 (the gains the
 * game's audio library gives its two MIXER commands, 0xDA83 and 0x5A82, which have 15 fractional bits). */
#define REVERB_FROM_LEFT ((int16_t)0xDA83)
#define REVERB_FROM_RIGHT 0x5A82

static int16_t clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* Adds block `in` times gain (15 fractional bits) to block `out`, saturating, as the RSP's MIXER command does. */
static void mix_block(int16_t *out, const int16_t *in, int16_t gain) {
    int i;

    for (i = 0; i < SYNTH_SUBFRAME; i++) {
        out[i] = clamp16(out[i] + ((in[i] * gain) >> 15));
    }
}

int synth_init(Synth *synth, const uint8_t *rom, size_t rom_size, const int16_t eqpower[128],
               const SynthReverb *reverb) {
    memset(synth, 0, sizeof(*synth));
    synth->rom = rom;
    synth->rom_size = rom_size;
    memcpy(synth->eqpower, eqpower, sizeof(synth->eqpower));
    if (reverb != NULL && reverb->length != 0) {
        synth->reverb = *reverb;
        if (synth->reverb.sections > SYNTH_REVERB_SECTIONS) {
            synth->reverb.sections = SYNTH_REVERB_SECTIONS;
        }
        synth->line = calloc(reverb->length, sizeof(int16_t));
        if (synth->line == NULL) {
            return -1;
        }
    }
    return 0;
}

void synth_free(Synth *synth) {
    free(synth->line);
    memset(synth, 0, sizeof(*synth));
}

void synth_set_player(Synth *synth, SynthPlayer player, void *context) {
    synth->player = player;
    synth->player_context = context;
    synth->player_due = 0;
}

/* ---- The voices ---- */

static SynthVoice *voice_at(Synth *synth, int voice) {
    return voice >= 0 && voice < SYNTH_VOICES ? &synth->voice[voice] : NULL;
}

/* The next sample of a voice's stream: its loop gone round as often as the sample says, and 0 once past its end. */
static float stream_next(Synth *synth, SynthVoice *v) {
    const SoundWave *w = v->wave;
    uint32_t total = sound_wave_samples(w);
    uint32_t frame;

    if (w->has_loop && v->position == w->loop_end && (w->loop_count == 0xFFFFFFFFu || v->loops < w->loop_count)) {
        /* Round the loop. The sample keeps the decoded frame that holds the loop's start (the state the decoder is in
         * there), so the stream goes on from inside that frame, and decodes the next from it. */
        v->position = w->loop_start;
        v->frame_number = w->loop_start / 16;
        memcpy(v->frame, w->loop_state, sizeof(v->frame));
        memcpy(v->state, w->loop_state, sizeof(v->state));
        if (w->loop_count != 0xFFFFFFFFu) {
            v->loops++;
        }
    }
    if (v->position >= total) {
        v->position++;
        if (v->position >= total + 3) { /* the resampler has had the last sample */
            v->ended = 1;
        }
        return 0.0f;
    }
    frame = v->position / 16;
    if (frame != v->frame_number) {
        if (w->type == SOUND_WAVE_ADPCM) {
            /* Frames are only ever read in order (a loop restarts from a kept frame), so the state is the last one's. */
            vadpcm_decode_frame(synth->rom + w->rom + 9 * frame, w->book, w->order, w->predictors, v->state, v->frame);
        } else {
            const uint8_t *p = synth->rom + w->rom + 32 * frame;
            uint32_t i;

            for (i = 0; i < 16 && frame * 16 + i < total; i++) {
                v->frame[i] = (int16_t)(uint16_t)((unsigned)p[2 * i] << 8 | p[2 * i + 1]);
            }
        }
        v->frame_number = frame;
    }
    return v->frame[v->position++ % 16];
}

void synth_voice_play(Synth *synth, int voice, const SoundWave *wave) {
    SynthVoice *v = voice_at(synth, voice);
    SynthVoice keep;

    if (v == NULL || wave == NULL || wave->rom == 0 || wave->rom > synth->rom_size ||
        wave->length > synth->rom_size - wave->rom) {
        return;
    }
    /* The stream starts again; the settings carry on. */
    keep = *v;
    memset(v, 0, sizeof(*v));
    v->wave = wave;
    v->frame_number = 0xFFFFFFFFu;
    v->pitch = keep.pitch;
    v->volume = keep.volume;
    v->volume_target = keep.volume_target;
    v->volume_step = keep.volume_step;
    v->ramp = keep.ramp;
    v->pan = keep.pan;
    v->fxmix = keep.fxmix;
    /* The resampler starts with the first sample under it and the next two ahead. */
    v->history[1] = stream_next(synth, v);
    v->history[2] = stream_next(synth, v);
    v->history[3] = stream_next(synth, v);
}

void synth_voice_start(Synth *synth, int voice, const SoundWave *wave, float pitch, int16_t volume, int pan, int fxmix,
                       int32_t ramp_us) {
    SynthVoice *v = voice_at(synth, voice);

    if (v == NULL) {
        return;
    }
    v->volume = 0.0f;
    v->ramp = 0;
    synth_voice_play(synth, voice, wave);
    if (v->wave != wave) {
        return;
    }
    synth_voice_set_pitch(synth, voice, pitch);
    synth_voice_set_pan(synth, voice, pan);
    synth_voice_set_fxmix(synth, voice, fxmix);
    synth_voice_set_volume(synth, voice, volume, ramp_us);
}

void synth_voice_stop(Synth *synth, int voice) {
    SynthVoice *v = voice_at(synth, voice);

    if (v != NULL) {
        v->wave = NULL;
    }
}

void synth_voice_set_pitch(Synth *synth, int voice, float pitch) {
    SynthVoice *v = voice_at(synth, voice);

    if (v != NULL) {
        v->pitch = pitch < 0.0f ? 0.0f : pitch > SYNTH_MAX_PITCH ? SYNTH_MAX_PITCH : pitch;
    }
}

void synth_voice_set_volume(Synth *synth, int voice, int16_t volume, int32_t ramp_us) {
    SynthVoice *v = voice_at(synth, voice);
    int32_t samples;

    if (v == NULL) {
        return;
    }
    /* The mixer's gain is the volume squared (n_audio's envelope mixer: volume x volume / 32768), so that volumes
     * from the music player fall off as the N64's do. */
    v->volume_target = volume <= 0 ? 0.0f : (float)((int32_t)volume * volume / 32768);
    samples = ramp_us <= 0 ? 0 : (int32_t)((int64_t)ramp_us * SYNTH_RATE / 1000000);
    if (samples <= 0) {
        v->volume = v->volume_target;
        v->ramp = 0;
    } else {
        v->volume_step = (v->volume_target - v->volume) / (float)samples;
        v->ramp = samples;
    }
}

void synth_voice_set_pan(Synth *synth, int voice, int pan) {
    SynthVoice *v = voice_at(synth, voice);

    if (v != NULL) {
        v->pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
    }
}

void synth_voice_set_fxmix(Synth *synth, int voice, int fxmix) {
    SynthVoice *v = voice_at(synth, voice);

    if (v != NULL) {
        v->fxmix = fxmix < 0 ? 0 : fxmix > 127 ? 127 : fxmix;
    }
}

int synth_voice_playing(const Synth *synth, int voice) {
    return voice >= 0 && voice < SYNTH_VOICES && synth->voice[voice].wave != NULL && !synth->voice[voice].ended;
}

/* Adds a voice's sub-frame to the dry and wet pairs. The resampler reads between the samples with a four-point cubic
 * curve (the RSP uses a four-point filter of its own, whose table is in its microcode). */
static void voice_mix(Synth *synth, SynthVoice *v, float *dry, float *wet) {
    const float scale = 1.0f / (32768.0f * 32768.0f * 32768.0f);
    float left = synth->eqpower[v->pan] * scale;
    float right = synth->eqpower[127 - v->pan] * scale;
    float to_dry = synth->eqpower[v->fxmix];
    float to_wet = synth->eqpower[127 - v->fxmix];
    float *h = v->history;
    int i;

    for (i = 0; i < SYNTH_SUBFRAME && !v->ended; i++) {
        float t = v->phase;
        float s = h[1] + 0.5f * t *
                             (h[2] - h[0] +
                              t * (2.0f * h[0] - 5.0f * h[1] + 4.0f * h[2] - h[3] + t * (3.0f * (h[1] - h[2]) + h[3] - h[0])));
        float l, r;

        if (v->ramp > 0) {
            v->volume += v->volume_step;
            if (--v->ramp == 0) {
                v->volume = v->volume_target;
            }
        }
        s *= v->volume;
        l = s * left;
        r = s * right;
        dry[2 * i] += l * to_dry;
        dry[2 * i + 1] += r * to_dry;
        wet[2 * i] += l * to_wet;
        wet[2 * i + 1] += r * to_wet;
        v->phase += v->pitch;
        while (v->phase >= 1.0f) {
            v->phase -= 1.0f;
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = stream_next(synth, v);
        }
    }
}

/* ---- The reverb ---- */

static void line_read(const Synth *synth, uint32_t back, int16_t *block) {
    uint32_t length = synth->reverb.length;
    uint32_t at = (synth->line_at + length - back % length) % length;
    int i;

    for (i = 0; i < SYNTH_SUBFRAME; i++) {
        block[i] = synth->line[at];
        if (++at == length) {
            at = 0;
        }
    }
}

static void line_write(Synth *synth, uint32_t back, const int16_t *block) {
    uint32_t length = synth->reverb.length;
    uint32_t at = (synth->line_at + length - back % length) % length;
    int i;

    for (i = 0; i < SYNTH_SUBFRAME; i++) {
        synth->line[at] = block[i];
        if (++at == length) {
            at = 0;
        }
    }
}

/* Runs a sub-frame of the wet pair through the reverb, into out (one channel, which goes to both sides). */
static void reverb_run(Synth *synth, const int16_t *wet, int16_t *out) {
    int16_t in[SYNTH_SUBFRAME];
    int16_t a[SYNTH_SUBFRAME];
    int16_t b[SYNTH_SUBFRAME];
    int s, i;

    for (i = 0; i < SYNTH_SUBFRAME; i++) {
        in[i] = clamp16(((wet[2 * i] * REVERB_FROM_LEFT) >> 15) + ((wet[2 * i + 1] * REVERB_FROM_RIGHT) >> 15));
        out[i] = 0;
    }
    line_write(synth, 0, in);
    for (s = 0; s < synth->reverb.sections; s++) {
        const SynthReverbSection *d = &synth->reverb.section[s];

        line_read(synth, d->input, a);
        line_read(synth, d->output, b);
        if (d->ffcoef != 0) {
            mix_block(b, a, d->ffcoef);
        }
        if (d->fbcoef != 0) {
            mix_block(a, b, d->fbcoef);
            line_write(synth, d->input, a);
        }
        if (d->lowpass != 0) {
            /* One pole: each value is the input times 1 - k plus the last value times k, k = lowpass / 32768. */
            float k = d->lowpass / 32768.0f;
            float last = synth->lowpass_last[s];

            for (i = 0; i < SYNTH_SUBFRAME; i++) {
                last = b[i] * (1.0f - k) + last * k;
                b[i] = clamp16((int32_t)last);
            }
            synth->lowpass_last[s] = last;
        }
        line_write(synth, d->output, b);
        if (d->gain != 0) {
            mix_block(out, b, d->gain);
        }
    }
    synth->line_at = (synth->line_at + SYNTH_SUBFRAME) % synth->reverb.length;
}

/* ---- Output ---- */

static void make_subframe(Synth *synth) {
    float dry[SYNTH_SUBFRAME * 2];
    float wet[SYNTH_SUBFRAME * 2];
    int i;

    /* The player first: what it changes is heard from this sub-frame. A player that asks for no time at all is called
     * again in the next. */
    while (synth->player != NULL && synth->player_due < SYNTH_SUBFRAME) {
        int32_t us = synth->player(synth->player_context);

        /* The time to the next call, rounded to whole samples as n_audio does (16666 us is 480 samples). */
        synth->player_due += us > 0 ? (int32_t)((int64_t)us * SYNTH_RATE / 1000000.0 + 0.5) : SYNTH_SUBFRAME;
    }
    synth->player_due -= SYNTH_SUBFRAME;

    memset(dry, 0, sizeof(dry));
    memset(wet, 0, sizeof(wet));
    for (i = 0; i < SYNTH_VOICES; i++) {
        if (synth_voice_playing(synth, i)) {
            voice_mix(synth, &synth->voice[i], dry, wet);
        }
    }
    if (synth->line != NULL) {
        int16_t wet16[SYNTH_SUBFRAME * 2];
        int16_t reverb[SYNTH_SUBFRAME];

        for (i = 0; i < SYNTH_SUBFRAME * 2; i++) {
            wet16[i] = clamp16((int32_t)wet[i]);
        }
        reverb_run(synth, wet16, reverb);
        for (i = 0; i < SYNTH_SUBFRAME; i++) {
            dry[2 * i] += reverb[i];
            dry[2 * i + 1] += reverb[i];
        }
    }
    for (i = 0; i < SYNTH_SUBFRAME * 2; i++) {
        synth->out[i] = clamp16((int32_t)dry[i]);
    }
    synth->out_left = SYNTH_SUBFRAME;
}

void synth_render(Synth *synth, int16_t *out, int frames) {
    while (frames > 0) {
        int n;

        if (synth->out_left == 0) {
            make_subframe(synth);
        }
        n = frames < synth->out_left ? frames : synth->out_left;
        memcpy(out, synth->out + 2 * (SYNTH_SUBFRAME - synth->out_left), (size_t)n * 2 * sizeof(int16_t));
        out += 2 * n;
        frames -= n;
        synth->out_left -= n;
    }
}
