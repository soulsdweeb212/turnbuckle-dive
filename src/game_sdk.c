/* The port's versions of the N64 SDK functions that the game's files call: libultra's message queues, controllers,
 * Rumble Pak and Controller Pak, cartridge reads and the save memory, video, threads, interrupts and caches, the
 * scheduler's client list, and the music library libmus. The decompilation's include/port.h says why several are renamed port_...: their N64
 * records are smaller than LibUltraShip's.
 *
 * How the game runs on them: one thread, the game's own. On the N64 the game also has a render thread (func_80002CC4),
 * an audio thread and the scheduler, which wait on message queues; here none of them runs. The game's frame hand-offs
 * go to port_present instead (the decompilation's port.h), and the "hardware" these functions drive finishes at once:
 * a cartridge read copies the bytes from the ROM and posts its message before it returns, and so does a controller
 * read. So a game function that waits for such a message always finds it in its queue. A wait on an empty queue that
 * nothing will ever fill stops the program with a message, instead of hanging.
 *
 * Sound: the libmus functions go to the port's music player (src/audio/music.c). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/sound.h"
#include "game.h"
#include "game_sdk.h"
#include "game_vars.h"

static GameSdkHooks g_hooks;
static unsigned g_plugged = 1;
static unsigned g_rumble_paks;            /* bit n: controller n + 1 has a Rumble Pak (only plugged-in controllers) */
static unsigned g_rumbling;               /* bit n: controller n + 1's motor is on */
static void (*g_rumble)(int channel, int on);
static uint32_t g_frames;

static void motor(int channel, int on) {
    if (g_rumble != NULL) {
        g_rumble(channel, on);
    }
    if (on) {
        g_rumbling |= 1u << channel;
    } else {
        g_rumbling &= ~(1u << channel);
    }
}

void game_sdk_set_rumble_paks(unsigned paks) {
    unsigned gone;
    int i;

    paks &= g_plugged;
    gone = g_rumbling & ~paks;
    for (i = 0; i < 4; i++) {
        if ((gone >> i) & 1) {
            motor(i, 0); /* a pak taken out stops: the game cannot stop it any more */
        }
    }
    g_rumble_paks = paks;
}

void game_sdk_set_plugged(unsigned plugged) {
    g_plugged = plugged & 0xFu;
    game_sdk_set_rumble_paks(g_rumble_paks);
}

void game_sdk_set_rumble(void (*rumble)(int channel, int on)) {
    g_rumble = rumble;
}

void game_sdk_set(const GameSdkHooks *hooks, unsigned plugged) {
    if (hooks != NULL) {
        g_hooks = *hooks;
    } else {
        memset(&g_hooks, 0, sizeof(g_hooks));
    }
    game_sdk_set_plugged(plugged);
}

uint32_t game_sdk_frames(void) {
    return g_frames;
}

/* ---- The frame hand-off ----
 * On the N64 an idle thread (func_800004B8) calls guRandom in a loop whenever the CPU has nothing else to do, so the
 * random seed xseed moves on by however much idle time each frame leaves, which differs from run to run. On PC no
 * thread idles; instead each hand-off that waits for the screen (a frame, or a wait for the retrace) draws a fixed
 * number of random numbers, g_idle_randoms (1, or 0 with game_sdk_set_idle_randoms), so the same match gives the
 * same numbers on every run. */
static int g_idle_randoms = 1;

void game_sdk_set_idle_randoms(int count) {
    g_idle_randoms = count;
}

uint32_t guRandom(void); /* the game's own (src/main/guRandom.c) */

/* ---- The port's settings ----
 * The game's code reads these through port.h. A championship run has 9 matches in the original game; the window's
 * Settings menu can make it shorter (game_sdk_set_championship_matches). */
static int g_championship_matches = 9;

void game_sdk_set_championship_matches(int matches) {
    g_championship_matches = matches < 1 ? 1 : matches > 9 ? 9 : matches;
}

int32_t port_championship_matches(void) {
    return g_championship_matches;
}

/* The players' own costume colours (game_sdk_set_costume_colour), by character id: bank (5 bits) and entry (5 bits),
 * as func_8000BB40 takes the id apart; 1024 wrestlers, two colours each, 0 when the game's own colour applies and
 * otherwise the colour with bit 24 set. */
