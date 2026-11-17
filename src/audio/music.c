/* The music player (src/audio/music.h). */
#include "music.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The effect table and the effects' priorities, in main's data: RAM addresses minus 0x7FFFF400. The table holds the
 * RAM addresses of the effects' streams, which lie between EFFECTS_DATA and the table itself. */
#define MAIN_RAM_TO_ROM 0x7FFFF400u
#define EFFECTS_DATA 0x8002EEE0u
#define EFFECT_TABLE 0x8002FECCu
#define EFFECT_PRIORITIES 0x80030214u

#define TICK 256u                        /* clock units a tick */
#define DEFAULT_TEMPO (0x6000 / MUSIC_FRAMES) /* a tempo of 120 */
#define REST 0x60
#define ENDLESS 0x7FFF
#define NO_PITCH 99.9f

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- Reading a stream ----
 * A channel may only read up to its `end`; a stream that runs off its data ends as if it had stopped. */

static int can_read(const MusicChannel *c, const uint8_t *p, size_t n) {
    return p != NULL && p < c->end && (size_t)(c->end - p) >= n;
}

static uint8_t next_byte(MusicChannel *c, const uint8_t **p, int *bad) {
    if (!can_read(c, *p, 1)) {
        *bad = 1;
        return 0;
    }
    return *(*p)++;
}

/* A length: one byte below 0x80, or two giving 15 bits. */
static uint16_t next_length(MusicChannel *c, const uint8_t **p, int *bad) {
    uint8_t b = next_byte(c, p, bad);

    if (b < 0x80) {
        return b;
    }
    return (uint16_t)((b & 0x7F) << 8 | next_byte(c, p, bad));
}

/* ---- The pieces ---- */

float music_power_of_2(float x) {
    /* e to the power x ln 2, its series to the sixth power, in double with x's powers in single precision; for a
     * negative x, 1 over the value at -x. */
    static const double k[6] = { 0.693147180559945, 0.240226506959101, 0.0555041086648216, 0.00961812910762848,
                                 0.00133335581464284, 0.000154035303933816 };
    float a = x < 0.0f ? -x : x;
    float power = a;
    double sum = 1.0;
    int i;

    if (x == 0.0f) {
        return 1.0f;
    }
    for (i = 0; i < 6; i++) {
        sum += k[i] * power;
        power *= a;
    }
    return x < 0.0f ? (float)(1.0 / (float)sum) : (float)sum;
}

/* A random number between about -range/2 and +range/2, from a seed shared by every channel: each call shifts it left
 * eight times, the bit shifted in being the one that was at bit 27 (so it repeats every 7 calls, as the N64's). */
static int random_value(Music *music, int range) {
    int i;

    for (i = 0; i < 8; i++) {
        music->random_seed = music->random_seed << 1 | (music->random_seed >> 27 & 1);
    }
    return (int)((float)range * (((float)(int32_t)music->random_seed / 65536.0f) / 65536.0f));
}

static int tempo_of(int bpm) {
    return bpm * 0x6000 / 120 / MUSIC_FRAMES;
}

/* A channel as reset for a new song line or effect. Whether its voice is sounding carries over. */
static void channel_reset(Music *music, MusicChannel *c, const SoundBank *bank) {
    uint8_t sounding = c->sounding;

    (void)music;
    memset(c, 0, sizeof(*c));
    c->sounding = sounding;
    c->bank = bank;
    c->stop_countdown = -1;
    c->last_pitch = NO_PITCH;
    c->attack_step = 1.0f;
    c->decay_step = 1.0f / 255.0f;
    c->release_step = 1.0f / 15.0f;
    c->bend_range = 1.0f / 32.0f;
    c->tempo_scale = 128;
    c->length = 1;
    c->tempo = DEFAULT_TEMPO;
    c->unscaled_tempo = DEFAULT_TEMPO;
    c->volume_scale = 128;
    c->pan_scale = 128;
    c->last_volume = 0xFFFF;
    c->volume_hold = 1;
    c->bend_hold = 1;
    c->volume = 127;
    c->pan = 64;
    c->last_pan = 0xFF;
    c->env_speed = 1;
    c->env_peak = 127;
    c->env_sustain = 127;
    c->env_attack = 1;
    c->env_decay = 255;
    c->env_release = 15;
    c->last_reverb = 0xFF;
    c->default_velocity = 127;
}

/* Lets a channel go: no stream, no sound. */
static void channel_free(Music *music, int index) {
    MusicChannel *c = &music->channel[index];

    c->data = NULL;
    c->volume_at = NULL;
    c->bend_at = NULL;
    c->song = NULL;
    c->effect = 0;
    c->handle = 0;
    c->pending = NULL;
    if (c->sounding) {
        c->sounding = 0;
        synth_voice_stop(music->synth, index);
    }
}

/* An envelope's seven bytes: speed, start level, attack length, peak level, decay length, sustain level, release
 * length. The steps are the level change a step (attack and decay) and the fraction of the level lost a step
 * (release). */
