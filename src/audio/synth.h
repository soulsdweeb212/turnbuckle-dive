/* The synthesizer: the port's own mixer, which does on the CPU what the N64's audio library (n_audio) and its audio
 * microcode on the RSP do (src/audio/synth.c). C and C++.
 *
 * It plays voices: each streams one sample of a sound bank (src/audio/sound_bank.h) from the ROM, decoding it a frame
 * at a time and going round its loop, at a pitch (how many of the sample's samples go by for each sample of output,
 * read between the samples with the N64's four-point filter),
 * with a volume that moves to a new value over a given time, a pan and a reverb send. The voices are summed into a dry
 * pair of channels and a wet pair; the wet pair goes through the reverb, whose output joins the dry. It makes stereo
 * 16-bit sound at the N64's rate, in sub-frames of SYNTH_SUBFRAME samples, as the N64's audio library does.
 *
 * Whoever plays music on it (the music player, libmus on the N64) is called back as time goes by: the player says how
 * long until it wants its next call, and the synthesizer calls it at the start of the sub-frame nearest to that time.
 * What it changes then is heard from that sub-frame, which is how n_audio times its changes too; and a volume ramp
 * lasts the nearest whole number of sub-frames to its time, as n_audio's do.
 *
 * Everything here runs on the game's thread; nothing is shared with another. */
#ifndef TD_SYNTH_H
#define TD_SYNTH_H

#include <stddef.h>
#include <stdint.h>

#include "sound_bank.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_RATE 28800   /* the game's output rate (func_80003120's MusInitialize) */
#define SYNTH_VOICES 24    /* the game's number of voices (the same) */
#define SYNTH_SUBFRAME 184 /* samples a sub-frame, n_audio's */
#define SYNTH_MAX_PITCH 1.99996f /* n_audio's limit: a voice cannot go faster than about twice its sample's rate */
#define SYNTH_REVERB_SECTIONS 8
/* Samples played a video frame by the model of the N64's audio interface (Synth.retrace_samples): fitted to the
 * Project64 recording the port is compared with, whose frame lengths it matches nine times in ten. */
#define SYNTH_RETRACE_SAMPLES 481.09

/* The reverb, as the N64's audio library describes one (its parameter tables, such as the game's big room, which the
 * port reads from the ROM: src/audio/sound.c). One delay line of `length` samples, into which the wet sound is written,
 * and sections that each read it at two places (taps), `input` and `output` samples back:
 * - the output tap's block gains the input tap's times ffcoef (feed-forward);
 * - the input tap's block gains the output tap's times fbcoef (feedback), and is written back;
 * - when lowpass is not 0, the output tap's block goes through a one-pole low-pass filter (lowpass is the pole);
 * - the output tap's block is written back, and joins the reverb's output times gain.
 * With fbcoef = -ffcoef a section is an all-pass filter; with ffcoef 0 it is a comb. The coefficients have 15
 * fractional bits (32767 is 1). Sections that sway their output tap (the chorus and flange effects) are not supported:
 * the game's reverb has none. */
typedef struct {
    uint32_t input;
    uint32_t output;
    int16_t ffcoef;
    int16_t fbcoef;
    int16_t gain;
    int16_t lowpass;
} SynthReverbSection;

typedef struct {
    uint32_t length; /* 0: no reverb */
    int sections;
    SynthReverbSection section[SYNTH_REVERB_SECTIONS];
} SynthReverb;

/* A player: called when its time comes, returns the microseconds until it wants to be called again. */
typedef int32_t (*SynthPlayer)(void *context);

/* What a voice was told, for a trace (--sound-replay): the wave for a start; the volume and its ramp in microseconds; the
 * pitch; the pan or the send in `value`. */
typedef enum { SYNTH_TRACE_START, SYNTH_TRACE_STOP, SYNTH_TRACE_VOLUME, SYNTH_TRACE_PITCH, SYNTH_TRACE_PAN, SYNTH_TRACE_FXMIX } SynthTraceOp;
typedef void (*SynthTrace)(void *context, int voice, SynthTraceOp op, const SoundWave *wave, int32_t value,
                           int32_t ramp_us, float pitch);

/* A voice's volume, as n_audio's envelope mixer keeps it. Levels have 15 fractional bits (32767 is 1). The volume
 * it is given is squared into `level`; the left and right levels are the level times the pan's gains, and move to new
 * values in straight lines, a step every 8 samples, over a ramp of whole sub-frames.
 *
 * Changes (volume, pan, send, pitch, start, stop) reach a voice in groups, one a sub-frame boundary, and the mixer
 * works out where the levels are when each comes: the first of a group finds them where the ramp has got to, unless
 * it comes at the start of an audio frame (below), when it finds them already at the ramp's end; every later change
 * in the same group finds them at the end too. So a volume that changes with its pan, or with its pitch (vibrato),
 * jumps rather than slides; that is how the N64 sounds. Stopping a voice sets its level to almost nothing (1). */
typedef struct {
    int32_t level;       /* the volume squared */
    int32_t from[2];     /* the left and right levels where the ramp starts */
    int32_t to[2];       /* and where it ends */
    int32_t rate[2];     /* the step every 8 samples, 16 fractional bits */
    int32_t ramp_length; /* samples */
    int32_t elapsed;     /* samples since the ramp started */
    int64_t heard[2];    /* the levels being heard, 16 fractional bits */
    int changed;         /* a change waits for the next sub-frame */
    uint64_t group;      /* 1 + the time of the boundary of the last change */
} SynthLevels;