#define COSTUME_SET 0x1000000u
static uint32_t g_costume[32 * 32][2];

static uint32_t *costume_entry(unsigned character, int colour) {
    if (colour != 1 && colour != 2) {
        return NULL;
    }
    return &g_costume[((character >> 8) & 31) * 32 + (character & 31)][colour - 1];
}

void game_sdk_set_costume_colour(unsigned character, int colour, int32_t rgb) {
    uint32_t *entry = costume_entry(character, colour);

    if (entry != NULL) {
        *entry = rgb < 0 ? 0 : ((uint32_t)rgb & 0xFFFFFF) | COSTUME_SET;
    }
}

int32_t game_sdk_costume_colour(unsigned character, int colour) {
    const uint32_t *entry = costume_entry(character, colour);

    return entry != NULL && (*entry & COSTUME_SET) ? (int32_t)(*entry & 0xFFFFFF) : -1;
}

static void (*g_anim_hook)(int slot, int anim, int previous, int mode);

void game_sdk_set_anim_hook(void (*hook)(int slot, int anim, int previous, int mode)) {
    g_anim_hook = hook;
}

/* The game's func_800D427C calls it when it changes a wrestler's animation (port.h); it only passes the news on. */
void port_anim_started(int32_t slot, int16_t anim, int16_t previous, int32_t mode) {
    if (g_anim_hook != NULL) {
        g_anim_hook((int)slot, anim, previous, (int)mode);
    }
}

/* A colour's 16 shades, lightest first, shaped as the game's own (its colour table, resource 0xA51: rows of 16
 * shades): shade 7 is the colour itself; the lighter shades go toward white, 7% of the way a step (49% at shade 0),
 * and the darker ones toward black, by 1/16 a step (half the colour at shade 15). These are averages of the game's 13
 * coloured rows. Each shade is RGBA5551, its two bytes in the N64's order, as the game's palettes hold it. */
int32_t port_costume_ramp(uint16_t character, int32_t channel, uint16_t *ramp) {
    int32_t rgb = game_sdk_costume_colour(character, channel);
    int k;
    int c;

    if (rgb < 0) {
        return 0;
    }
    for (k = 0; k < 16; k++) {
        unsigned bits[3];
        unsigned value;
        uint8_t *out = (uint8_t *)&ramp[k];

        for (c = 0; c < 3; c++) {
            unsigned v = ((unsigned)rgb >> (16 - 8 * c)) & 0xFF;

            if (k < 7) {
                v += (255 - v) * (unsigned)(7 - k) * 7 / 100;
            } else {
                v = v * (unsigned)(16 - (k - 7)) / 16;
            }
            bits[c] = (v * 31 + 127) / 255;
        }
        value = bits[0] << 11 | bits[1] << 6 | bits[2] << 1 | 1;
        out[0] = (uint8_t)(value >> 8);
        out[1] = (uint8_t)value;
    }
    return 1;
}

/* ---- The frame's display list ----
 * port_render_frame (game code) builds the frame's graphics tasks; each reaches port_gfx_submit, which copies its
 * commands at once, because the game builds its tasks in four records that it reuses in turn. The commands of all the
 * frame's tasks go into one list, each task's closing G_ENDDL left out, so they run one after another, and the list
 * gets one G_ENDDL at its end. A command is port.h's 16-byte Gfx, two words as wide as a pointer. */
typedef struct {
    uintptr_t w0;
    uintptr_t w1;
} FrameCmd;
_Static_assert(sizeof(FrameCmd) == 16, "a display-list command is 16 bytes, like port.h's Gfx");

#define G_SETCIMG_OP 0xFFu
#define G_ENDDL_OP 0xDFu

static FrameCmd *g_frame;
static uint32_t g_frame_count;
static uint32_t g_frame_cap;
static uint32_t g_frame_tasks;
static uint32_t g_frame_images; /* the colour images (G_SETCIMG) the frame's tasks have set */