static void set_envelope(MusicChannel *c, const uint8_t e[7]) {
    c->env_speed = e[0] != 0 ? e[0] : 1;
    c->env_factor = 0x400 / c->env_speed;
    c->env_start = e[1];
    c->env_attack = e[2];
    c->env_peak = e[3];
    c->attack_step = (float)(c->env_peak - c->env_start) * (float)(1.0 / c->env_attack);
    c->env_decay = e[4];
    c->env_sustain = e[5];
    c->decay_step = (float)(c->env_sustain - c->env_peak) * (float)(1.0 / c->env_decay);
    c->env_release = e[6];
    c->release_step = (float)(1.0 / c->env_release);
}

static void set_envelope_number(MusicChannel *c, unsigned number) {
    const uint8_t *e;

    if (c->song == NULL) {
        return; /* an effect has no envelope table */
    }
    e = c->song + be32(c->song + 0x10) + 7 * (size_t)number;
    if (can_read(c, e, 7)) {
        set_envelope(c, e);
    }
}

/* Starts the channel's pending wave on its voice (which keeps its pitch, volume, pan and send). */
static void start_pending(Music *music, int index) {
    MusicChannel *c = &music->channel[index];

    if (c->sounding) {
        synth_voice_stop(music->synth, index);
    }
    c->sounding = 1;
    synth_voice_play(music->synth, index, c->pending);
    c->pending = NULL;
}

/* ---- Starting sounds ---- */

/* A channel for an effect with priority `priority`: with `restart`, the first already playing that effect; else the
 * first free one, or failing that the effect channel of lowest priority (the first of equals) if it is below
 * `priority`. Songs' channels are never taken. -1: none. */
static int find_effect_channel(const Music *music, int number, int restart, int32_t priority) {
    int best = -1;
    int i;

    if (restart) {
        for (i = 0; i < MUSIC_CHANNELS; i++) {
            if (music->channel[i].effect == number) {
                return i;
            }
        }
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        const MusicChannel *c = &music->channel[i];

        if (c->data == NULL) {
            return i;
        }
        if (c->effect != 0 && (best < 0 || c->priority < music->channel[best].priority)) {
            best = i;
        }
    }
    return best >= 0 && music->channel[best].priority < priority ? best : -1;
}

uint32_t music_start_effect(Music *music, int number, int volume, int pan, int restart, int priority) {
    int index;
    MusicChannel *c;

    if (number < 0 || number >= MUSIC_EFFECTS || music->effect[number] == NULL) {
        return 0;
    }
    if (priority == -1) {
        priority = music->effect_priority[number];
    }
    index = find_effect_channel(music, number, restart, priority);
    if (index < 0) {
        return 0;
    }
    c = &music->channel[index];
    channel_reset(music, c, music->effects_bank);
    c->effect = (uint16_t)number;
    c->volume_scale = (int16_t)volume;
    c->pan_scale = (int16_t)pan;
    c->priority = priority;
    c->handle = music->next_handle++;
    c->data = c->data_base = music->effect[number];
    c->end = music->effects_end;
    return c->handle;
}

/* A channel for line k of a song: the first free one; else the effect channel of lowest priority (the last of
 * equals); else channel 0 if it plays another song (the N64's search looks at no other); else the one this song's
 * line k already has; else channel k. */
static int find_song_channel(const Music *music, const uint8_t *song, const uint8_t *line, int k) {
    int best = -1;
    int i;

    for (i = 0; i < MUSIC_CHANNELS; i++) {
        if (music->channel[i].data == NULL) {
            return i;
        }
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        const MusicChannel *c = &music->channel[i];

        if (c->effect != 0 && (best < 0 || c->priority <= music->channel[best].priority)) {
            best = i;
        }
    }
    if (best >= 0) {
        return best;
    }
    if (music->channel[0].effect == 0 && music->channel[0].song != song) {
        return 0;
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        if (music->channel[i].song == song && music->channel[i].data_base == line) {
            return i;
        }
    }
    return k % MUSIC_CHANNELS;
}

static void queue_add(Music *music, uint8_t command, uint32_t handle) {
    int next = (music->queue_write + 1) % MUSIC_QUEUE;

    if (next == music->queue_read) {
        return; /* full: the command is lost, as on the N64 */
    }
    music->queue[music->queue_write].command = command;
    music->queue[music->queue_write].handle = handle;
    music->queue_write = next;
}

/* The song's header: the number of lines, then the offsets (from the song's start) of three tables of one word a
 * line (the lines' streams, their volume streams and their bend streams; 0 for none), of the envelope table and of the
 * drum table. All big-endian. The lines are made ready paused and all start together at the next frame. */
