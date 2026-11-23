/* What the developer match reads of the game's match state (src/dev/dev_records.c), as plain values that port code
 * (C and C++) can use without the game's headers. C and C++. */
#ifndef TD_DEV_RECORDS_H
#define TD_DEV_RECORDS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

/* The wrestler log: a binary file, in the PC's byte order (little-endian): the header "TDWRLOG1", then u32 record size,
 * u32 number of records (4), u32 number of fields, and for each field u16 name length, the name, u32 offset and u32
 * size (dev_records_fields); then one entry per frame: u32 frame, u32 match frame (D_800E2FAC), u32 seed (xseed), i32
 * phase, and the four records (dev_records_snapshot). tools/wr_log.py reads it. dev_records_log_open returns NULL when
 * the file cannot be written; dev_records_log_frame adds the entry of `frame` (the hand-offs so far). */
FILE *dev_records_log_open(const char *path);
void dev_records_log_frame(FILE *log, uint32_t frame, const DevMatch *match);

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

/* The match frame, D_800E2FAC. */
uint32_t dev_records_match_frame(void);

/* Fills out with up to max work blocks, in the game's depth order, and returns how many the scene has. */
size_t dev_records_draw_blocks(DevDrawBlock *out, size_t max);

/* The draw switches. dev_records_hide_groups(mask) stops the objects of each group whose bit is set in mask (bit n for
 * group n) from being drawn, by setting the hide bit of their draw group, and dev_records_unhide_groups puts back what
 * it changed. The developer match calls the two around the port's building of a frame (GameSdkHooks), so the game's
 * own code never sees the change. */
void dev_records_hide_groups(uint32_t mask);
void dev_records_unhide_groups(void);

/* One entity's costume, for the Costume Colours window: the four entities are the wrestlers the game has loaded,
 * in a match or a menu. character is the wrestler's character id; colours has bit 1 set when its costume has parts
 * that take colour 1, and bit 2 for colour 2 (func_8000B46C); original[1] and [2] are those colours as the costume
 * was painted, 0xRRGGBB (func_8000B550); level[1] and [2] the Costume Change menu's choices, 0 to 14, or 15 for the
 * original. */
#define DEV_COSTUMES 4
typedef struct {
    uint16_t character;
    uint8_t colours;
    uint8_t level[3];
    uint32_t original[3];
} DevCostume;

/* Fills out with entity n's costume and returns 1, or returns 0 when the entity shows no wrestler. Call it while the
 * game waits in a hand-off, as every function here. */
int dev_records_costume(int n, DevCostume *out);

/* Colours again every palette of each entity that shows wrestler `character` (func_8000B884, as func_8000BB40 does
 * when the Costume Change menu changes a colour), so that a new costume colour (game_sdk_set_costume_colour) shows at
 * once. */
void dev_records_recolour(unsigned character);

/* The Player Console (src/dev/player_console.cpp): each player as the game holds it, and a log of what changes. */

/* Where a player stands, from the game's zone flags (WR_FLAG_IN_RING, WR_FLAG_ON_APRON, WR_FLAG_ON_FLOOR). Normally one
 * is set; two while climbing in from the apron (ring and apron) or diving off it (apron and floor). */
#define DEV_ZONE_RING 1
#define DEV_ZONE_APRON 2
#define DEV_ZONE_FLOOR 4

/* One player, slot 0 to 3. */
typedef struct {
    int16_t character;           /* the record's characterId as the game stores it: 0xBNN; -1 for an empty slot */
    uint8_t bank;                /* its bank, bits 8..11 */
    uint8_t entry;               /* its entry in the bank, the low byte minus 1 */
    int16_t costume;             /* the costume the game loaded for the slot (D_8007DFB0), -1 when unknown */
    char name[26];               /* the game's long and short names (buildWrestlerNameRecord) */
    char short_name[12];
    uint32_t n64_record;         /* the record's N64 address: D_800F9D10 + slot * 0x3B0 */
    uint32_t n64_character;      /* characterId's N64 address: n64_record + 0x002 */
    uint32_t n64_entity_character; /* the slot's entry of the game's character table D_8006A2A4: 0x8006A2A4 + 2 * slot */
    uint16_t entity_character;   /* that entry's value */
    const void *pc_record;       /* the record in the PC's memory, for a debugger */
    uint32_t character_offset;   /* characterId's offset in the record (0x002 on both machines) */
    int16_t anim;                /* animId, a resource id (0xE39 and up) */
    int16_t anim_frame;          /* animFrame, counting up from 0 */
    int16_t anim_frames;         /* the animation's length in frames (anim.frameCount) */
    int16_t state;               /* the action state (dev_state_name) */
    float x, y, z;               /* root position; Y 90 on the mat and the apron, 0 on the floor */
    uint8_t zone;                /* DEV_ZONE_* bits */
    int16_t ring_side;           /* side of the ring: 0 +Z, 1 +X, 2 -Z, 3 -X */
} DevPlayer;

/* Fills out with the four players and returns 1 while a match runs (overlay 2 loaded); returns 0 otherwise, when the
 * records hold no match. Call it while the game waits in a hand-off, as every function here. */
int dev_players_read(DevPlayer out[DEV_WRESTLERS], uint32_t *match_frame);

/* One line of the Player Console's log. */
#define DEV_EVENT_ANIM 0        /* the game's func_800D427C started an animation */
#define DEV_EVENT_ANIM_DIRECT 1 /* the animation id changed without func_800D427C (the game set animId itself) */
#define DEV_EVENT_ZONE 2        /* the player's zone changed */
typedef struct {
    uint32_t match_frame; /* D_800E2FAC when it happened */
    uint8_t kind;         /* DEV_EVENT_* */
    int8_t slot;
    int16_t character;
    int16_t value;    /* the new animation id, or the new zone */
    int16_t previous; /* the animation id or the zone it replaces */
    int32_t mode;     /* func_800D427C's mode (DEV_EVENT_ANIM only) */
    float x, y, z;
} DevEvent;

/* Starts listening to the game (game_sdk_set_anim_hook). */
void dev_players_listen(void);
/* Called once per game frame, in the hand-off: notes the animation and zone changes that did not pass through
 * func_800D427C. */
void dev_players_frame(void);
/* Moves up to max of the events since the last call into out, oldest first, and returns how many. When more than the
 * buffer holds (1024) came in between, the oldest are lost and *lost says how many. */
size_t dev_players_take(DevEvent *out, size_t max, uint32_t *lost);

/* A wrestler state's short name ("" for one docs/wrestler.md does not identify), and a match phase's name. */
const char *dev_state_name(int state);
const char *dev_phase_name(int phase);

#ifdef __cplusplus
}
#endif

#endif
