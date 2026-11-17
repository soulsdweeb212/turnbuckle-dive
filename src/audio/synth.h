/* The synthesizer: the port's own mixer, which does on the CPU what the N64's audio library (n_audio) and its audio
 * microcode on the RSP do (src/audio/synth.c). C and C++.
 *
 * It plays voices: each streams one sample of a sound bank (src/audio/sound_bank.h) from the ROM, decoding it a frame
 * at a time and going round its loop, at a pitch (how many of the sample's samples go by for each sample of output),
 * with a volume that moves to a new value over a given time, a pan and a reverb send. The voices are summed into a dry
 * pair of channels and a wet pair; the wet pair goes through the reverb, whose output joins the dry. It makes stereo
 * 16-bit sound at the N64's rate, in sub-frames of SYNTH_SUBFRAME samples, as the N64's audio library does.
 *
 * Whoever plays music on it (the music player, libmus on the N64) is called back as time goes by: the player says how
 * long until it wants its next call, and the synthesizer calls it at the start of the sub-frame in which that time
 * falls. What it changes then is heard from that sub-frame, which is how n_audio times its changes too.
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
    /* The resampler: the last four samples read and how far the output is between the second and third. */
    float history[4];
    float phase;
    float pitch;
    /* The gain (the volume squared, 0 to 32766) and where it is moving: by step a sample, for ramp more samples. */
    float volume;
    float volume_target;
    float volume_step;
    int32_t ramp;
    int pan;   /* 0 left, 64 middle, 127 right */
    int fxmix; /* 0 dry, 127 all to the reverb */
} SynthVoice;

typedef struct {
    const uint8_t *rom;
    size_t rom_size;
    int16_t eqpower[128]; /* the gains for a pan or a reverb send: [0] is 1, [127] is 0 (n_eqpower) */
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
    /* The sub-frame made last, and how much of it is still to be handed out. */
    int16_t out[SYNTH_SUBFRAME * 2];
    int out_left;
} Synth;

/* Sets up synth to play samples of rom, with eqpower's pan gains and the given reverb (copied). Returns 0, or -1 when
 * the reverb's line cannot be made. Free with synth_free. */
int synth_init(Synth *synth, const uint8_t *rom, size_t rom_size, const int16_t eqpower[128],
               const SynthReverb *reverb);
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

#ifdef __cplusplus
}
#endif

#endif
