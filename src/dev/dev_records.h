/* What the developer match reads of the game's match state (src/dev/dev_records.c), as plain values that port code
 * (C and C++) can use without the game's headers. C and C++. */
#ifndef TD_DEV_RECORDS_H
#define TD_DEV_RECORDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEV_WRESTLERS 4

/* One wrestler record (the game's Wrestler, include/wrestler.h), the fields the Match Inspector shows. */
typedef struct {
    int16_t character;  /* characterId: 0xBNN (bank, index + 1); -1 for an empty slot */
    int16_t state;      /* action state, an index into the handler table D_800E2FF0 */
    int16_t step;       /* step inside the state */
    int16_t anim;       /* animation id */
    int16_t frame;      /* animation frame */
    int16_t health;     /* hidden health 0..255 */
    int16_t health_max; /* recoverable ceiling */
    int16_t spirit;     /* spirit meter 0..100 */
    float x, y, z;      /* root position; the ring's inside is -200..200 in x and z */
    int32_t heading;    /* facing: 0x1000 is a full turn, 0 faces +z */
    int8_t held_by;     /* who holds or attacks this wrestler, -1 for nobody */
    int8_t opponent;    /* who it faces and attacks */
    uint64_t flags;     /* WR_FLAG_* */
} DevWrestler;

/* The match around the wrestlers. */
typedef struct {
    uint32_t match_frame; /* D_800E2FAC, the match loop's frame count */
    int8_t phase;         /* D_800EB465: 0..4, 4 when the match has ended */
    uint32_t seed;        /* xseed, guRandom's seed */
} DevMatch;

/* Reads the four wrestlers and the match. Call it while the game waits in a hand-off (port_present). */
void dev_records_read(DevWrestler out[DEV_WRESTLERS], DevMatch *match);

/* The bytes of one snapshot for the wrestler log: the four records as the PC holds them, with the address of each
 * one's profile set to 0, because addresses differ from run to run. dev_records_snapshot writes
 * dev_records_snapshot_size() bytes to out. */
size_t dev_records_snapshot_size(void);
void dev_records_snapshot(uint8_t *out);

/* The fields of one record in the snapshot, in order: name, offset and size; the log's header lists them, so that
 * tools/wr_log.py can name the field where two logs differ. */
typedef struct {
    const char *name;
    uint32_t offset;
    uint32_t size;
} DevField;
const DevField *dev_records_fields(size_t *count, size_t *record_size);

/* One work block of the scene (Nw2SceneWork in the game's common.h), what the draw-order table shows. The game keeps
 * the blocks sorted by depth, farthest first (func_80001EA8), and draws them by group in this order: the group-0
 * blocks, group 6, group 3, then the meshes of every other group, then group 5's display lists. */
typedef struct {
    uint8_t group;  /* the owning object's draw group (its +0x18), without the hide bit */
    uint8_t hidden; /* 1 when the object or the block is not drawn (bit 0x80 of the group or of the kind) */
    uint8_t flat;   /* 1: a 2D object, drawn without perspective */
    uint8_t mesh;   /* 1: a mesh (kind 1), 0: a display list */
    float depth;    /* the sort key */
    float eye_z;    /* its centre's distance in front of the camera */
} DevDrawBlock;

/* Fills out with up to max work blocks, in the game's depth order, and returns how many the scene has. */
size_t dev_records_draw_blocks(DevDrawBlock *out, size_t max);

/* The draw switches. dev_records_hide_groups(mask) stops the objects of each group whose bit is set in mask (bit n for
 * group n) from being drawn, by setting the hide bit of their draw group, and dev_records_unhide_groups puts back what
 * it changed. The developer match calls the two around the port's building of a frame (GameSdkHooks), so the game's
 * own code never sees the change. */
void dev_records_hide_groups(uint32_t mask);
void dev_records_unhide_groups(void);

/* A wrestler state's short name ("" for one docs/wrestler.md does not identify), and a match phase's name. */
const char *dev_state_name(int state);
const char *dev_phase_name(int phase);

#ifdef __cplusplus
}
#endif

#endif