typedef struct {
    const SoundWave *wave; /* NULL: the voice is free */
    int ended;             /* the sample has played to its end (the voice is silent) */
    /* The stream: the next sample to read, the frame decoded last (its number and its 16 samples) and the decoder's
     * state, and how many times the loop has been gone round. */
    uint32_t position;
    uint32_t frame_number;
    int16_t frame[16];
    int16_t state[16];
    uint32_t loops;
    /* The resampler: the last four samples read (zeros at the start), how far the output is between the second and
     * third (16 fractional
     * bits) and how far it moves a sample: the pitch with 16 fractional bits, made from n_audio's 15. */
    int16_t history[4];
    uint32_t fraction;
    uint32_t step;
    float pitch;
    SynthLevels levels;
    int pan;   /* 0 left, 64 middle, 127 right */
    int fxmix; /* 0 dry, 127 all to the reverb */
} SynthVoice;

typedef struct {
    const uint8_t *rom;
    size_t rom_size;
    int16_t eqpower[128]; /* the gains for a pan or a reverb send: [0] is 1, [127] is 0 (n_eqpower) */
    /* The resampler's filter, the audio microcode's: for each of 64 places between two samples, the weights (15
     * fractional bits) of the sample before, the two either side and the one after. Even between none (place 0) it
     * mixes in the neighbours a little, which softens the highest sounds as the N64 does. */
    int16_t resample[64][4];
    SynthVoice voice[SYNTH_VOICES];
    /* The reverb's delay line and where the next sub-frame is written in it, and the low-pass filters' last values. */
    SynthReverb reverb;
    int16_t *line;
    uint32_t line_at;
    float lowpass_last[SYNTH_REVERB_SECTIONS];
    /* The player, and the time of its next call in samples from the start of the sub-frame being made (each call's time
     * rounded to whole samples). */
    SynthPlayer player;
    void *player_context;
    double player_due;
    /* Time, in samples from the start: the start of the sub-frame being made, and the audio frames. The N64 makes its
     * sound in frames of 552 or 368 samples (three sub-frames or two), the length chosen at each video frame by how
     * much the audio interface still has to play; a change that comes at a frame's start finds a voice's levels at
     * the ramp's end (above). The lengths come from frame_lengths while it has any (a recording of the N64's), and
     * then from a model of the audio interface that plays retrace_samples samples a video frame. */
    uint64_t time;
    uint64_t frame_start;
    uint64_t frame_next;
    const uint16_t *frame_lengths;
    size_t frame_count;
    double retrace_samples;
    struct {
        int32_t queue[2];
        int queued;
        double played;
        int short_ok;
        int32_t made;
        int started;
    } ai;
    /* A trace of every voice change, when set. */
    SynthTrace trace;
    void *trace_context;
    /* The sub-frame made last, and how much of it is still to be handed out. */
    int16_t out[SYNTH_SUBFRAME * 2];
    int out_left;
} Synth;

/* Sets up synth to play samples of rom, with eqpower's pan gains, the resampler's filter (64 rows of 4) and the given
 * reverb (copied). Returns 0, or -1 when
 * the reverb's line cannot be made. Free with synth_free. */
int synth_init(Synth *synth, const uint8_t *rom, size_t rom_size, const int16_t eqpower[128],
               const int16_t resample[256], const SynthReverb *reverb);
void synth_free(Synth *synth);

void synth_set_player(Synth *synth, SynthPlayer player, void *context);

/* The voices, by number (0 to SYNTH_VOICES - 1). Volume is 0 to 32767, and the gain is its square (32767 is 1, 16384
 * a quarter), as n_audio's; pan and fxmix are 0 to 127.
 * synth_voice_play starts a wave from its beginning and keeps the voice's pitch, volume, pan and send, as n_audio's
 * start does (the music player sets them apart). synth_voice_start also sets them, its volume moving from 0 over
 * ramp_us microseconds (n_alSynStartVoiceParams). */
void synth_voice_play(Synth *synth, int voice, const SoundWave *wave);
void synth_voice_start(Synth *synth, int voice, const SoundWave *wave, float pitch, int16_t volume, int pan, int fxmix,
                       int32_t ramp_us);
void synth_voice_stop(Synth *synth, int voice);
void synth_voice_set_pitch(Synth *synth, int voice, float pitch);
void synth_voice_set_volume(Synth *synth, int voice, int16_t volume, int32_t ramp_us);
void synth_voice_set_pan(Synth *synth, int voice, int pan);
void synth_voice_set_fxmix(Synth *synth, int voice, int fxmix);
/* Whether the voice is playing: started, not stopped, and its sample not played to its end. */
int synth_voice_playing(const Synth *synth, int voice);

/* Makes `frames` stereo samples (left, right) into out. */
void synth_render(Synth *synth, int16_t *out, int frames);
/* Whether the next sub-frame synth_render makes starts with a call to the player (none of the last one's output is
 * left). */
int synth_player_next(const Synth *synth);

#ifdef __cplusplus
}
#endif

#endif
