/* --sound-replay and --sound-lockstep (src/audio/sound_replay.h). The calls file is written by the comparison script
 * run in Project64 (the decompilation's notes/gameplay_route/work/reference/out/pj64/Scripts/td_sound.js).
 *
 * The replay reads its "a" lines, the game's calls, and makes the same calls on the port's player at the same player
 * frames, and writes the player's calls to the synthesizer in the same form as the script's "s" lines. The player's
 * frames are counted from 1: a game call logged at frame F came after F frames, so it is made just before frame F + 1.
 *
 * The lockstep runs the port's own game instead: the developer match, the same as the recording's. On the N64 the
 * music player runs on its own thread, once a retrace, whatever the game is doing; on PC it runs when the sound is
 * made. So that the port's game meets its player at the same moments as the original's did, the sound is made on
 * demand: before each of the game's calls, up to the player frame at which the original made the same call, and at
 * each frame hand-off, up to the frame of the original's hand-off. Each question then gets the answer the original
 * got, unless the port's player differs, which is what the comparison looks for. */
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
    int answered; /* a question the recording has the answer to (an "r" line) */
    int32_t answer;
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
    uint32_t last; /* the last player frame the recording reached */
    FILE *trace;
    /* The lockstep's: the match's start, its hand-offs' player frames, the calls the player makes itself (those before
     * the match) and the next of the recording's calls the port's game should make. */
    int lockstep;
    uint32_t match_frame;
    uint32_t *hand_offs;
    int nhand_offs, hand_off;
    int queued;
    int expect;
    int differ; /* the port's game's calls no longer follow the recording's */
    int questions, answers_same;
    int after; /* calls made after the recording's last hand-off */
    FILE *audio;
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
    } else if (strcmp(c->name, "MusAsk") == 0 || strcmp(c->name, "MusHandleAsk") == 0) {
        /* A question: nothing to make. */
    } else {
        fprintf(stderr, "sound-replay: %s is not replayed\n", c->name);
    }
}

/* The player as the synthesizer calls it: first the game's calls that came before this frame (in the lockstep, only those
 * before the match: the match's are the port's game's own). */
static int32_t replay_frame(void *context) {
    Replay *r = context;

    while (r->next < r->queued && r->calls[r->next].frame <= r->frame) {
        make_call(r, &r->calls[r->next++]);
    }
    r->frame++;
    return music_frame(sound_music());
}

static void put16(FILE *f, unsigned v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

/* Reads DIR/calls.txt into r: the game's calls ("a", with the answers "r"), the match's start ("m"), the hand-offs
 * ("h") and the audio frames ("b"). Returns 0, or 1 with a message. */
static int read_recording(Replay *r, const char *dir) {
    char path[1024], line[256];
    FILE *calls;
    int match_calls = -1;

    memset(r, 0, sizeof(*r));
    r->dir = dir;
    r->calls = calloc(MAX_CALLS, sizeof(Call));
    r->hand_offs = calloc(MAX_CALLS, sizeof(uint32_t));
    snprintf(path, sizeof(path), "%s/calls.txt", dir);
    calls = fopen(path, "r");
    if (calls == NULL || r->calls == NULL || r->hand_offs == NULL) {
        fprintf(stderr, "sound-replay: cannot read %s\n", path);
        if (calls != NULL) {
            fclose(calls);
        }
        return 1;
    }
    while (fgets(line, sizeof(line), calls) != NULL) {
        unsigned frame, bytes;
        long value;

        if (line[0] == 'a' && r->ncalls < MAX_CALLS) {
            Call *c = &r->calls[r->ncalls];
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
                r->ncalls++;
            }
        } else if (line[0] == 'r' && sscanf(line + 2, "%u %ld", &frame, &value) == 2 && r->ncalls > 0) {
            r->calls[r->ncalls - 1].answered = 1;
            r->calls[r->ncalls - 1].answer = (int32_t)value;
        } else if (line[0] == 'm' && sscanf(line + 2, "%u", &frame) == 1) {
            r->match_frame = frame;
            match_calls = r->ncalls;
        } else if (line[0] == 'h' && sscanf(line + 2, "%u", &frame) == 1 && r->nhand_offs < MAX_CALLS) {
            r->hand_offs[r->nhand_offs++] = frame;
            r->last = frame > r->last ? frame : r->last;
        } else if (line[0] == 's' && sscanf(line + 2, "%u", &frame) == 1 && frame > r->last) {
            r->last = frame;
        } else if (line[0] == 'b' && sscanf(line + 2, "%u %u", &frame, &bytes) == 2) {
            if (r->nframes < MAX_CALLS) {
                r->frames[r->nframes++] = (uint16_t)(bytes / 4);
            }
            r->last = frame > r->last ? frame : r->last;
        }
    }
    fclose(calls);
    r->queued = match_calls >= 0 ? match_calls : r->ncalls;
    r->expect = r->queued;
    return 0;
}