static void frame_add(const FrameCmd *cmds, uint32_t count) {
    if (g_frame_count + count > g_frame_cap) {
        uint32_t cap = g_frame_cap != 0 ? g_frame_cap : 4096;
        FrameCmd *grown;

        while (cap < g_frame_count + count) {
            cap *= 2;
        }
        grown = realloc(g_frame, cap * sizeof(FrameCmd));
        if (grown == NULL) {
            port_fatal("port_gfx_submit: no memory for a frame of %u commands", cap);
        }
        g_frame = grown;
        g_frame_cap = cap;
    }
    memcpy(g_frame + g_frame_count, cmds, count * sizeof(FrameCmd));
    g_frame_count += count;
}

/* Checks each task as it comes, as the G7 gate asks: its list ends with G_ENDDL; every colour image it sets
 * (G_SETCIMG) is a real address, because Fast3D takes a fill rectangle into a NULL colour image for a depth clear; and
 * the frame sets one colour image only, the frame's own, which the first task's setup sets. On the N64 the frame's
 * last task then sets the next framebuffer and clears it; on PC it must not, or the clear would cover the frame
 * (submitGraphicsTask). */
void port_gfx_submit(const struct Gfx *list, uint32_t count, int32_t last) {
    const FrameCmd *cmds = (const FrameCmd *)list;
    uint32_t i;

    (void)last;
    if (count == 0 || (cmds[count - 1].w0 >> 24) != G_ENDDL_OP) {
        port_fatal("port_gfx_submit: a task's list of %u commands does not end with G_ENDDL", count);
    }
    for (i = 0; i + 1 < count; i++) {
        if ((cmds[i].w0 >> 24) == G_SETCIMG_OP) {
            if (cmds[i].w1 == 0 || g_frame_images != 0) {
                port_fatal("port_gfx_submit: command %u of the frame's task %u sets colour image %u to %p", i,
                           g_frame_tasks + 1, g_frame_images + 1, (void *)cmds[i].w1);
            }
            g_frame_images++;
        }
    }
    frame_add(cmds, count - 1);
    g_frame_tasks++;
}

const void *game_sdk_frame(uint32_t *count, uint32_t *tasks) {
    *count = g_frame_count;
    *tasks = g_frame_tasks;
    return g_frame;
}

void game_sdk_framebuffers(uint8_t *out[3]) {
    int i;

    for (i = 0; i < 3; i++) {
        out[i] = (uint8_t *)D_8002EEB0[i];
    }
}

static void sram_flush(void); /* the save memory (below) */

void port_present(int32_t kind) {
    int i;

    sram_flush();
    sound_present(); /* the sound device is handed what it wants (src/audio/sound.h) */
    if (kind == GAME_PRESENT_FRAME) {
        sound_watch_hand_off();
    }

    if (kind == GAME_PRESENT_FRAME) {
        static const FrameCmd end = { (uintptr_t)G_ENDDL_OP << 24, 0 };

        g_frames++;
        g_frame_count = 0;
        g_frame_tasks = 0;
        g_frame_images = 0;
        if (g_hooks.before_render != NULL) {
            g_hooks.before_render();
        }
        port_render_frame();
        if (g_hooks.after_render != NULL) {
            g_hooks.after_render();
        }
        if (g_frame_tasks != 0) {
            frame_add(&end, 1);
        }
    }
    if (kind == GAME_PRESENT_FRAME || kind == GAME_PRESENT_SYNC) {
        for (i = 0; i < g_idle_randoms; i++) {
            guRandom();
        }
    }
    if (g_hooks.present != NULL) {
        g_hooks.present((int)kind);
    }
}

/* ---- Message queues ----
 * A queue keeps the N64's 0x18 bytes (the game gives each one that much storage): +0 and +4 the lists of waiting
 * threads (unused here), +8 how many messages it holds, +0xC the index of the first, +0x10 how many it can hold, and
 * +0x14 the handle (port.h's PTR_TO_SLOT) of its array of 4-byte messages. Fields are read and written with memcpy,
 * because the game's storage is only sure to be 4-byte aligned. */
typedef struct {
    uint32_t mtqueue;
    uint32_t fullqueue;
    int32_t valid;
    int32_t first;
    int32_t count;
    uint32_t msgs;
} Queue;
_Static_assert(sizeof(Queue) == 0x18, "a message queue keeps the N64's 0x18 bytes");

