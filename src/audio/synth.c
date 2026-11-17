/* The synthesizer (src/audio/synth.h). */
#include "synth.h"

#include <stdlib.h>
#include <string.h>

#include "vadpcm.h"

/* How the reverb takes the wet pair in, as the game's audio library has the microcode do it: the left is mixed into
 * itself at -0.293 (0xDA83, 15 fractional bits), which leaves 0.707 of it, then the right is mixed in at 0.707
 * (0x5A82): one channel, (left + right) x 0.707, saturating at each step. */
#define REVERB_SELF ((int16_t)0xDA83)
#define REVERB_FROM_RIGHT 0x5A82
/* The reverb's output joins the main pair at 0x7FFF, a hair under 1 (n_alMainBusPull). */
#define REVERB_OUT 0x7FFF

/* A product with 15 fractional bits, rounded, as the microcode's vector multiplies give it. */
static int32_t mul15(int32_t a, int32_t b) {
    return (a * b + 0x4000) >> 15;
}

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
               const int16_t resample[256], const SynthReverb *reverb) {
    int i;

    memset(synth, 0, sizeof(*synth));
    synth->rom = rom;
    synth->rom_size = rom_size;
    memcpy(synth->eqpower, eqpower, sizeof(synth->eqpower));
    memcpy(synth->resample, resample, sizeof(synth->resample));
    for (i = 0; i < SYNTH_VOICES; i++) {
        SynthLevels *l = &synth->voice[i].levels;

        l->level = 1;
        l->from[0] = l->from[1] = l->to[0] = l->to[1] = 1;
        l->heard[0] = l->heard[1] = (int64_t)1 << 16;
    }
    synth->retrace_samples = SYNTH_RETRACE_SAMPLES;
    synth->frame_next = 0;
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

#define TRACE(synth, voice, op, wave, value, ramp, pitch)                                                              \
    do {                                                                                                               \
        if ((synth)->trace != NULL && voice >= 0 && voice < SYNTH_VOICES) {                                            \
            (synth)->trace((synth)->trace_context, voice, op, wave, value, ramp, pitch);                               \
        }                                                                                                              \
    } while (0)

/* The next sample of a voice's stream: its loop gone round as often as the sample says, and 0 once past its end. */
static int16_t stream_next(Synth *synth, SynthVoice *v) {
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
        if (v->position >= total + 4) { /* the last sample has gone through the resampler's window */
            v->ended = 1;
        }
        return 0;
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

/* ---- Levels (src/audio/synth.h, SynthLevels) ---- */

/* A change reaches a voice: returns whether it finds the levels at the ramp's end, because it comes after another
 * change of the same group or at the start of an audio frame. */
static int join_group(Synth *synth, SynthLevels *l) {
    int at_end = l->group == synth->time + 1 || synth->time == synth->frame_start;

    l->group = synth->time + 1;
    return at_end;
}

/* Where a level is after `elapsed` samples of a ramp from `from` at `rate` a step of 8 (n_audio's own sum). */
static int32_t level_after(int32_t from, int32_t elapsed, int32_t rate) {
    int32_t steps = elapsed >> 3;

    return (int16_t)(from + ((int32_t)((rate & 0xFFFF) * steps) >> 16) + (rate >> 16) * steps);
}

/* The step every 8 samples to go from `from` to `to` in `time` samples, 16 fractional bits (the fraction scaled by
 * 65535, as n_audio does). No time: at once. */
static int32_t ramp_rate(int32_t from, int32_t to, int32_t time) {
    double start = from <= 0 ? 1.0 : from;
    double end = to < 1 ? 1.0 : to;
    double value, fraction;
    int16_t whole, carry;

    if (time == 0) {
        return from <= to ? 0x7FFFFFFF : 0;
    }
    value = (end - start) * (1.0 / time) * 8.0;
    whole = (int16_t)value;
    fraction = value - whole + 1.0;
    carry = (int16_t)fraction;
    return (int32_t)(int16_t)(whole - 1 + carry) * 65536 + (int32_t)(uint16_t)(uint32_t)((fraction - carry) * 65535.0);
}

static int32_t side_level(const Synth *synth, int32_t level, int pan) {
    return (level * synth->eqpower[pan]) >> 15;
}

/* Before a volume, pan or send change: the levels where the change finds them. */
static void levels_before_change(Synth *synth, SynthVoice *v) {
    SynthLevels *l = &v->levels;
    int s;

    if (join_group(synth, l) || l->elapsed >= l->ramp_length) {
        l->to[0] = side_level(synth, l->level, v->pan);
        l->to[1] = side_level(synth, l->level, 127 - v->pan);
        l->from[0] = l->to[0];
        l->from[1] = l->to[1];
        l->elapsed = l->ramp_length;
    } else {
        l->from[0] = level_after(l->from[0], l->elapsed, l->rate[0]);
        l->from[1] = level_after(l->from[1], l->elapsed, l->rate[1]);
    }
    for (s = 0; s < 2; s++) {
        if (l->from[s] == 0) {
            l->from[s] = 1;
        }
    }
    l->changed = 1;
}

/* At the start of a sub-frame after changes: the ramp from where the levels are to where the new settings put them. */
static void levels_start_ramp(const Synth *synth, SynthVoice *v) {
    SynthLevels *l = &v->levels;
    int s;

    l->changed = 0;
    l->to[0] = side_level(synth, l->level, v->pan);
    l->to[1] = side_level(synth, l->level, 127 - v->pan);
    for (s = 0; s < 2; s++) {
        l->rate[s] = ramp_rate(l->from[s], l->to[s], l->ramp_length);
        l->heard[s] = (int64_t)l->from[s] << 16;
    }
}

/* A step of 8 samples along the ramp, stopping at its end. */
static void levels_step(SynthLevels *l) {
    int s;

    for (s = 0; s < 2; s++) {
        int64_t end = (int64_t)l->to[s] << 16;

        l->heard[s] += l->rate[s];
        if ((l->rate[s] >= 0 && l->heard[s] > end) || (l->rate[s] < 0 && l->heard[s] < end)) {
            l->heard[s] = end;
        }
    }
}

void synth_voice_play(Synth *synth, int voice, const SoundWave *wave) {
    SynthVoice *v = voice_at(synth, voice);
    SynthVoice keep;

    TRACE(synth, voice, SYNTH_TRACE_START, wave, 0, 0, 0.0f);
    if (v == NULL || wave == NULL || wave->rom == 0 || wave->rom > synth->rom_size ||
        wave->length > synth->rom_size - wave->rom) {
        return;
    }
    /* The stream starts again; the settings and the levels carry on. */
    keep = *v;
    memset(v, 0, sizeof(*v));
    v->wave = wave;
    v->frame_number = 0xFFFFFFFFu;
    v->pitch = keep.pitch;
    v->step = keep.step;
    v->levels = keep.levels;
    v->pan = keep.pan;
    v->fxmix = keep.fxmix;
    join_group(synth, &v->levels);
    /* The resampler's four samples start as zeros, and the wave's come in behind them, as the microcode's start: its
     * window trails the samples read by four, so a sample is heard about three samples after it is read. */
}

void synth_voice_start(Synth *synth, int voice, const SoundWave *wave, float pitch, int16_t volume, int pan, int fxmix,
                       int32_t ramp_us) {
    SynthVoice *v = voice_at(synth, voice);

    if (v == NULL) {
        return;
    }
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

    TRACE(synth, voice, SYNTH_TRACE_STOP, NULL, 0, 0, 0.0f);
    if (v != NULL) {
        join_group(synth, &v->levels);
        v->levels.level = 1;
        v->levels.changed = 1;
        v->wave = NULL;
    }
}

void synth_voice_set_pitch(Synth *synth, int voice, float pitch) {
    SynthVoice *v = voice_at(synth, voice);

    TRACE(synth, voice, SYNTH_TRACE_PITCH, NULL, 0, 0, pitch);
    if (v != NULL) {
        join_group(synth, &v->levels);
        v->pitch = pitch < 0.0f ? 0.0f : pitch > SYNTH_MAX_PITCH ? SYNTH_MAX_PITCH : pitch;
        /* n_audio hands the microcode the pitch with 15 fractional bits; the microcode moves with 16. */
        v->step = (uint32_t)(int32_t)(v->pitch * 32768.0f) << 1;
    }
}

void synth_voice_set_volume(Synth *synth, int voice, int16_t volume, int32_t ramp_us) {
    SynthVoice *v = voice_at(synth, voice);
    int32_t samples;

    TRACE(synth, voice, SYNTH_TRACE_VOLUME, NULL, volume, ramp_us, 0.0f);
    if (v == NULL) {
        return;
    }
    levels_before_change(synth, v);
    /* The level is the volume squared, and the ramp lasts whole sub-frames, the nearest number to its time (480
     * samples become 552). */
    v->levels.level = ((int32_t)volume * volume) >> 15;
    samples = ramp_us <= 0 ? 0 : (int32_t)((double)ramp_us * SYNTH_RATE / 1000000.0 + 0.5);
    v->levels.ramp_length = (samples + SYNTH_SUBFRAME / 2) / SYNTH_SUBFRAME * SYNTH_SUBFRAME;
    v->levels.elapsed = 0;
}

void synth_voice_set_pan(Synth *synth, int voice, int pan) {
    SynthVoice *v = voice_at(synth, voice);

    TRACE(synth, voice, SYNTH_TRACE_PAN, NULL, pan, 0, 0.0f);
    if (v != NULL) {
        levels_before_change(synth, v);
        v->pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
    }
}

void synth_voice_set_fxmix(Synth *synth, int voice, int fxmix) {
    SynthVoice *v = voice_at(synth, voice);

    TRACE(synth, voice, SYNTH_TRACE_FXMIX, NULL, fxmix, 0, 0.0f);
    if (v != NULL) {
        levels_before_change(synth, v);
        v->fxmix = fxmix < 0 ? 0 : fxmix > 127 ? 127 : fxmix;
    }
}

int synth_voice_playing(const Synth *synth, int voice) {
    return voice >= 0 && voice < SYNTH_VOICES && synth->voice[voice].wave != NULL && !synth->voice[voice].ended;
}

/* Adds a voice's sub-frame to the dry and wet pairs. The resampler reads between the samples with the microcode's
 * four-point filter: the row for where it is between the second and third of the last four samples. */
static void voice_mix(Synth *synth, SynthVoice *v, int16_t *dry, int16_t *wet) {
    int32_t to_dry = synth->eqpower[v->fxmix];
    int32_t to_wet = synth->eqpower[127 - v->fxmix];
    int16_t *h = v->history;
    int32_t left = 0, right = 0;
    int i;

    if (v->levels.changed) {
        levels_start_ramp(synth, v);
    }
    v->levels.elapsed += SYNTH_SUBFRAME;
    for (i = 0; i < SYNTH_SUBFRAME && !v->ended; i++) {
        const int16_t *w = synth->resample[v->fraction >> 10];
        int32_t s = clamp16((h[0] * w[0] + h[1] * w[1] + h[2] * w[2] + h[3] * w[3]) >> 15);
        int32_t l, r;

        /* Every 8 samples the levels step, and hold for those 8. */
        if (i % 8 == 0) {
            levels_step(&v->levels);
            left = (int32_t)(v->levels.heard[0] >> 16);
            right = (int32_t)(v->levels.heard[1] >> 16);
        }
        /* Each side, then its dry and wet parts, added to the buses, which saturate at each voice as the N64's do. */
        l = mul15(s, left);
        r = mul15(s, right);
        dry[2 * i] = clamp16(dry[2 * i] + mul15(l, to_dry));
        dry[2 * i + 1] = clamp16(dry[2 * i + 1] + mul15(r, to_dry));
        wet[2 * i] = clamp16(wet[2 * i] + mul15(l, to_wet));
        wet[2 * i + 1] = clamp16(wet[2 * i + 1] + mul15(r, to_wet));
        v->fraction += v->step;
        while (v->fraction >= 0x10000u) {
            v->fraction -= 0x10000u;
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
        int16_t left = clamp16(wet[2 * i] + ((wet[2 * i] * REVERB_SELF) >> 15));

        in[i] = clamp16(left + ((wet[2 * i + 1] * REVERB_FROM_RIGHT) >> 15));
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

/* The length of the next audio frame: the recording's, or what the N64 would choose. At each video frame (every
 * retrace_samples samples of sound played), unless the audio interface has two buffers waiting, it is handed the frame
 * made last and a new one is made: 368 samples if the buffer playing has 265 or more left and the last frame was not
 * short, else 552. */
static int32_t next_frame_length(Synth *synth) {
    if (synth->frame_count > 0) {
        synth->frame_count--;
        return *synth->frame_lengths++;
    }
    for (;;) {
        double left;
        int32_t length;

        if (synth->ai.started) {
            double play = synth->retrace_samples;

            while (play > 0.0 && synth->ai.queued > 0) {
                double rest = synth->ai.queue[0] - synth->ai.played;

                if (play >= rest) {
                    play -= rest;
                    synth->ai.queue[0] = synth->ai.queue[1];
                    synth->ai.queued--;
                    synth->ai.played = 0.0;
                } else {
                    synth->ai.played += play;
                    play = 0.0;
                }
            }
        }
        synth->ai.started = 1;
        left = synth->ai.queued > 0 ? synth->ai.queue[0] - synth->ai.played : 0.0;
        if (synth->ai.queued >= 2) {
            continue;
        }
        if (synth->ai.made != 0) {
            synth->ai.queue[synth->ai.queued++] = synth->ai.made;
        }
        if (left >= 0x109 && synth->ai.short_ok) {
            synth->ai.short_ok = 0;
            length = 368;
        } else {
            synth->ai.short_ok = 1;
            length = 552;
        }
        synth->ai.made = length;
        return length;
    }
}

static void make_subframe(Synth *synth) {
    int16_t dry[SYNTH_SUBFRAME * 2];
    int16_t wet[SYNTH_SUBFRAME * 2];
    int i;

    if (synth->time == synth->frame_next) {
        synth->frame_start = synth->time;
        synth->frame_next += (uint64_t)next_frame_length(synth);
    }

    /* The player first. What it changes is heard from the start of the sub-frame nearest to its call's time, as n_audio
     * applies changes: so it is called here for the times from half a sub-frame before this one's start to half a
     * sub-frame after. A player that asks for no time at all is called again in the next. */
    while (synth->player != NULL && synth->player_due < SYNTH_SUBFRAME / 2) {
        int32_t us = synth->player(synth->player_context);

        /* The time to the next call, rounded to whole samples as n_audio does (16666 us is 480 samples). */
        synth->player_due += us > 0 ? (int32_t)((int64_t)us * SYNTH_RATE / 1000000.0 + 0.5) : SYNTH_SUBFRAME;
    }
    synth->player_due -= SYNTH_SUBFRAME;

    memset(dry, 0, sizeof(dry));
    memset(wet, 0, sizeof(wet));
    /* The voices from the last to the first, which matches the N64's sums where they saturate. */
    for (i = 0; i < SYNTH_VOICES; i++) {
        int k = SYNTH_VOICES - 1 - i;

        if (synth_voice_playing(synth, k)) {
            voice_mix(synth, &synth->voice[k], dry, wet);
        }
    }
    if (synth->line != NULL) {
        int16_t reverb[SYNTH_SUBFRAME];

        reverb_run(synth, wet, reverb);
        for (i = 0; i < SYNTH_SUBFRAME; i++) {
            int32_t r = (reverb[i] * REVERB_OUT) >> 15;

            dry[2 * i] = clamp16(dry[2 * i] + r);
            dry[2 * i + 1] = clamp16(dry[2 * i + 1] + r);
        }
    }
    memcpy(synth->out, dry, sizeof(dry));
    synth->out_left = SYNTH_SUBFRAME;
    synth->time += SYNTH_SUBFRAME;
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
