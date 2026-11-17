/* --sound-replay (src/audio/sound_replay.h). The calls file is written by the comparison script run in Project64 (the
 * decompilation's notes/gameplay_route/work/reference/out/pj64/Scripts/td_sound.js); this reads its "a" lines, the
 * game's calls, and makes the same calls on the port's player at the same player frames, and writes the player's
 * calls to the synthesizer in the same form as the script's "s" lines. The player's frames are counted from 1: a game
 * call logged at frame F came after F frames, so it is made just before frame F + 1. */
#include "sound_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound.h"

#define MAX_CALLS 65536
#define SONG_BUFFER 0x8000

typedef struct {
    uint32_t frame;
    char name[32];
    int32_t args[6];
    int count;
} Call;

typedef struct {
    const char *dir;
    Call *calls;
    int ncalls;
    int next;
    uint32_t frame;
    uint8_t song[SONG_BUFFER]; /* the game's one song buffer, D_800487F0 */
    uint16_t frames[MAX_CALLS]; /* the recording's audio frame lengths */
    size_t nframes;
    FILE *trace;
} Replay;

static void wave_name(const SoundWave *wave, char *text, size_t size) {
    int b;

    for (b = 0; b < 2; b++) {
        const SoundBank *bank = sound_bank(b);

        if (wave >= bank->waves && wave < bank->waves + bank->count) {
            snprintf(text, size, "%d %d", b, (int)(wave - bank->waves));
            return;
        }
    }
    snprintf(text, size, "? ?");
}

static void trace(void *context, int voice, SynthTraceOp op, const SoundWave *wave, int32_t value, int32_t ramp_us,
                  float pitch) {
    Replay *r = context;
    char text[32];
    uint32_t bits;

    switch (op) {
    case SYNTH_TRACE_START:
        wave_name(wave, text, sizeof(text));
        fprintf(r->trace, "s %u %d start %s\n", (unsigned)r->frame, voice, text);
        break;
    case SYNTH_TRACE_STOP:
        fprintf(r->trace, "s %u %d stop\n", (unsigned)r->frame, voice);
        break;
    case SYNTH_TRACE_VOLUME:
        fprintf(r->trace, "s %u %d vol %d %d\n", (unsigned)r->frame, voice, (int)value, (int)ramp_us);
        break;
    case SYNTH_TRACE_PITCH:
        memcpy(&bits, &pitch, sizeof(bits));
        fprintf(r->trace, "s %u %d pitch %08X\n", (unsigned)r->frame, voice, (unsigned)bits);
        break;
    case SYNTH_TRACE_PAN:
        fprintf(r->trace, "s %u %d pan %d\n", (unsigned)r->frame, voice, (int)value);
        break;
    case SYNTH_TRACE_FXMIX:
        fprintf(r->trace, "s %u %d fx %d\n", (unsigned)r->frame, voice, (int)value);
        break;
    }
}