static Queue queue_get(const void *mq) {
    Queue q;
    memcpy(&q, mq, sizeof(q));
    return q;
}

static void queue_put(void *mq, const Queue *q) {
    memcpy(mq, q, sizeof(*q));
}

void port_osCreateMesgQueue(void *mq, void *msgs, int32_t count) {
    Queue q = { 0, 0, 0, 0, count, port_ptr_to_slot(msgs) };

    if (mq == NULL || msgs == NULL || count <= 0) {
        port_fatal("osCreateMesgQueue: queue %p, messages %p, count %d", mq, msgs, (int)count);
    }
    queue_put(mq, &q);
}

int32_t port_osSendMesg(void *mq, uint32_t msg, int32_t flag) {
    Queue q = queue_get(mq);
    uint8_t *msgs = port_slot_to_ptr(q.msgs);

    (void)flag;
    if (q.valid >= q.count) {
        /* Full: on the N64 a blocking send waits for a reader, which no other thread here can be. */
        if (flag != 0) {
            port_fatal("osSendMesg: queue %p is full (%d messages) and nothing else runs to empty it", mq,
                       (int)q.count);
        }
        return -1;
    }
    memcpy(msgs + 4 * ((q.first + q.valid) % q.count), &msg, 4);
    q.valid++;
    queue_put(mq, &q);
    return 0;
}

int32_t port_osRecvMesg(void *mq, uint32_t *msg, int32_t flag) {
    Queue q = queue_get(mq);
    uint8_t *msgs;

    if (q.valid == 0) {
        if (flag == 0) {
            return -1;
        }
        port_fatal("osRecvMesg: the game waits on queue %p, which is empty, and nothing else runs to fill it", mq);
    }
    msgs = port_slot_to_ptr(q.msgs);
    if (msg != NULL) {
        memcpy(msg, msgs + 4 * q.first, 4);
    }
    q.first = (q.first + 1) % q.count;
    q.valid--;
    queue_put(mq, &q);
    return 0;
}

/* The queues and messages that osSetEventMesg registers, by event (the N64 has 15). */
#define EVENT_SI 5
static struct {
    void *mq;
    uint32_t msg;
} g_events[16];

void port_osSetEventMesg(uint32_t event, void *mq, uint32_t msg) {
    if (event >= 16) {
        port_fatal("osSetEventMesg: event %u", event);
    }
    g_events[event].mq = mq;
    g_events[event].msg = msg;
}

/* An SI transfer (a controller command) has ended: its message goes to the queue registered for the SI event. */
static void si_done(void) {
    if (g_events[EVENT_SI].mq != NULL) {
        port_osSendMesg(g_events[EVENT_SI].mq, g_events[EVENT_SI].msg, 0);
    }
}

/* ---- Controllers and the Rumble Pak ----
 * The game's records keep the N64's sizes: a controller's status is 4 bytes {u16 type, u8 status, u8 errno}, a pad is
 * 6 bytes {u16 buttons, s8 stick x, s8 stick y, u8 errno}. A controller that is not plugged in reads with errno 8
 * (CONT_NO_RESPONSE_ERROR). The pad bits come from the hooks' read_pad. */
static void write_status(uint8_t *status) {
    int i;

    for (i = 0; i < 4; i++) {
        uint16_t type = (g_plugged >> i) & 1 ? 0x0005 : 0; /* CONT_TYPE_NORMAL */
        uint8_t errno_ = (g_plugged >> i) & 1 ? 0 : 8;

        memcpy(status + 4 * i, &type, 2);
        status[4 * i + 2] = 0; /* no Controller Pak */
        status[4 * i + 3] = errno_;
    }
}

int32_t port_osContInit(void *mq, uint8_t *bitpattern, uint8_t *status) {
    (void)mq;
    *bitpattern = (uint8_t)g_plugged;
    write_status(status);
    return 0;
}

int32_t port_osContStartQuery(void *mq) {
    (void)mq;
    si_done();
    return 0;
}

void port_osContGetQuery(uint8_t *status) {
    write_status(status);
}

int32_t port_osContStartReadData(void *mq) {
    (void)mq;
    si_done();
    return 0;
}

