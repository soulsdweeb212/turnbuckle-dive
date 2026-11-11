/* The port's versions of the N64 SDK functions that the developer program's game files call: libultra's message
 * queues, controllers, Rumble Pak, cartridge reads, video, threads, interrupts and caches, the scheduler's client list,
 * and the music library libmus. The decompilation's include/port.h says why several are renamed port_...: their N64
 * records are smaller than LibUltraShip's.
 *
 * How the game runs on them: one thread, the game's own. On the N64 the game also has a render thread (func_80002CC4),
 * an audio thread and the scheduler, which wait on message queues; here none of them runs. The game's frame hand-offs
 * go to port_present instead (the decompilation's port.h), and the "hardware" these functions drive finishes at once:
 * a cartridge read copies the bytes from the ROM and posts its message before it returns, and so does a controller
 * read. So a game function that waits for such a message always finds it in its queue. A wait on an empty queue that
 * nothing will ever fill stops the program with a message, instead of hanging.
 *
 * Sound is not ported yet: the libmus functions do nothing and report that nothing is playing. */
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "game_sdk.h"

static GameSdkHooks g_hooks;
static unsigned g_plugged = 1;
static uint32_t g_frames;

void game_sdk_set(const GameSdkHooks *hooks, unsigned plugged) {
    if (hooks != NULL) {
        g_hooks = *hooks;
    } else {
        memset(&g_hooks, 0, sizeof(g_hooks));
    }
    g_plugged = plugged & 0xFu;
}

uint32_t game_sdk_frames(void) {
    return g_frames;
}

/* ---- The frame hand-off ----
 * On the N64 an idle thread (func_800004B8) calls guRandom in a loop whenever the CPU has nothing else to do, so the
 * random seed xseed moves on by however much idle time each frame leaves, which differs from run to run. On PC no
 * thread idles; instead each hand-off that waits for the screen (a frame, or a wait for the retrace) draws a fixed
 * number of random numbers, IDLE_RANDOMS, so the same match gives the same numbers on every run. */
#define IDLE_RANDOMS 1

uint32_t guRandom(void); /* the game's own (src/main/guRandom.c) */

void port_present(int32_t kind) {
    int i;

    if (kind == GAME_PRESENT_FRAME) {
        g_frames++;
    }
    if (kind == GAME_PRESENT_FRAME || kind == GAME_PRESENT_SYNC) {
        for (i = 0; i < IDLE_RANDOMS; i++) {
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

/* No Rumble Pak is plugged in: osMotorInit answers 1 (PFS_ERR_NOPACK), as the N64 does without one. */
int32_t port_osMotorInit(void *mq, void *pfs, int32_t channel) {
    (void)mq;
    (void)pfs;
    (void)channel;
    return 1;
}

int32_t port_osMotorAccess(void *pfs, int32_t flag) {
    (void)pfs;
    (void)flag;
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

void *osCartRomInit(void) {
    return g_cart_handle;
}

int32_t port_osEPiStartDma(void *handle, PortIoMesg *mb, int32_t direction) {
    if (direction != 0) {
        port_fatal("osEPiStartDma: a write to the cartridge (direction %d)", (int)direction);
    }
    port_rom_read(mb->dramAddr, mb->devAddr, mb->size);
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
 * through port_present, and sound is not ported yet. */
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

/* ---- Music and sound effects (libmus): not ported yet ----
 * Nothing plays: a song or an effect gets handle 0, and every question about what plays answers 0 (nothing). */
uint32_t MusStartSong(void *song) {
    (void)song;
    return 0;
}

void MusStop(int32_t flags, int32_t speed) {
    (void)flags;
    (void)speed;
}

int32_t MusAsk(int32_t flags) {
    (void)flags;
    return 0;
}

void MusSetMasterVolume(int32_t flags, int32_t volume) {
    (void)flags;
    (void)volume;
}

uint32_t MusBankStartEffect(void *bank, int32_t number) {
    (void)bank;
    (void)number;
    return 0;
}

uint32_t MusBankStartEffect2(void *bank, int32_t number, int32_t volume, int32_t pan, int32_t restart, int32_t pri) {
    (void)bank;
    (void)number;
    (void)volume;
    (void)pan;
    (void)restart;
    (void)pri;
    return 0;
}

int32_t MusHandleAsk(uint32_t handle) {
    (void)handle;
    return 0;
}

int32_t MusHandleStop(uint32_t handle, int32_t speed) {
    (void)handle;
    (void)speed;
    return 0;
}

int32_t MusHandleSetReverb(uint32_t handle, int32_t reverb) {
    (void)handle;
    (void)reverb;
    return 0;
}