uint32_t music_start_song(Music *music, const uint8_t *song, size_t size) {
    uint32_t handle = music->next_handle++;
    uint32_t lines, k;
    uint32_t tables[3];

    if (song == NULL || size < 0x18) {
        return handle;
    }
    lines = be32(song);
    for (k = 0; k < 3; k++) {
        tables[k] = be32(song + 4 + 4 * k);
        if (tables[k] > size || (size - tables[k]) / 4 < lines) {
            fprintf(stderr, "music: the song's header is broken\n");
            return handle;
        }
    }
    if (be32(song + 0x10) > size || be32(song + 0x14) > size) {
        fprintf(stderr, "music: the song's header is broken\n");
        return handle;
    }
    for (k = 0; k < lines; k++) {
        uint32_t at = be32(song + tables[0] + 4 * k);
        uint32_t volume = be32(song + tables[1] + 4 * k);
        uint32_t bend = be32(song + tables[2] + 4 * k);
        MusicChannel *c;
        int index;

        if (at == 0 || at >= size) {
            continue;
        }
        index = find_song_channel(music, song, song + at, (int)k);
        c = &music->channel[index];
        channel_reset(music, c, music->songs_bank);
        c->paused = 1;
        c->song = song;
        c->end = song + size;
        c->data = c->data_base = song + at;
        c->volume_at = c->volume_base = volume != 0 && volume < size ? song + volume : NULL;
        c->bend_at = c->bend_base = bend != 0 && bend < size ? song + bend : NULL;
        c->handle = handle;
    }
    queue_add(music, 1, handle);
    return handle;
}

/* ---- Stopping, asking and changing ---- */

static void stop_channel(MusicChannel *c, int frames) {
    if (c->paused) {
        c->paused = 0;
        c->stop_length = 1;
        c->stop_countdown = 0;
    } else {
        c->stop_length = frames != 0 ? (uint32_t)frames : 1;
        c->stop_countdown = frames;
    }
}

static int in_classes(const MusicChannel *c, int classes) {
    return c->effect != 0 ? (classes & MUSIC_EFFECTS_CLASS) != 0 : (classes & MUSIC_SONGS_CLASS) != 0;
}

void music_stop(Music *music, int classes, int frames) {
    int i;

    for (i = 0; i < MUSIC_CHANNELS; i++) {
        MusicChannel *c = &music->channel[i];

        if (c->data != NULL && c->stop_countdown == -1 && in_classes(c, classes)) {
            stop_channel(c, frames);
        }
    }
}

int music_ask(const Music *music, int classes) {
    int n = 0;
    int i;

    for (i = 0; i < MUSIC_CHANNELS; i++) {
        n += music->channel[i].data != NULL && in_classes(&music->channel[i], classes);
    }
    return n;
}

void music_set_master_volume(Music *music, int classes, int volume) {
    if (classes & MUSIC_EFFECTS_CLASS) {
        music->master[0] = (uint16_t)volume;
    }
    if (classes & MUSIC_SONGS_CLASS) {
        music->master[1] = (uint16_t)volume;
    }
}

int music_handle_ask(const Music *music, uint32_t handle) {
    int n = 0;
    int i;

    if (handle == 0) {
        return 0;
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        n += music->channel[i].handle == handle;
    }
    return n;
}

int music_handle_stop(Music *music, uint32_t handle, int frames) {
    int n = 0;
    int i;

    if (handle == 0) {
        return 0;
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        MusicChannel *c = &music->channel[i];

        if (c->handle == handle && c->stop_countdown == -1) {
            stop_channel(c, frames);
            n++;
        }
    }
    return n;
}

/* The handle changes: each returns how many channels it changed. */
#define FOR_HANDLE(music, handle, body)                                                                                \
    int n = 0;                                                                                                         \
    int i;                                                                                                             \
    if (handle == 0) {                                                                                                 \
        return 0;                                                                                                      \
    }                                                                                                                  \
    for (i = 0; i < MUSIC_CHANNELS; i++) {                                                                             \
        MusicChannel *c = &(music)->channel[i];                                                                        \
        if (c->handle == handle) {                                                                                     \
            body;                                                                                                      \
            n++;                                                                                                       \
        }                                                                                                              \
    }                                                                                                                  \
    return n

int music_handle_set_volume(Music *music, uint32_t handle, int volume) {
    FOR_HANDLE(music, handle, c->volume_scale = (int16_t)volume);
}

int music_handle_set_pan(Music *music, uint32_t handle, int pan) {
    FOR_HANDLE(music, handle, (c->pan_scale = (int16_t)pan, c->last_pan = 0xFF));
}

/* The handle's reverb is a floor under the channels' own: it is heard from each channel's next note. */
int music_handle_set_reverb(Music *music, uint32_t handle, int reverb) {
    int r = reverb < 0 ? 0 : reverb > 127 ? 127 : reverb;

    FOR_HANDLE(music, handle, (c->reverb_base = (uint8_t)r, c->last_reverb = 0xFF));
}

int music_handle_set_freq_offset(Music *music, uint32_t handle, float semitones) {
    FOR_HANDLE(music, handle, c->offset = semitones + c->distort);
}

int music_handle_set_tempo(Music *music, uint32_t handle, int scale) {
    int s = scale < 1 ? 1 : scale > 256 ? 256 : scale;

    FOR_HANDLE(music, handle, (c->tempo_scale = (int16_t)s, c->tempo = (uint16_t)((c->unscaled_tempo * s) >> 7)));
}