static float from_bits(int32_t bits) {
    float f;

    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void make_call(Replay *r, const Call *c) {
    Music *m = sound_music();
    const int32_t *a = c->args;

    if (strcmp(c->name, "MusStartSong") == 0) {
        char path[1024];
        FILE *f;

        snprintf(path, sizeof(path), "%s/song_%u.bin", r->dir, (unsigned)c->frame);
        f = fopen(path, "rb");
        if (f == NULL || fread(r->song, 1, SONG_BUFFER, f) != SONG_BUFFER) {
            fprintf(stderr, "sound-replay: cannot read %s\n", path);
        } else {
            music_start_song(m, r->song, SONG_BUFFER);
        }
        if (f != NULL) {
            fclose(f);
        }
    } else if (strcmp(c->name, "MusBankStartEffect") == 0 || strcmp(c->name, "MusStartEffect") == 0) {
        music_start_effect(m, a[0], 128, 128, 0, -1);
    } else if (strcmp(c->name, "MusBankStartEffect2") == 0 || strcmp(c->name, "MusStartEffect2") == 0) {
        music_start_effect(m, a[0], a[1], a[2], a[3], a[4]);
    } else if (strcmp(c->name, "MusStop") == 0) {
        music_stop(m, a[0], a[1]);
    } else if (strcmp(c->name, "MusHandleStop") == 0) {
        music_handle_stop(m, (uint32_t)a[0], a[1]);
    } else if (strcmp(c->name, "MusSetMasterVolume") == 0) {
        music_set_master_volume(m, a[0], a[1]);
    } else if (strcmp(c->name, "MusHandleSetVolume") == 0) {
        music_handle_set_volume(m, (uint32_t)a[0], a[1]);
    } else if (strcmp(c->name, "MusHandleSetPan") == 0) {
        music_handle_set_pan(m, (uint32_t)a[0], a[1]);
    } else if (strcmp(c->name, "MusHandleSetFreqOffset") == 0) {
        music_handle_set_freq_offset(m, (uint32_t)a[0], from_bits(a[1]));
    } else if (strcmp(c->name, "MusHandleSetTempo") == 0) {
        music_handle_set_tempo(m, (uint32_t)a[0], a[1]);
    } else if (strcmp(c->name, "MusHandleSetReverb") == 0) {
        music_handle_set_reverb(m, (uint32_t)a[0], a[1]);
    } else if (strcmp(c->name, "MusHandlePause") == 0) {
        music_handle_pause(m, (uint32_t)a[0]);
    } else if (strcmp(c->name, "MusHandleUnPause") == 0) {
        music_handle_unpause(m, (uint32_t)a[0]);
    } else if (strcmp(c->name, "mono") == 0) {
        music_set_mono(m, a[0]);
    } else {
        fprintf(stderr, "sound-replay: %s is not replayed\n", c->name);
    }
}

/* The player as the synthesizer calls it: first the game's calls that came before this frame. */
static int32_t replay_frame(void *context) {
    Replay *r = context;

    while (r->next < r->ncalls && r->calls[r->next].frame <= r->frame) {
        make_call(r, &r->calls[r->next++]);
    }
    r->frame++;
    return music_frame(sound_music());
}

static void put16(FILE *f, unsigned v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

int sound_replay(const uint8_t *rom, size_t rom_size, const char *dir) {
    static Replay r;
    char path[1024], line[256];
    FILE *calls, *audio;
    uint32_t last = 0;
    uint64_t audio_bytes = 0;
    int16_t block[SYNTH_SUBFRAME * 2];

    memset(&r, 0, sizeof(r));
    r.dir = dir;
    r.calls = calloc(MAX_CALLS, sizeof(Call));
    snprintf(path, sizeof(path), "%s/calls.txt", dir);
    calls = fopen(path, "r");
    if (calls == NULL || r.calls == NULL) {
        fprintf(stderr, "sound-replay: cannot read %s\n", path);
        free(r.calls);
        return 1;
    }
    while (fgets(line, sizeof(line), calls) != NULL) {
        unsigned frame, bytes;

        if (line[0] == 'a' && r.ncalls < MAX_CALLS) {
            Call *c = &r.calls[r.ncalls];
            char *p = line + 2, *end;

            c->frame = (uint32_t)strtoul(p, &end, 10);
            p = end + 1;
            if (sscanf(p, "%31s", c->name) == 1) {
                p += strlen(c->name);
                while (c->count < 6) {
                    long v = strtol(p, &end, 10);

                    if (end == p) {
                        break;
                    }
                    c->args[c->count++] = (int32_t)v;
                    p = end;
                }
                r.ncalls++;
            }
        } else if (line[0] == 's' && sscanf(line + 2, "%u", &frame) == 1 && frame > last) {
            last = frame;
        } else if (line[0] == 'b' && sscanf(line + 2, "%u %u", &frame, &bytes) == 2) {
            audio_bytes += bytes;
            if (r.nframes < MAX_CALLS) {
                r.frames[r.nframes++] = (uint16_t)(bytes / 4);
            }
            last = frame > last ? frame : last;
        }
    }
    fclose(calls);
    if (sound_start(rom, rom_size) != 0) {
        free(r.calls);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/port_calls.txt", dir);
    r.trace = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/port_audio.raw", dir);
    audio = fopen(path, "wb");
    if (r.trace == NULL || audio == NULL) {
        fprintf(stderr, "sound-replay: cannot write to %s\n", dir);
        sound_end();
        free(r.calls);
        return 1;
    }
    /* The N64's audio frames as recorded, so that changes meet the frame starts where they did there. */
    sound_synth()->frame_lengths = r.frames;
    sound_synth()->frame_count = r.nframes;
    sound_synth()->trace = trace;
    sound_synth()->trace_context = &r;
    synth_set_player(sound_synth(), replay_frame, &r);
    /* Up to the last player frame the recording reached. */
    while (r.frame <= last) {
        int i;

        synth_render(sound_synth(), block, SYNTH_SUBFRAME);
        for (i = 0; i < SYNTH_SUBFRAME * 2; i++) {
            put16(audio, (uint16_t)block[i]);
        }
    }
    fclose(audio);
    fclose(r.trace);
    sound_end();
    free(r.calls);
    printf("sound-replay: %d game calls over %u player frames (the recording has %llu bytes of sound); wrote "
           "port_calls.txt and port_audio.raw\n",
           r.ncalls, (unsigned)last, (unsigned long long)audio_bytes);
    return 0;
}
