/* The music player: the port's own player of the game's songs and sound effects, which does what the N64's music
 * library (libmus, Software Creations' MusicTools player, an early release) does, on the port's synthesizer
 * (src/audio/music.c). C and C++.
 *
 * Songs and effects are streams of notes and commands. A song has a header and up to 24 streams, one a channel, each
 * with a stream of volume changes and one of pitch bends beside it; an effect is one bare stream, found by number in the
 * game's effect table. The player has 24 channels, channel i always playing on synthesizer voice i; a song takes a
 * channel for each of its streams, an effect one. The synthesizer calls the player 60 times a second (a frame); each
 * frame every channel's clock moves on by its tempo, the channel reads the notes and commands that have come due, and
 * its envelope, vibrato and the rest set the voice's pitch, volume and pan. The game starts and stops sounds through
 * the functions below, which are libmus's, and gets a handle for each.
 *
 * How it behaves, field by field, is written up in the decompilation's notes (notes/sound_player_sequencer.md and
 * notes/sound_player_voices.md); the comments in music.c say what each part does. Everything runs on the game's
 * thread. */
#ifndef TD_MUSIC_H
#define TD_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#include "sound_bank.h"
#include "synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_CHANNELS SYNTH_VOICES
#define MUSIC_FRAMES 60      /* frames a second (the N64's NTSC rate) */
#define MUSIC_FRAME_US 16666 /* microseconds a frame: 1000000 / 60, as the N64 rounds it */
#define MUSIC_EFFECTS 210    /* the game's effect table */
#define MUSIC_LOOPS 4        /* loops one inside another */
#define MUSIC_QUEUE 64       /* the pause and unpause commands waiting for the next frame */

/* The classes MusStop and MusAsk take, and MusSetMasterVolume's. */
#define MUSIC_EFFECTS_CLASS 1
#define MUSIC_SONGS_CLASS 2

typedef struct {
    const uint8_t *data;   /* where the stream is (NULL: the channel is free) */
    const uint8_t *volume; /* the volume stream's place, and the bend stream's, at the loop's start */
    const uint8_t *bend;
    uint16_t volume_hold;
    uint16_t bend_hold;
    uint8_t count; /* times left; 0xFF: for ever */
    uint8_t volume_value;
    uint8_t bend_value; /* the bend value kept as a byte, so a negative one comes back 256 higher (the N64's) */
} MusicLoop;

/* A channel. The clocks count 1/256 of a tick; ticks are the unit of note lengths. */
typedef struct {
    /* What it plays, and where: the stream, the volume and bend streams beside it, and where each starts (for the
     * jump command). `end` is where the data it may read ends. */
    const uint8_t *data;
    const uint8_t *data_base;
    const uint8_t *volume_at;
    const uint8_t *volume_base;
    const uint8_t *bend_at;
    const uint8_t *bend_base;
    const uint8_t *end;
    const uint8_t *song; /* the song's header (NULL for an effect) */
    const uint8_t *drums;
    const SoundBank *bank;
    const SoundWave *pending; /* a wave to start at the next frame */
    int paused;
    uint32_t handle;
    int32_t priority;
    uint16_t effect; /* the effect's number; 0 for a song */

    /* Time. */
    uint64_t now;
    uint64_t next_note;
    uint64_t note_start;
    uint64_t release_at;
    uint64_t volume_clock;
    uint64_t bend_clock;
    uint64_t sweep_clock;
    uint16_t tempo;          /* clock gain a frame */
    int16_t unscaled_tempo;  /* the tempo before the handle's scale */
    int16_t tempo_scale;     /* 128: as written */
    uint16_t length;         /* the current note's, in ticks; 0x7FFF: for ever */
    uint16_t fixed_length;   /* 0: each note gives its own */
    uint8_t ignore_length;   /* the next note gives its own length after all */
    uint16_t ticks;          /* ticks since the note started (as of the last frame's end) */
    uint16_t volume_hold;    /* ticks left on the volume stream's value, and the bend stream's */
    uint16_t bend_hold;
    int32_t stop_countdown;  /* -1: not stopping */
    uint32_t stop_length;

    /* The note. */
    uint16_t wave;
    uint8_t velocity;
    uint8_t velocity_on;      /* each note carries a velocity */
    uint8_t default_velocity;
    int8_t transpose;
    uint8_t ignore_transpose;
    uint8_t retrigger_off;    /* notes do not start a new wave (tied notes) */
    float pitch;              /* note + the wave's tuning + transpose, semitones */
    float offset;             /* frequency offset + distortion, semitones */
    float distort;
    float glide_from;         /* portamento: the pitch it slides from, and where it has got to */
    float glide_at;
    uint8_t glide;            /* portamento length in ticks (0: off) */
    uint16_t cutoff;          /* release this many ticks after the note starts */
    uint16_t endit;           /* or this many before it ends */

    /* The envelope. */
    uint8_t env_speed;       /* frames between level changes */
    int32_t env_factor;      /* 1024 / speed (0 until an envelope is set: the level stays at the start) */
    uint8_t env_start, env_peak, env_sustain;
    uint8_t env_attack, env_decay, env_release; /* lengths in steps */
    float attack_step, decay_step, release_step;
    uint8_t env_phase;       /* 0 none, 1 attack, 2 decay, 3 sustain, 4 release, 5 done */
    uint8_t env_level;
    uint8_t env_countdown;
    uint8_t release_level;
    uint8_t env_off;         /* notes do not restart the envelope */

    /* Volume, pan and reverb. */
    uint8_t volume;
    int16_t volume_scale;    /* the handle's, 128: as written */
    uint16_t last_volume;    /* the last sent (0xFFFF: send again) */
    uint8_t pan;
    int16_t pan_scale;
    uint8_t last_pan;        /* 0xFF: send again */
    uint8_t reverb;
    uint8_t reverb_base;     /* the handle's */
    uint8_t last_reverb;     /* 0xFF: send again */
    float last_pitch;        /* 99.9: send again */

    /* Pitch bend, vibrato, wobble and pan sweep. */
    float bend_value;        /* -64 to 63 */
    float bend_range;        /* semitones a unit of bend */
    float bend;              /* semitones */
    uint8_t vib_delay;       /* ticks */
    uint8_t vib_speed;       /* ticks a cycle (0: off) */
    float vib_step;          /* radians a tick */
    float vib_depth;         /* semitones */
    float vib_value;
    int8_t wobble_depth;     /* semitones */
    uint8_t wobble_on, wobble_off, wobble_count; /* frames */
    int8_t wobble_value;
    uint8_t sweep_speed;     /* 0: off */
    uint8_t sweep_fraction;
    uint8_t sweep_down;

    MusicLoop loop[MUSIC_LOOPS];
    uint8_t loops;

    uint8_t sounding; /* the voice has been started and not stopped */
} MusicChannel;