/* Pausing and unpausing wait for the next frame. A paused channel stands still, its voice sounding on. */
int music_handle_pause(Music *music, uint32_t handle) {
    queue_add(music, 0, handle);
    return 1;
}

int music_handle_unpause(Music *music, uint32_t handle) {
    queue_add(music, 1, handle);
    return 1;
}

void music_set_mono(Music *music, int mono) {
    music->mono = mono;
}

/* ---- The commands of a stream ----
 * Each reads its arguments after the command byte. A command byte is 0x80 and up; the game's streams use 0x80 to
 * 0xAA. */

static void loop_start(MusicChannel *c, uint8_t count) {
    MusicLoop *l;

    if (c->loops >= MUSIC_LOOPS) {
        return;
    }
    l = &c->loop[c->loops++];
    l->count = count;
    l->data = c->data;
    l->volume = c->volume_at;
    l->bend = c->bend_at;
    l->volume_hold = c->volume_hold;
    l->bend_hold = c->bend_hold;
    l->volume_value = c->volume;
    l->bend_value = (uint8_t)(int)c->bend_value;
}

/* The end of a loop: round again (a count of 0xFF is for ever), or on past it. Either way the pan goes back to the
 * middle, the bend range to its default and everything is sent again; the stream sets them again where it needs. */
static void loop_end(MusicChannel *c) {
    if (c->loops > 0) {
        MusicLoop *l = &c->loop[c->loops - 1];

        if (l->count != 0xFF) {
            l->count--;
        }
        if (l->count == 0) {
            c->loops--;
        } else {
            c->data = l->data;
            c->volume_at = l->volume;
            c->bend_at = l->bend;
            c->volume = l->volume_value;
            c->volume_hold = l->volume_hold;
            c->bend_hold = l->bend_hold;
            c->bend_value = (float)l->bend_value;
            c->bend = c->bend_value * c->bend_range;
        }
    }
    c->last_volume = 0xFFFF;
    c->last_reverb = 0xFF;
    c->last_pan = 0xFF;
    c->pan = 64;
    c->last_pitch = NO_PITCH;
    c->bend_range = 1.0f / 32.0f;
}