void port_osContGetReadData(uint8_t *pads) {
    int i;

    for (i = 0; i < 4; i++) {
        GamePad pad = { 0, 0, 0 };
        uint8_t *p = pads + 6 * i;

        if ((g_plugged >> i) & 1) {
            if (g_hooks.read_pad != NULL) {
                g_hooks.read_pad(i, &pad);
            }
            memcpy(p, &pad.buttons, 2);
            p[2] = (uint8_t)pad.stick_x;
            p[3] = (uint8_t)pad.stick_y;
            p[4] = 0;
        } else {
            memset(p, 0, 4);
            p[4] = 8;
        }
        p[5] = 0;
    }
}

int32_t port_osContSetCh(uint8_t channels) {
    (void)channels;
    return 0;
}

/* No Controller Pak is plugged in, so the menus find no pak to save to: osPfsInitPak answers 1 (PFS_ERR_NOPACK), as
 * the N64 does without one, and so does osPfsFindFile, which the game only calls after osPfsInitPak has succeeded. */
int32_t osPfsInitPak(void *mq, void *pfs, int32_t channel) {
    (void)mq;
    (void)pfs;
    (void)channel;
    return 1;
}

int32_t osPfsFindFile(void *pfs, uint16_t company, uint32_t game, uint8_t *name, uint8_t *ext, int32_t *file) {
    (void)pfs;
    (void)company;
    (void)game;
    (void)name;
    (void)ext;
    (void)file;
    return 1;
}

/* The Rumble Pak. A controller has one when the program says so (game_sdk_set_rumble_paks: a game pad that can rumble,
 * on that controller's port). The game looks for one every few frames (func_80003E78, func_800042BC): osMotorInit
 * answers 0 and notes the controller's number in the game's record of the pak, at the place the N64's OSPfs keeps its
 * channel (offset 8), or 1 (PFS_ERR_NOPACK) without a pak, as the N64 does. __osMotorAccess then starts (flag 1) or
 * stops (0) that controller's motor through game_sdk_set_rumble's function. osPfsInitPak still answers "no pak" for a
 * controller with a Rumble Pak: on the N64 it answers 10 then, which the menus' pak check (func_800BB360) turns into a
 * result that takes another branch in the menus (func_800BCDF4), not yet checked on PC. */
int32_t port_osMotorInit(void *mq, void *pfs, int32_t channel) {
    (void)mq;
    if (channel < 0 || channel > 3 || !((g_rumble_paks >> channel) & 1)) {
        return 1;
    }
    memcpy((uint8_t *)pfs + 8, &channel, sizeof(channel));
    return 0;
}

int32_t port_osMotorAccess(void *pfs, int32_t flag) {
    int32_t channel;

    memcpy(&channel, (const uint8_t *)pfs + 8, sizeof(channel));
    if (channel < 0 || channel > 3 || !((g_rumble_paks >> channel) & 1)) {
        return 1; /* PFS_ERR_NOPACK */
    }
    motor(channel, flag != 0);
    return 0;
}

/* ---- Cartridge reads ----
 * osCartRomInit returns the cartridge's handle, which the game only hands back to osEPiStartDma. osEPiStartDma copies
 * the request's bytes from the ROM at once, notes the handle in the request, and posts the request's handle to its
 * queue, as the N64's PI manager does when the transfer ends. The request is port.h's PortIoMesg. */
typedef struct {
    uint16_t type;
    uint8_t pri;
    uint8_t status;
    void *retQueue;
    void *dramAddr;
    uint32_t devAddr;
    uint32_t size;
    void *piHandle;
} PortIoMesg;

static uint8_t g_cart_handle[0x74]; /* the size of the N64's OSPiHandle; the game never reads it */

/* The save memory: the cartridge's 32 KiB of battery-backed SRAM, where the menus keep the game's saves. The game
 * reaches it through a PI handle of its own, which func_80000A40 fills (device type 3, SRAM, at 0xA8000000) and hands
 * to osEPiLinkHandle once; a request through that handle has an offset into the SRAM as its devAddr, and reads
 * (direction 0) or writes (1) it. On PC the SRAM is memory, all zeros until the game writes a save
 * (game_sdk_sram). */