typedef struct {
    Synth *synth;
    const SoundBank *songs_bank;   /* the songs' samples */
    const SoundBank *effects_bank; /* the effects' samples */
    const uint8_t *effect[MUSIC_EFFECTS]; /* each effect's stream (in the ROM), or NULL */
    const uint8_t *effects_end;
    int32_t effect_priority[MUSIC_EFFECTS];
    MusicChannel channel[MUSIC_CHANNELS];
    uint32_t next_handle;
    uint32_t random_seed;
    uint16_t master[2]; /* [0] effects, [1] songs; 0x7FFF is full */
    int mono;           /* centre the pans sent this frame (the game's func_8001613C) */
    struct {
        uint8_t command; /* 0 pause, 1 unpause */
        uint32_t handle;
    } queue[MUSIC_QUEUE];
    int queue_read, queue_write;
} Music;

/* Sets up the player on synth, with the two banks and the effect table from rom (which must stay loaded), and makes it
 * synth's player. Returns 0, or -1 with a message on stderr when the effect table is not the game's. */
int music_init(Music *music, Synth *synth, const SoundBank *songs, const SoundBank *effects, const uint8_t *rom,
               size_t rom_size);

/* libmus's functions, as the game calls them. A handle of 0 means no sound. */
uint32_t music_start_song(Music *music, const uint8_t *song, size_t size);
uint32_t music_start_effect(Music *music, int number, int volume, int pan, int restart, int priority);
void music_stop(Music *music, int classes, int frames);
int music_ask(const Music *music, int classes);
void music_set_master_volume(Music *music, int classes, int volume);
int music_handle_ask(const Music *music, uint32_t handle);
int music_handle_stop(Music *music, uint32_t handle, int frames);
int music_handle_set_volume(Music *music, uint32_t handle, int volume);
int music_handle_set_pan(Music *music, uint32_t handle, int pan);
int music_handle_set_reverb(Music *music, uint32_t handle, int reverb);
int music_handle_set_freq_offset(Music *music, uint32_t handle, float semitones);
int music_handle_set_tempo(Music *music, uint32_t handle, int scale);
int music_handle_pause(Music *music, uint32_t handle);
int music_handle_unpause(Music *music, uint32_t handle);
void music_set_mono(Music *music, int mono);

/* One frame of the player (the synthesizer calls it through music_init's registration); returns the microseconds to
 * the next. */
int32_t music_frame(void *music);

/* 2 to the power x as the N64's player works it out (a series that is exact to 0.015 cents within an octave either way
 * and goes sharp more than two octaves down). */
float music_power_of_2(float x);

#ifdef __cplusplus
}
#endif

#endif