/* Runs the command `op`; returns 0 when the stream has stopped. */
static int run_command(Music *music, int index, uint8_t op, int *bad) {
    MusicChannel *c = &music->channel[index];
    const uint8_t **p = &c->data;
    uint8_t a, b;
    int i;

    switch (op) {
    case 0x80: /* stop */
        c->volume_at = NULL;
        c->bend_at = NULL;
        c->song = NULL;
        c->effect = 0;
        c->handle = 0;
        c->pending = NULL;
        return 0;
    case 0x81: /* wave */
        c->wave = next_length(c, p, bad);
        break;
    case 0x82: /* portamento: its length in ticks, from the note now playing */
        c->glide = next_byte(c, p, bad);
        if (c->glide != 0) {
            c->glide_at = c->pitch;
        }
        break;
    case 0x83: /* portamento off */
        c->glide = 0;
        break;
    case 0x84: { /* an envelope given here */
        uint8_t e[7];

        for (i = 0; i < 7; i++) {
            e[i] = next_byte(c, p, bad);
        }
        if (!*bad) {
            set_envelope(c, e);
        }
        break;
    }
    case 0x85: { /* tempo: an effect's own, or every line of the song's */
        int t = tempo_of(next_byte(c, p, bad));

        if (c->effect != 0) {
            c->tempo = (uint16_t)t;
        } else {
            int16_t scale = c->tempo_scale;

            for (i = 0; i < MUSIC_CHANNELS; i++) {
                MusicChannel *o = &music->channel[i];

                if (o->song == c->song) {
                    o->unscaled_tempo = (int16_t)t;
                    o->tempo = (uint16_t)((t * scale) >> 7);
                }
            }
        }
        break;
    }
    case 0x86: /* release this many ticks after the note starts */
        a = next_byte(c, p, bad);
        b = next_byte(c, p, bad);
        c->cutoff = (uint16_t)(a << 8 | b);
        c->endit = 0;
        break;
    case 0x87: /* release this many ticks before the note ends */
        c->endit = next_byte(c, p, bad);
        c->cutoff = 0;
        break;
    case 0x88: /* vibrato: delay, ticks a cycle, depth in fiftieths of a semitone */
    case 0x89: /* the same, starting downward */
        c->vib_delay = next_byte(c, p, bad);
        c->vib_speed = next_byte(c, p, bad);
        c->vib_step = 6.2831852f / (float)c->vib_speed;
        c->vib_depth = (float)next_byte(c, p, bad) / 50.0f;
        if (op == 0x89) {
            c->vib_depth = -c->vib_depth;
        }
        break;
    case 0x8A: /* vibrato off */
        c->vib_speed = 0;
        c->vib_value = 0.0f;
        break;
    case 0x8B: /* every note this long (0: each gives its own) */
        c->fixed_length = next_length(c, p, bad);
        break;
    case 0x8C: /* the next note gives its own length */
        c->ignore_length = 1;
        break;
    case 0x8D: /* transpose */
        c->transpose = (int8_t)next_byte(c, p, bad);
        break;
    case 0x8E: /* transpose no more */
        c->ignore_transpose = 1;
        break;
    case 0x8F: { /* detune, in hundredths of a semitone */
        float d = (float)(int8_t)next_byte(c, p, bad) / 100.0f;

        c->offset = c->offset - c->distort + d;
        c->distort = d;
        break;
    }
    case 0x90: /* an envelope from the song's table */
        set_envelope_number(c, next_length(c, p, bad));
        break;
    case 0x91: /* notes leave the envelope alone */
        c->env_off = 1;
        break;
    case 0x92:
        c->env_off = 0;
        break;
    case 0x93: /* notes keep the wave playing (tied) */
        c->retrigger_off = 1;
        break;
    case 0x94:
        c->retrigger_off = 0;
        break;
    case 0x95: /* a loop: this many times (0xFF for ever) */
        a = next_byte(c, p, bad);
        loop_start(c, a);
        break;
    case 0x96: /* the loop's end */
        loop_end(c);
        break;
    case 0x97: /* wobble: semitones, frames on, frames off */
        c->wobble_depth = (int8_t)next_byte(c, p, bad);
        c->wobble_on = next_byte(c, p, bad);
        c->wobble_off = next_byte(c, p, bad);
        break;
    case 0x98:
        c->wobble_on = 0;
        break;
    case 0x99: /* each note carries a velocity */
        c->velocity_on = 1;
        break;
    case 0x9A:
        c->velocity_on = 0;
        break;
    case 0x9B: /* every note this velocity */
        c->velocity_on = 0;
        c->default_velocity = next_byte(c, p, bad);
        break;
    case 0x9C: /* pan, 0 to 255 */
        c->pan = next_byte(c, p, bad) >> 1;
        c->last_pan = 0xFF;
        break;
    case 0x9D: /* stereo: not used by this player */
        next_byte(c, p, bad);
        next_byte(c, p, bad);
        break;
    case 0x9E: /* drums: a map from the song's drum table, when it has one (else the byte is not this command's) */
        if (c->song != NULL && be32(c->song + 0x14) != be32(c->song + 0x10)) {
            const uint8_t *entry = c->song + be32(c->song + 0x14) + 4 * (size_t)next_byte(c, p, bad);

            c->drums = can_read(c, entry, 4) ? c->song + be32(entry) : NULL;
        } else {
            c->drums = NULL;
        }
        break;
    case 0x9F:
        c->drums = NULL;
        break;
    case 0xA0: /* a message for the composer's tools */
        next_byte(c, p, bad);
        break;
    case 0xA1: { /* jump: to these offsets of the stream and of the volume and bend streams */
        uint16_t to[3];

        for (i = 0; i < 3; i++) {
            a = next_byte(c, p, bad);
            to[i] = (uint16_t)(a << 8 | next_byte(c, p, bad));
        }
        if (!*bad) {
            c->data = c->data_base + to[0];
            c->volume_at = c->volume_base != NULL ? c->volume_base + to[1] : NULL;
            c->bend_at = c->bend_base != NULL ? c->bend_base + to[2] : NULL;
            c->volume_hold = 1;
            c->bend_hold = 1;
        }
        break;
    }
    case 0xA2: /* reverb, from the next note */
        c->reverb = next_byte(c, p, bad);
        break;
    case 0xA3: /* a random transpose: range, base */
        a = next_byte(c, p, bad);
        b = next_byte(c, p, bad);
        c->transpose = (int8_t)(random_value(music, a) + b);
        break;
    case 0xA4: /* a random volume */
        a = next_byte(c, p, bad);
        b = next_byte(c, p, bad);
        c->volume = (uint8_t)(random_value(music, a) + b);
        break;
    case 0xA5: /* a random pan */
        a = next_byte(c, p, bad);
        b = next_byte(c, p, bad);
        c->pan = (uint8_t)(random_value(music, a) + b);
        break;
    case 0xA6: /* volume */
        c->volume = next_byte(c, p, bad);
        break;
    case 0xA7: { /* start another effect, which answers to this one's handle */
        uint16_t number = next_length(c, p, bad);
        uint32_t handle;

        if (*bad) {
            break;
        }
        /* At this channel's priority; meanwhile the channel's own is one higher, so the new effect cannot take it. */
        c->priority++;
        handle = music_start_effect(music, number, c->volume, c->pan, 0, c->priority - 1);
        c->priority--;
        if (handle != 0) {
            for (i = 0; i < MUSIC_CHANNELS; i++) {
                if (music->channel[i].handle == handle) {
                    music->channel[i].handle = c->handle;
                    music->channel[i].bank = c->bank;
                }
            }
        }
        break;
    }
    case 0xA8: { /* bend range, in 64ths of a semitone a unit (the old player squares the bend for now) */
        c->bend_range = (float)next_byte(c, p, bad) * (1.0f / 64.0f);
        c->bend = c->bend_value * c->bend_value;
        break;
    }
    case 0xA9: /* pan sweep */
        c->sweep_speed = next_byte(c, p, bad);
        break;
    case 0xAA: /* change the reverb type: not in this player */
        next_byte(c, p, bad);
        break;
    default:
        fprintf(stderr, "music: unknown command 0x%02X; the channel stops\n", op);
        *bad = 1;
        break;
    }
    return 1;
}