static uint8_t g_sram[GAME_SDK_SRAM_SIZE];
static void *g_sram_handle;
static uint32_t g_sram_writes;
static char *g_sram_path; /* the save file (game_sdk_sram_file), or NULL: none */

int game_sdk_sram_file(const char *path) {
    FILE *in;
    long size = -1;
    size_t length = strlen(path);

    free(g_sram_path);
    g_sram_path = malloc(length + 1);
    if (g_sram_path == NULL) {
        return -1;
    }
    memcpy(g_sram_path, path, length + 1);
    in = fopen(path, "rb");
    if (in == NULL) {
        return 0;
    }
    if (fseek(in, 0, SEEK_END) == 0) {
        size = ftell(in);
        rewind(in);
    }
    if (size != (long)GAME_SDK_SRAM_SIZE || fread(g_sram, 1, GAME_SDK_SRAM_SIZE, in) != GAME_SDK_SRAM_SIZE) {
        fprintf(stderr, "save: %s is not a save of %u bytes; the game starts with no save and will not write it\n",
                path, (unsigned)GAME_SDK_SRAM_SIZE);
        memset(g_sram, 0, sizeof(g_sram));
        fclose(in);
        free(g_sram_path);
        g_sram_path = NULL;
        return -1;
    }
    fclose(in);
    printf("save: read %s\n", path);
    return 1;
}

/* Writes the save memory to the save file: to a file beside it first, which then takes the save file's place, so
 * that a program stopped while writing never leaves half a save. */
static void sram_write_file(void) {
    char *temporary;
    size_t length;
    FILE *out;
    int ok;

    if (g_sram_path == NULL) {
        return;
    }
    length = strlen(g_sram_path);
    temporary = malloc(length + 5);
    if (temporary == NULL) {
        return;
    }
    memcpy(temporary, g_sram_path, length);
    memcpy(temporary + length, ".new", 5);
    out = fopen(temporary, "wb");
    ok = out != NULL && fwrite(g_sram, 1, GAME_SDK_SRAM_SIZE, out) == GAME_SDK_SRAM_SIZE;
    if (out != NULL && fclose(out) != 0) {
        ok = 0;
    }
#ifdef _WIN32
    if (ok) {
        remove(g_sram_path); /* rename does not replace a file on Windows */
    }
#endif
    if (!ok || rename(temporary, g_sram_path) != 0) {
        fprintf(stderr, "save: could not write %s\n", g_sram_path);
        remove(temporary);
    }
    free(temporary);
}

/* Writes the save file at each hand-off after the game has written to the save memory: a save, which the game may
 * make in several writes, reaches the file once. */
static void sram_flush(void) {
    static uint32_t saved;

    if (saved != g_sram_writes) {
        saved = g_sram_writes;
        sram_write_file();
    }
}

void osEPiLinkHandle(void *handle) {
    if (((const uint8_t *)handle)[4] != 3) { /* the handle's device type (OSPiHandle.type) */
        port_fatal("osEPiLinkHandle: a handle of device type %u; the game links only its SRAM's (3)",
                   ((const uint8_t *)handle)[4]);
    }
    g_sram_handle = handle;
}

uint8_t *game_sdk_sram(uint32_t *writes) {
    if (writes != NULL) {
        *writes = g_sram_writes;
    }
    return g_sram;
}

void *osCartRomInit(void) {
    return g_cart_handle;
}

int32_t port_osEPiStartDma(void *handle, PortIoMesg *mb, int32_t direction) {
    if (handle != NULL && handle == g_sram_handle) {
        if (mb->devAddr > GAME_SDK_SRAM_SIZE || mb->size > GAME_SDK_SRAM_SIZE - mb->devAddr) {
            port_fatal("osEPiStartDma: 0x%X bytes at 0x%X of the SRAM, which has 0x%X", (unsigned)mb->size,
                       (unsigned)mb->devAddr, (unsigned)GAME_SDK_SRAM_SIZE);
        }
        if (direction == 0) {
            memcpy(mb->dramAddr, g_sram + mb->devAddr, mb->size);
        } else {
            memcpy(g_sram + mb->devAddr, mb->dramAddr, mb->size);
            g_sram_writes++;
        }
    } else if (direction != 0) {
        port_fatal("osEPiStartDma: a write to the cartridge (direction %d)", (int)direction);
    } else {
        port_rom_read(mb->dramAddr, mb->devAddr, mb->size);
    }
    mb->piHandle = handle;
    if (mb->retQueue != NULL) {
        port_osSendMesg(mb->retQueue, port_ptr_to_slot(mb), 0);
    }
    return 0;
}