static void free_recording(Replay *r) {
    free(r->calls);
    free(r->hand_offs);
    r->calls = NULL;
    r->hand_offs = NULL;
}

/* Starts the sound, opens DIR/port_calls.txt and DIR/port_audio.raw and hands the synthesizer the recording's audio
 * frames and the replay's player. Returns 0, or 1 with a message. */
static int start_output(Replay *r, const uint8_t *rom, size_t rom_size) {
    char path[1024];

    if (sound_start(rom, rom_size) != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/port_calls.txt", r->dir);
    r->trace = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/port_audio.raw", r->dir);
    r->audio = fopen(path, "wb");
    if (r->trace == NULL || r->audio == NULL) {
        fprintf(stderr, "sound-replay: cannot write to %s\n", r->dir);
        if (r->trace != NULL) {
            fclose(r->trace);
        }
        if (r->audio != NULL) {
            fclose(r->audio);
        }
        sound_end();
        return 1;
    }
    /* The N64's audio frames as recorded, so that changes meet the frame starts where they did there. */
    sound_synth()->frame_lengths = r->frames;
    sound_synth()->frame_count = r->nframes;
    sound_synth()->trace = trace;
    sound_synth()->trace_context = r;
    synth_set_player(sound_synth(), replay_frame, r);
    return 0;
}

/* One sub-frame of sound, to port_audio.raw. */
static void make_subframe(Replay *r) {
    int16_t block[SYNTH_SUBFRAME * 2];
    int i;

    synth_render(sound_synth(), block, SYNTH_SUBFRAME);
    for (i = 0; i < SYNTH_SUBFRAME * 2; i++) {
        put16(r->audio, (uint16_t)block[i]);
    }
}

static void stop_output(Replay *r) {
    fclose(r->audio);
    fclose(r->trace);
    sound_end();
}

int sound_replay(const uint8_t *rom, size_t rom_size, const char *dir) {
    static Replay r;

    if (read_recording(&r, dir) != 0) {
        free_recording(&r);
        return 1;
    }
    r.queued = r.ncalls; /* every call, the match's too */
    if (start_output(&r, rom, rom_size) != 0) {
        free_recording(&r);
        return 1;
    }
    /* Up to the last player frame the recording reached. */
    while (r.frame <= r.last) {
        make_subframe(&r);
    }
    stop_output(&r);
    printf("sound-replay: %d game calls over %u player frames; wrote port_calls.txt and port_audio.raw\n", r.ncalls,
           (unsigned)r.last);
    free_recording(&r);
    return 0;
}

/* ---- The lockstep ---- */

static Replay g_lockstep;

/* The sound up to the moment just after the player's frame `target`: the next sub-frame would begin with frame
 * target + 1. */
static void advance_to(Replay *r, uint32_t target) {
    while (r->frame < target || !synth_player_next(sound_synth())) {
        make_subframe(r);
    }
}

static void write_call(Replay *r, const char *name, int count, const int32_t *args) {
    int i;

    fprintf(r->trace, "a %u %s", (unsigned)r->frame, name);
    for (i = 0; i < count; i++) {
        fprintf(r->trace, " %d", (int)args[i]);
    }
    fputc('\n', r->trace);
}

static int same_call(const Call *c, const char *name, int count, const int32_t *args, const void *song) {
    int i;

    if (strcmp(c->name, name) != 0 || (song == NULL && c->count != count)) {
        return 0;
    }
    for (i = 0; song == NULL && i < count; i++) {
        if (c->args[i] != args[i]) {
            return 0;
        }
    }
    return 1;
}

static void lockstep_call(void *context, const char *name, int count, const int32_t *args, const void *song) {
    Replay *r = context;
    const Call *c = r->expect < r->ncalls ? &r->calls[r->expect] : NULL;
    int same = !r->differ && c != NULL && same_call(c, name, count, args, song);
    int i;

    /* After the recording's last hand-off: written down ("x"), but there is nothing to compare it with. */
    if (r->hand_off >= r->nhand_offs) {
        r->after++;
        fprintf(r->trace, "x %u %s\n", (unsigned)r->frame, name);
        return;
    }
    /* The developer match sets up what the boot set up on the N64 (the master volumes) before its first frame: such a
     * call, the same as one of the boot's in the recording, stands in for it ("p" in port_calls.txt). */
    if (!same && r->hand_off == 0 && song == NULL) {
        for (i = 0; i < r->queued; i++) {
            if (same_call(&r->calls[i], name, count, args, NULL)) {
                fprintf(r->trace, "p %u %s\n", (unsigned)r->frame, name);
                return;
            }
        }
    }
    if (same) {
        advance_to(r, c->frame);
        r->expect++;
    } else if (!r->differ) {
        r->differ = 1;
        fprintf(stderr, "sound-lockstep: from player frame %u the game's calls differ: the port's %s, the original's %s\n",
                (unsigned)r->frame, name, c != NULL ? c->name : "(none)");
    }
    if (song != NULL) {
        char path[1024];
        FILE *f;

        /* The song by its frame, as the recording keeps it; the recording's line has the song's number. */
        snprintf(path, sizeof(path), "%s/port_song_%u.bin", r->dir, (unsigned)r->frame);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(song, 1, SONG_BUFFER, f);
            fclose(f);
        }
        fprintf(r->trace, "a %u %s %d\n", (unsigned)r->frame, name, same ? (int)c->args[0] : -1);
    } else {
        write_call(r, name, count, args);
    }
}