/* ---- Notes ---- */

/* When the note's envelope releases: never for an endless note; else `cutoff` ticks after its start, or `endit`
 * ticks before its end. */
static void start_envelope(MusicChannel *c) {
    if (c->length == ENDLESS) {
        c->release_at = 0x7FFFFFFF;
    } else if (c->cutoff != 0) {
        c->release_at = c->note_start + (uint64_t)c->cutoff * TICK;
    } else {
        c->release_at = c->next_note - (uint64_t)c->endit * TICK;
    }
    c->env_phase = 1;
    c->env_level = c->env_start;
    c->env_countdown = c->env_speed;
}

static void stream_ended(Music *music, int index) {
    MusicChannel *c = &music->channel[index];

    c->data = NULL;
    if (c->sounding) {
        c->sounding = 0;
        synth_voice_set_volume(music->synth, index, 0, 0);
        synth_voice_stop(music->synth, index);
    }
}

/* Reads commands up to the next note and starts it. */
static void read_note(Music *music, int index) {
    MusicChannel *c = &music->channel[index];
    int bad = 0;
    uint8_t note = 0;

    for (;;) {
        uint8_t op = next_byte(c, &c->data, &bad);

        if (bad) {
            break;
        }
        if (op < 0x80) {
            note = op;
            break;
        }
        if (!run_command(music, index, op, &bad) || bad) {
            bad = 1;
            break;
        }
    }
    if (bad) {
        stream_ended(music, index);
        return;
    }
    c->glide_from = c->glide_at;
    c->velocity = c->velocity_on ? next_byte(c, &c->data, &bad) : c->default_velocity;
    if (c->fixed_length != 0) {
        if (c->ignore_length) {
            c->ignore_length = 0;
            c->length = next_length(c, &c->data, &bad);
        } else {
            c->length = c->fixed_length;
        }
    } else {
        c->length = next_length(c, &c->data, &bad);
    }
    if (bad) {
        stream_ended(music, index);
        return;
    }
    c->ticks = 0;
    c->wobble_value = 0;
    c->note_start = c->next_note;
    c->wobble_count = c->wobble_off;
    c->next_note += (uint64_t)c->length * TICK;

    if (note == REST) {
        /* A rest lets the note before it go into its release. */
        if (c->env_phase < 4) {
            c->env_phase = 4;
            c->env_countdown = 1;
            c->release_at = c->now;
            c->release_level = c->env_level;
        }
        return;
    }
    if (c->drums != NULL) {
        const uint8_t *d = c->drums + 4 * (size_t)note;

        if (can_read(c, d, 4)) {
            c->wave = d[0];
            set_envelope_number(c, d[1]);
            c->pan = d[2] >> 1;
            note = d[3];
        }
    }
    if (!c->env_off) {
        start_envelope(c);
    }
    if (c->sweep_speed != 0) {
        c->sweep_clock = c->note_start;
        c->sweep_fraction = 0;
        c->sweep_down = c->pan & 0x40;
    }
    if (c->wave >= c->bank->count) {
        return; /* no such wave: the note is not heard */
    }
    if (!c->retrigger_off) {
        c->pending = &c->bank->waves[c->wave];
        if (c->sounding && c->last_volume != 0) {
            /* The voice is heard: it fades out over this frame and the new wave starts at the next. */
            c->last_volume = 0;
            synth_voice_set_volume(music->synth, index, 0, MUSIC_FRAME_US);
        } else {
            start_pending(music, index);
        }
    }
    c->pitch = (float)note + c->bank->waves[c->wave].tune + (float)(c->ignore_transpose ? 0 : c->transpose);
    if (c->reverb != c->last_reverb) {
        uint8_t mix = (uint8_t)(c->reverb_base + (((128 - c->reverb_base) * c->reverb) >> 7));

        c->last_reverb = c->reverb;
        synth_voice_set_fxmix(music->synth, index, mix > 127 ? 127 : mix);
    }
}

/* ---- What changes each frame ---- */

/* The volume and bend streams: a value below 0x80 lasts a tick; 0x80 and up is a value (its low 7 bits) followed by a
 * length, and lasts the length plus 2 ticks. */
static uint8_t stream_item(MusicChannel *c, const uint8_t **p, uint16_t *hold, int *bad) {
    uint8_t v = next_byte(c, p, bad);

    if (v < 0x80) {
        *hold = 1;
        return v;
    }
    *hold = (uint16_t)(next_length(c, p, bad) + 2);
    return v & 0x7F;
}