/* ---- Video, the scheduler, threads, interrupts and caches ----
 * No picture is drawn through these (port_present draws), and no thread but the game's runs, so they only keep what
 * the game reads back. */
void osViSetMode(void *mode) {
    (void)mode;
}

void osViSetSpecialFeatures(uint32_t features) {
    (void)features;
}

void osViSetYScale(float scale) {
    (void)scale;
}

void osViBlack(uint8_t black) {
    (void)black;
}

/* The scheduler sends each client a message at every retrace; only the render thread is a client, and it does not
 * run. */
void osScAddClient(void *sc, void *client, uint32_t queue) {
    (void)sc;
    (void)client;
    (void)queue;
}

/* The render thread and the audio thread are created but never started: on PC the game's thread does their part
 * through port_present (the sound through src/audio/sound.h). */
void osCreateThread(void *thread, int32_t id, void (*entry)(void *), void *arg, void *stack, int32_t pri) {
    (void)thread;
    (void)id;
    (void)entry;
    (void)arg;
    (void)stack;
    (void)pri;
}

void osStartThread(void *thread) {
    (void)thread;
}

static uint32_t g_int_mask = 0x3FFFFF;

uint32_t osSetIntMask(uint32_t mask) {
    uint32_t old = g_int_mask;

    g_int_mask = mask;
    return old;
}

int32_t osAfterPreNMI(void) {
    return 0;
}

void osInvalDCache(void *p, int32_t size) {
    (void)p;
    (void)size;
}

void osWritebackDCache(void *p, int32_t size) {
    (void)p;
    (void)size;
}

void osWritebackDCacheAll(void) {
}

/* ---- Music and sound effects (libmus) ----
 * The port's music player (src/audio/music.c) does what the N64's library does. Without sound (no window, or a scripted
 * or replayed run, src/main.c) nothing plays: a song or an effect gets handle 0, and every question about what plays
 * answers 0 (nothing), so such runs go the same every time. The game passes the effects' sample bank to the effect
 * functions; the player knows it already. Each call is shown to whoever watches them (sound_watch_call, the
 * comparison's --sound-lockstep) before it is made, and each question's answer after. While the Sound Player holds the
 * songs (game_sdk_hold_songs), the game's calls that would start or stop a song are let through as if made and do
 * nothing. */

/* The game loads every song into one buffer of this size (D_800487F0) before it starts it. */
#define SONG_BUFFER 0x8000

/* The Sound Player's hold on the songs (game_sdk_hold_songs), and the screen the game was on when it took them: the
 * menu, the overlay, and the song that screen asks for (-1 until it has asked), with whether it has asked for another
 * since (game_sdk_scene_changed). */
static int g_hold_songs;
static int g_player_calls;
static uint32_t g_held_song;
static int g_scene_menu;
static int g_scene_overlay;
static int g_scene_song = -1;
static int g_scene_song_changed;

void game_sdk_hold_songs(int hold) {
    g_hold_songs = hold;
    g_held_song = 0;
    g_scene_menu = D_80038A90;
    g_scene_overlay = game_vars_overlay();
    g_scene_song = -1;
    g_scene_song_changed = 0;
}

/* The game's func_8000E55C calls it with the song a screen asks for (port.h). Only the game's own calls count; the
 * Sound Player's go through func_8000E55C too. */
void port_song_asked(int32_t song) {
    if (!g_hold_songs || g_player_calls) {
        return;
    }
    if (g_scene_song < 0) {
        g_scene_song = song;
    } else if (song != g_scene_song) {
        g_scene_song_changed = 1;
    }
}

int game_sdk_scene_changed(void) {
    return g_hold_songs &&
           (g_scene_song_changed || D_80038A90 != g_scene_menu || game_vars_overlay() != g_scene_overlay);
}