static void lockstep_answer(void *context, int32_t value) {
    Replay *r = context;
    const Call *c = r->expect > 0 ? &r->calls[r->expect - 1] : NULL;

    fprintf(r->trace, "%c %u %d\n", r->hand_off >= r->nhand_offs ? 'x' : 'r', (unsigned)r->frame, (int)value);
    if (!r->differ && c != NULL && c->answered) {
        r->questions++;
        r->answers_same += c->answer == value;
    }
}

static void lockstep_hand_off(void *context) {
    Replay *r = context;

    if (r->hand_off < r->nhand_offs) {
        advance_to(r, r->hand_offs[r->hand_off]);
    }
    r->hand_off++;
    fprintf(r->trace, "h %u\n", (unsigned)r->frame);
}

static const SoundWatch g_lockstep_watch = { lockstep_call, lockstep_answer, lockstep_hand_off, &g_lockstep };

int sound_lockstep_start(const uint8_t *rom, size_t rom_size, const char *dir) {
    Replay *r = &g_lockstep;

    if (read_recording(r, dir) != 0) {
        free_recording(r);
        return 1;
    }
    if (r->nhand_offs == 0) {
        fprintf(stderr, "sound-lockstep: %s/calls.txt is not a match's recording (no hand-offs)\n", dir);
        free_recording(r);
        return 1;
    }
    if (start_output(r, rom, rom_size) != 0) {
        free_recording(r);
        return 1;
    }
    r->lockstep = 1;
    /* Up to the match, with the original's calls before it: the boot's, which the developer match leaves out. */
    advance_to(r, r->match_frame);
    fprintf(r->trace, "m %u\n", (unsigned)r->frame);
    sound_watch(&g_lockstep_watch);
    return 0;
}

void sound_lockstep_end(void) {
    Replay *r = &g_lockstep;

    if (!r->lockstep) {
        return;
    }
    sound_watch(NULL);
    printf("sound-lockstep: the port's game made %d of the original's %d calls in the match in the same order%s "
           "(and %d after the recording's end); %d of %d questions answered the same; %d of %d hand-offs; wrote "
           "port_calls.txt and port_audio.raw\n",
           r->expect - r->queued, r->ncalls - r->queued, r->differ ? ", then differed" : "", r->after,
           r->answers_same, r->questions, r->hand_off < r->nhand_offs ? r->hand_off : r->nhand_offs, r->nhand_offs);
    stop_output(r);
    free_recording(r);
    r->lockstep = 0;
}