static void volume_stream(MusicChannel *c) {
    int bad = 0;

    do {
        c->volume_clock += TICK;
        if (--c->volume_hold == 0) {
            uint8_t v = stream_item(c, &c->volume_at, &c->volume_hold, &bad);

            if (bad) {
                c->volume_at = NULL;
                return;
            }
            c->volume = v;
        }
    } while (c->volume_clock < c->now);
}

static void bend_stream(MusicChannel *c) {
    int bad = 0;

    do {
        c->bend_clock += TICK;
        if (--c->bend_hold == 0) {
            uint8_t v = stream_item(c, &c->bend_at, &c->bend_hold, &bad);

            if (bad) {
                c->bend_at = NULL;
                return;
            }
            c->bend_value = (float)((int)v - 64);
            c->bend = c->bend_value * c->bend_range;
        }
    } while (c->bend_clock < c->now);
}

/* The envelope: the level changes every `speed` frames, by how many steps of the note's time have gone by. */
static void envelope(MusicChannel *c) {
    int32_t steps;

    if (c->now >= c->release_at && c->env_phase < 4) {
        c->env_phase = 4;
        c->env_countdown = 1;
        c->release_level = c->env_level;
    }
    if (--c->env_countdown != 0) {
        return;
    }
    c->env_countdown = c->env_speed;
    switch (c->env_phase) {
    case 1:
        steps = (int32_t)((((c->now - c->note_start) >> 8) * (uint64_t)(int64_t)c->env_factor) >> 10);
        if (steps < c->env_attack) {
            c->env_level = (uint8_t)(c->env_start + (int32_t)(c->attack_step * (float)steps));
        } else {
            c->env_phase = 2;
            c->env_level = c->env_peak;
        }
        break;
    case 2:
        steps = (int32_t)(((((c->now - c->note_start) >> 8) - c->env_attack) * (uint64_t)(int64_t)c->env_factor) >> 10);
        if (steps < c->env_decay) {
            c->env_level = (uint8_t)(c->env_peak + (int32_t)(c->decay_step * (float)steps));
        } else {
            c->env_phase = 3;
            c->env_level = c->env_sustain;
        }
        break;
    case 4:
        steps = (int32_t)((((c->now - c->release_at) >> 8) * (uint64_t)(int64_t)c->env_factor) >> 10);
        if (steps < c->env_release) {
            c->env_level =
                (uint8_t)(c->release_level - (int32_t)(c->release_step * (float)steps * (float)c->release_level));
        } else {
            c->env_phase = 5;
            c->env_level = 0;
        }
        break;
    default:
        break;
    }
}

/* The pan sweep: the pan moves speed / 64 a tick, back and forth between 0 and 127. */
static void sweep(MusicChannel *c) {
    do {
        c->sweep_clock += TICK;
        c->sweep_fraction = (uint8_t)(c->sweep_fraction + c->sweep_speed);
        if (c->sweep_fraction >= 64) {
            int move = c->sweep_fraction >> 6;
            int pan = c->pan;

            c->sweep_fraction &= 63;
            if (!c->sweep_down) {
                pan += move;
                if (pan >= 128) {
                    pan = 127;
                    c->sweep_down = 1;
                }
            } else {
                pan -= move;
                if (pan <= 0) {
                    pan = 0;
                    c->sweep_down = 0;
                }
            }
            c->pan = (uint8_t)pan;
        }
    } while (c->sweep_clock < c->now);
}

/* Vibrato: a sine of the ticks since the note started, after its delay; during the delay the last value stays. */
static float vibrato(MusicChannel *c) {
    int t = (int)c->ticks - c->vib_delay;

    if (t > 0) {
        c->vib_value = sinf((float)t * c->vib_step) * c->vib_depth;
    }
    return c->vib_value;
}

/* Wobble: the pitch jumps by its depth for `on` frames, then back for `off` frames. */
static float wobble(MusicChannel *c) {
    if (--c->wobble_count == 0) {
        if (c->wobble_value == 0) {
            c->wobble_value = c->wobble_depth;
            c->wobble_count = c->wobble_on;
        } else {
            c->wobble_value = 0;
            c->wobble_count = c->wobble_off;
        }
    }
    return (float)c->wobble_value;
}

/* The pitch: the note (sliding from the last when there is portamento), plus this frame's offset and the bend, sent as
 * a ratio when it has changed. Above twice the wave's own rate the note goes silent. */
static void send_pitch(Music *music, int index, float delta) {
    MusicChannel *c = &music->channel[index];
    float pitch = c->pitch;
    float sum;

    if (c->glide != 0) {
        if (c->ticks <= c->glide) {
            pitch = c->glide_from + (c->pitch - c->glide_from) / (float)c->glide * (float)c->ticks;
        }
        c->glide_at = pitch;
    }
    sum = pitch + delta + c->bend;
    if (sum != c->last_pitch) {
        float ratio;

        c->last_pitch = sum;
        ratio = music_power_of_2((float)(sum * (1.0 / 12.0)));
        if (ratio > 2.0f) {
            ratio = 2.0f;
            c->velocity = 0;
        }
        synth_voice_set_pitch(music->synth, index, ratio);
    }
}

