/* The controllers of the developer runs (src/dev/dev_pads.h): pad files to record and replay, the pad script and the
 * window's controllers, for the developer match (dev_match.c) and the developer boot (dev_boot.c).
 *
 * A pad file is "TDPADS02", then one 10-byte entry per pad read, in the order the game reads, little-endian: u32
 * frame, u8 channel, u8 0, u16 buttons, s8 stick x, s8 stick y. Replaying the reads in order gives the game the same
 * pads at the same moments, whatever the timing of the run; the frame lets a tool give the original game the same
 * pads at the same frames (Project64's td_lockstep.js). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev_pads.h"
#include "game.h"

static const char *g_who = "dev-pads";
static GamePad g_window_pads[4];
static FILE *g_record;
static FILE *g_replay;

/* The pad script: each line's first frame, frame count and buttons, and controller 1's pad for the current frame. */
typedef struct {
    uint32_t frame, frames;
    uint16_t buttons;
} ScriptPress;
static ScriptPress *g_script;
static size_t g_script_count;
static int g_scripted;
static GamePad g_script_pad;

static void script_load(const char *path) {
    static const struct {
        const char *name;
        uint16_t bits;
    } names[] = { { "A", 0x8000 },     { "B", 0x4000 },     { "Z", 0x2000 },     { "START", 0x1000 },
                  { "DUP", 0x0800 },   { "DDOWN", 0x0400 }, { "DLEFT", 0x0200 }, { "DRIGHT", 0x0100 },
                  { "L", 0x0020 },     { "R", 0x0010 },     { "CUP", 0x0008 },   { "CDOWN", 0x0004 },
                  { "CLEFT", 0x0002 }, { "CRIGHT", 0x0001 } };
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0;

    if (f == NULL) {
        port_fatal("%s: cannot read the pad script %s", g_who, path);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char buttons[128];
        unsigned frame, frames = 1;
        char *hash = strchr(line, '#');
        int got;
        ScriptPress *grown;

        n++;
        if (hash != NULL) {
            *hash = '\0';
        }
        got = sscanf(line, "%u %127s %u", &frame, buttons, &frames);
        if (got < 0) {
            continue;
        }
        if (got < 2) {
            port_fatal("%s: %s, line %d: a line is FRAME BUTTONS [FRAMES]", g_who, path, n);
        }
        grown = realloc(g_script, (g_script_count + 1) * sizeof(*g_script));
        if (grown == NULL) {
            port_fatal("%s: no memory for the pad script", g_who);
        }
        g_script = grown;
        g_script[g_script_count].frame = frame;
        g_script[g_script_count].frames = frames;
        g_script[g_script_count].buttons = 0;
        for (char *name = strtok(buttons, "+"); name != NULL; name = strtok(NULL, "+")) {
            size_t k;

            for (k = 0; k < sizeof(names) / sizeof(names[0]) && strcmp(names[k].name, name) != 0; k++) {
            }
            if (k == sizeof(names) / sizeof(names[0])) {
                port_fatal("%s: %s, line %d: no button is called %s", g_who, path, n, name);
            }
            g_script[g_script_count].buttons |= names[k].bits;
        }
        g_script_count++;
    }
    fclose(f);
}

void dev_pads_open(const char *who, const char *replay, const char *record, const char *script) {
    g_who = who;
    if (replay != NULL) {
        char magic[8];

        g_replay = fopen(replay, "rb");
        if (g_replay == NULL || fread(magic, 8, 1, g_replay) != 1 || memcmp(magic, "TDPADS02", 8) != 0) {
            port_fatal("%s: %s is not a pad file (--record-pads writes one)", g_who, replay);
        }
    }
    if (record != NULL) {
        g_record = fopen(record, "wb");
        if (g_record == NULL) {
            port_fatal("%s: cannot write the pad file %s", g_who, record);
        }
        fwrite("TDPADS02", 8, 1, g_record);
    }
    if (script != NULL) {
        script_load(script);
        g_scripted = 1;
    }
}

void dev_pads_close(void) {
    if (g_record != NULL) {
        fclose(g_record);
        g_record = NULL;
    }
    if (g_replay != NULL) {
        fclose(g_replay);
        g_replay = NULL;
    }
    free(g_script);
    g_script = NULL;
    g_script_count = 0;
    g_scripted = 0;
}

int dev_pads_replaying(void) {
    return g_replay != NULL;
}

int dev_pads_scripted(void) {
    return g_scripted;
}

int dev_pads_replay(int channel, GamePad *pad) {
    uint8_t e[10];

    memset(pad, 0, sizeof(*pad));
    if (g_replay == NULL || fread(e, 10, 1, g_replay) != 1) {
        return 0;
    }
    if (e[4] != channel) {
        port_fatal("%s: the pad file has a read of controller %u where the game reads controller %d: it was recorded "
                   "with other options",
                   g_who, e[4], channel);
    }
    pad->buttons = (uint16_t)(e[6] | e[7] << 8);
    pad->stick_x = (int8_t)e[8];
    pad->stick_y = (int8_t)e[9];
    return 1;
}

long dev_pads_tell(void) {
    return g_replay != NULL ? ftell(g_replay) : 0;
}

void dev_pads_seek(long at) {
    if (g_replay != NULL) {
        fseek(g_replay, at, SEEK_SET);
    }
}

void dev_pads_script_frame(uint32_t frame) {
    size_t i;

    g_script_pad.buttons = 0;
    for (i = 0; i < g_script_count; i++) {
        if (frame >= g_script[i].frame && frame < g_script[i].frame + g_script[i].frames) {
            g_script_pad.buttons |= g_script[i].buttons;
        }
    }
}

void dev_pads_script(GamePad *pad) {
    *pad = g_script_pad;
}

void dev_pads_window(int channel, GamePad *pad) {
    *pad = g_window_pads[channel & 3];
}

void dev_pads_set_window(int channel, uint16_t buttons, int8_t stick_x, int8_t stick_y) {
    if (channel >= 0 && channel < 4) {
        g_window_pads[channel].buttons = buttons;
        g_window_pads[channel].stick_x = stick_x;
        g_window_pads[channel].stick_y = stick_y;
    }
}

void dev_pads_record(uint32_t frame, int channel, const GamePad *pad) {
    uint8_t e[10];

    if (g_record == NULL) {
        return;
    }
    e[0] = (uint8_t)(frame & 0xFF);
    e[1] = (uint8_t)(frame >> 8 & 0xFF);
    e[2] = (uint8_t)(frame >> 16 & 0xFF);
    e[3] = (uint8_t)(frame >> 24);
    e[4] = (uint8_t)channel;
    e[5] = 0;
    e[6] = (uint8_t)(pad->buttons & 0xFF);
    e[7] = (uint8_t)(pad->buttons >> 8);
    e[8] = (uint8_t)pad->stick_x;
    e[9] = (uint8_t)pad->stick_y;
    fwrite(e, 10, 1, g_record);
}