void game_sdk_player_calls(int on) {
    g_player_calls = on;
}

uint32_t game_sdk_held_song(void) {
    return g_held_song;
}

/* Whether a call of the game's is held back. */
static int held_back(void) {
    return g_hold_songs && !g_player_calls;
}

uint32_t MusStartSong(void *song) {
    Music *music = sound_music();
    uint32_t handle;

    sound_watch_call("MusStartSong", 0, NULL, song);
    if (music == NULL || held_back()) {
        return 0;
    }
    handle = music_start_song(music, song, SONG_BUFFER);
    if (g_hold_songs && g_held_song == 0) {
        g_held_song = handle;
    }
    return handle;
}

void MusStop(int32_t flags, int32_t speed) {
    Music *music = sound_music();
    int32_t args[2] = { flags, speed };

    sound_watch_call("MusStop", 2, args, NULL);
    if (music != NULL) {
        music_stop(music, held_back() ? flags & ~MUSIC_SONGS_CLASS : flags, speed);
    }
}

int32_t MusAsk(int32_t flags) {
    Music *music = sound_music();
    int32_t answer;

    sound_watch_call("MusAsk", 1, &flags, NULL);
    answer = music != NULL ? music_ask(music, flags) : 0;
    sound_watch_answer(answer);
    return answer;
}

void MusSetMasterVolume(int32_t flags, int32_t volume) {
    Music *music = sound_music();
    int32_t args[2] = { flags, volume };

    sound_watch_call("MusSetMasterVolume", 2, args, NULL);
    if (music != NULL) {
        music_set_master_volume(music, flags, volume);
    }
}

uint32_t MusBankStartEffect(void *bank, int32_t number) {
    Music *music = sound_music();

    (void)bank;
    sound_watch_call("MusBankStartEffect", 1, &number, NULL);
    return music != NULL ? music_start_effect(music, number, 128, 128, 0, -1) : 0;
}

uint32_t MusBankStartEffect2(void *bank, int32_t number, int32_t volume, int32_t pan, int32_t restart, int32_t pri) {
    Music *music = sound_music();

    int32_t args[5] = { number, volume, pan, restart, pri };

    (void)bank;
    sound_watch_call("MusBankStartEffect2", 5, args, NULL);
    return music != NULL ? music_start_effect(music, number, volume, pan, restart, pri) : 0;
}

int32_t MusHandleAsk(uint32_t handle) {
    Music *music = sound_music();
    int32_t arg = (int32_t)handle, answer;

    sound_watch_call("MusHandleAsk", 1, &arg, NULL);
    answer = music != NULL ? music_handle_ask(music, handle) : 0;
    sound_watch_answer(answer);
    return answer;
}

int32_t MusHandleStop(uint32_t handle, int32_t speed) {
    Music *music = sound_music();
    int32_t args[2] = { (int32_t)handle, speed };

    sound_watch_call("MusHandleStop", 2, args, NULL);
    if (held_back() && handle == g_held_song) {
        return 0;
    }
    return music != NULL ? music_handle_stop(music, handle, speed) : 0;
}

int32_t MusHandleSetReverb(uint32_t handle, int32_t reverb) {
    Music *music = sound_music();
    int32_t args[2] = { (int32_t)handle, reverb };

    sound_watch_call("MusHandleSetReverb", 2, args, NULL);
    return music != NULL ? music_handle_set_reverb(music, handle, reverb) : 0;
}

int32_t MusHandleSetVolume(uint32_t handle, int32_t volume) {
    Music *music = sound_music();
    int32_t args[2] = { (int32_t)handle, volume };

    sound_watch_call("MusHandleSetVolume", 2, args, NULL);
    return music != NULL ? music_handle_set_volume(music, handle, volume) : 0;
}

/* The N64's D_80060500, which func_8000333C sets from the options' sound setting: while it is set, the pans the player
 * sends are centred. The player clears it at the end of each of its frames, as the N64's does. */
void func_8001613C(int32_t mode) {
    Music *music = sound_music();

    sound_watch_call("mono", 1, &mode, NULL);
    if (music != NULL) {
        music_set_mono(music, mode);
    }
}