/* The volume: the channel's, times the envelope's level, the velocity and the handle's scale, under the songs' or the
 * effects' master volume and any fade; sent when it has changed, moving there over a frame. Then the pan. */
static void send_volume_and_pan(Music *music, int index) {
    MusicChannel *c = &music->channel[index];
    uint32_t v = ((uint32_t)c->volume * c->env_level * c->velocity * (uint32_t)(int32_t)c->volume_scale) >> 13;
    uint32_t p;

    if (v > 0x7FFF) {
        v = 0x7FFF;
    }
    v = v * music->master[c->effect == 0 ? 1 : 0] >> 15;
    if (c->stop_countdown != -1) {
        v = v * (uint32_t)c->stop_countdown / c->stop_length;
    }
    if (v != c->last_volume) {
        c->last_volume = (uint16_t)v;
        synth_voice_set_volume(music->synth, index, (int16_t)v, MUSIC_FRAME_US);
    }
    p = ((uint32_t)c->pan * (uint32_t)(int32_t)c->pan_scale >> 7) & 0x7F;
    if (p != c->last_pan) {
        c->last_pan = (uint8_t)p;
        synth_voice_set_pan(music->synth, index, music->mono ? 0x40 : (int)p);
    }
}

int32_t music_frame(void *context) {
    Music *music = context;
    int i;

    while (music->queue_read != music->queue_write) {
        uint8_t command = music->queue[music->queue_read].command;
        uint32_t handle = music->queue[music->queue_read].handle;

        music->queue_read = (music->queue_read + 1) % MUSIC_QUEUE;
        for (i = 0; i < MUSIC_CHANNELS; i++) {
            if (music->channel[i].handle == handle) {
                music->channel[i].paused = command == 0;
            }
        }
    }
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        MusicChannel *c = &music->channel[i];

        if (c->data == NULL || c->paused) {
            continue;
        }
        if (c->pending != NULL) {
            start_pending(music, i);
        }
        c->now += c->tempo;
        if (c->length != ENDLESS) {
            while (c->now > c->next_note && c->data != NULL) {
                read_note(music, i);
            }
            if (c->data == NULL) {
                continue;
            }
        }
        if (c->volume_at != NULL && c->now > c->volume_clock) {
            volume_stream(c);
        }
        if (c->bend_at != NULL && c->now > c->bend_clock) {
            bend_stream(c);
        }
        if (c->stop_countdown != -1 && --c->stop_countdown == -1) {
            channel_free(music, i);
        } else if (c->sounding) {
            float delta;

            if (c->env_phase != 0) {
                envelope(c);
            }
            if (c->sweep_speed != 0 && c->now > c->sweep_clock) {
                sweep(c);
            }
            delta = c->offset;
            if (c->vib_speed != 0) {
                delta += vibrato(c);
            }
            if (c->wobble_on != 0) {
                delta += wobble(c);
            }
            if (c->pending == NULL) {
                send_pitch(music, i, delta);
                send_volume_and_pan(music, i);
            }
        }
        c->ticks = (uint16_t)((c->now - c->note_start) >> 8);
    }
    music->mono = 0;
    return MUSIC_FRAME_US;
}

int music_init(Music *music, Synth *synth, const SoundBank *songs, const SoundBank *effects, const uint8_t *rom,
               size_t rom_size) {
    const uint32_t data = EFFECTS_DATA - MAIN_RAM_TO_ROM;
    const uint32_t table = EFFECT_TABLE - MAIN_RAM_TO_ROM;
    const uint32_t priorities = EFFECT_PRIORITIES - MAIN_RAM_TO_ROM;
    int i;

    memset(music, 0, sizeof(*music));
    if (rom_size < priorities + 4 * MUSIC_EFFECTS) {
        fprintf(stderr, "music: the ROM is too small for the effect table\n");
        return -1;
    }
    music->synth = synth;
    music->songs_bank = songs;
    music->effects_bank = effects;
    music->effects_end = rom + table;
    for (i = 0; i < MUSIC_EFFECTS; i++) {
        uint32_t at = be32(rom + table + 4 * i);

        if (at < EFFECTS_DATA || at >= EFFECT_TABLE) {
            fprintf(stderr, "music: effect %d is not in the effects' data\n", i);
            return -1;
        }
        music->effect[i] = rom + data + (at - EFFECTS_DATA);
        music->effect_priority[i] = (int32_t)be32(rom + priorities + 4 * i);
    }
    music->next_handle = 1;
    music->random_seed = 0x12345678u;
    music->master[0] = 0x7FFF;
    music->master[1] = 0x7FFF;
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        channel_reset(music, &music->channel[i], songs);
    }
    synth_set_player(synth, music_frame, music);
    return 0;
}
