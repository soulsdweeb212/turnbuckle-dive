/* Reads the game's match state for the developer match (src/dev/dev_records.h). It is compiled with the game's
 * settings and includes the game's headers, so it reads the records by their types; the rest of the port sees only
 * the plain values of dev_records.h. It only reads the game's values, but for the draw switches, which change the
 * objects' draw groups while the port builds a frame and then put them back (dev_records_hide_groups). */
#include <string.h> /* system headers come before common.h, whose port.h renames some C library functions */

#include "common.h"

#include "dev_records.h"
#include "wr_states.h"

extern u32 D_800E2FAC; /* the match loop's frame count (func_8009A374) */
extern s8 D_800EB465;  /* the match phase (runMatchPhaseStateMachine) */
extern u32 xseed;      /* guRandom's seed */

void dev_records_read(DevWrestler out[DEV_WRESTLERS], DevMatch *match) {
    int i;

    for (i = 0; i < DEV_WRESTLERS; i++) {
        const Wrestler *w = &D_800F9D10[i];
        DevWrestler *o = &out[i];

        o->character = w->characterId;
        o->state = w->state;
        o->step = w->step;
        o->anim = w->animId;
        o->frame = w->animFrame;
        o->health = w->health;
        o->health_max = w->healthMax;
        o->spirit = w->spirit;
        o->x = w->x;
        o->y = w->y;
        o->z = w->z;
        o->heading = w->heading;
        o->held_by = w->heldById;
        o->opponent = w->opponentId;
        o->flags = w->flags;
    }
    match->match_frame = D_800E2FAC;
    match->phase = D_800EB465;
    match->seed = xseed;
}

size_t dev_records_snapshot_size(void) {
    return sizeof(D_800F9D10);
}

void dev_records_snapshot(uint8_t *out) {
    int i;

    memcpy(out, D_800F9D10, sizeof(D_800F9D10));
    for (i = 0; i < DEV_WRESTLERS; i++) {
        memset(out + i * sizeof(Wrestler) + offsetof(Wrestler, profile), 0, sizeof(void *)); /* the profile's address */
    }
}

/* Every field of Wrestler, in order (include/wrestler.h). A field added there and missing here shows in the log as
 * part of the field before it; the assert below catches a list that no longer ends at the record's end. */
#define F(name) { #name, (uint32_t)offsetof(Wrestler, name), (uint32_t)sizeof(((Wrestler *)0)->name) }
static const DevField kFields[] = {
    F(id), F(unk001), F(characterId), F(animFramesLeft), F(animFrame), F(animId), F(state), F(step), F(stepFlags),
    F(health), F(healthMax), F(spirit), F(stateTimer), F(escapeTimer), F(unk01A), F(flags), F(moveMode), F(unk02A),
    F(grappleCtx), F(hitInfo), F(unk034), F(koFlags), F(unk036), F(frameStatus), F(frameBits), F(unk039),
    F(counterKind), F(ringSide), F(dirIndex), F(knockdowns), F(pose), F(comboHits), F(lastMoveTakenId),
    F(lastMoveTakenParam), F(nextCharacterId), F(unk04A), F(x), F(y), F(z), F(heading), F(jointA), F(jointB),
    F(moveDelta), F(turnPerTick), F(bodyPart), F(heldById), F(secondAttackerId), F(opponentId), F(steppedOverId),
    F(partnerId), F(unk356), F(partnerFlags), F(pairSignals), F(attached), F(profile), F(anim), F(moveOwnerId),
    F(unk385), F(moveId), F(moveStatKind), F(unk38A), F(move),
};
#undef F
_Static_assert(offsetof(Wrestler, move) + sizeof(WrMove) == sizeof(Wrestler),
               "kFields must list every field of Wrestler, up to its last, move");

const DevField *dev_records_fields(size_t *count, size_t *record_size) {
    *count = sizeof(kFields) / sizeof(kFields[0]);
    *record_size = sizeof(Wrestler);
    return kFields;
}

/* The states' and phases' names, for the Match Inspector (src/dev/wr_states.h gives the enums). */
static const char *const kStateNames[0x92] = {
    [TD_WR_PREMATCH] = "prematch",
    [TD_WR_STANDING] = "standing",
    [TD_WR_APRON_TO_CORNER] = "apron to corner",
    [TD_WR_WALKING] = "walking",
    [TD_WR_RUNNING] = "running",
    [TD_WR_BUMPED] = "bumped",
    [TD_WR_STRIKE_06] = "strike 06",
    [TD_WR_STRIKE_07] = "strike 07",
    [TD_WR_STRIKE_08] = "strike 08",
    [TD_WR_APRON_BLOCK] = "apron block",
    [TD_WR_TURNBUCKLE] = "turnbuckle",
    [TD_WR_WHIPPER] = "whipper",
    [TD_WR_WHIPPED] = "whipped",
    [TD_WR_ROPE_WHIP_0D] = "rope whip 0d",
    [TD_WR_ROPE_WHIP_0E] = "rope whip 0e",
    [TD_WR_ROPE_WHIP_0F] = "rope whip 0f",
    [TD_WR_ROPE_WHIP_10] = "rope whip 10",
    [TD_WR_ROPE_REBOUND] = "rope rebound",
    [TD_WR_TAUNT] = "taunt",
    [TD_WR_DODGE] = "dodge",
    [TD_WR_STRIKE_1B] = "strike 1b",
    [TD_WR_STRIKE_1C] = "strike 1c",
    [TD_WR_STRIKE_1D] = "strike 1d",
    [TD_WR_STRIKE_1E] = "strike 1e",
    [TD_WR_STRIKE_1F] = "strike 1f",
    [TD_WR_STRIKE_20] = "strike 20",
    [TD_WR_STRIKE_21] = "strike 21",
    [TD_WR_STRIKE_22] = "strike 22",
    [TD_WR_STRIKE_23] = "strike 23",
    [TD_WR_STOMP] = "stomp",
    [TD_WR_STRIKE_25] = "strike 25",
    [TD_WR_STRIKE_26] = "strike 26",
    [TD_WR_STRIKE_27] = "strike 27",
    [TD_WR_HIT] = "hit",
    [TD_WR_KNOCKED_OUT] = "knocked out",
    [TD_WR_KO_DAZED] = "ko dazed",
    [TD_WR_SETTLING] = "settling",
    [TD_WR_LYING] = "lying",
    [TD_WR_ON_ROPES] = "on ropes",
    [TD_WR_DAZED] = "dazed",
    [TD_WR_STAGGER] = "stagger",
    [TD_WR_RUN_ATTACK_DOWNED] = "run attack downed",
    [TD_WR_RUN_ATTACKED_DOWN] = "run attacked down",
    [TD_WR_PICKING_UP] = "picking up",
    [TD_WR_PICKED_UP] = "picked up",
    [TD_WR_PINNING] = "pinning",
    [TD_WR_PINNED] = "pinned",
    [TD_WR_GROUND_GRAPPLE] = "ground grapple",
    [TD_WR_GROUND_GRAPPLED] = "ground grappled",
    [TD_WR_SUBMISSION] = "submission",
    [TD_WR_IN_SUBMISSION] = "in submission",
    [TD_WR_PIN_MOVE] = "pin move",
    [TD_WR_PIN_MOVE_TAKEN] = "pin move taken",
    [TD_WR_BLOCKING] = "blocking",
    [TD_WR_BLOCKED_HIT] = "blocked hit",
    [TD_WR_GETTING_UP] = "getting up",
    [TD_WR_GRAPPLE_REACH] = "grapple reach",
    [TD_WR_TIED_UP] = "tied up",
    [TD_WR_TIED_UP_OTHER] = "tied up other",
    [TD_WR_LOCK_UP] = "lock up",
    [TD_WR_LOCK_UP_OTHER] = "lock up other",
    [TD_WR_MOVE] = "move",
    [TD_WR_MOVE_TAKEN] = "move taken",
    [TD_WR_GRAPPLE_FLAG_4F] = "grapple flag 4f",
    [TD_WR_GRAPPLE_FLAG_50] = "grapple flag 50",
    [TD_WR_STOMP_55] = "stomp 55",
    [TD_WR_STOMP_56] = "stomp 56",
    [TD_WR_COUNTER_58] = "counter 58",
    [TD_WR_COUNTER_59] = "counter 59",
    [TD_WR_COUNTER_5A] = "counter 5a",
    [TD_WR_COUNTER_5B] = "counter 5b",
    [TD_WR_COUNTER_5C] = "counter 5c",
    [TD_WR_COUNTER_5D] = "counter 5d",
    [TD_WR_COUNTER_5E] = "counter 5e",
    [TD_WR_GRAPPLE_DODGED] = "grapple dodged",
    [TD_WR_ZONE_60] = "zone 60",
    [TD_WR_ZONE_61] = "zone 61",
    [TD_WR_ZONE_62] = "zone 62",
    [TD_WR_DIVE_63] = "dive 63",
    [TD_WR_DIVE_64] = "dive 64",
    [TD_WR_DIVE_65] = "dive 65",
    [TD_WR_DIVE_66] = "dive 66",
    [TD_WR_DIVE_67] = "dive 67",
    [TD_WR_DIVE_68] = "dive 68",
    [TD_WR_DIVE_69] = "dive 69",
    [TD_WR_TOP_ROPE_6B] = "top rope 6b",
    [TD_WR_TOP_ROPE_6C] = "top rope 6c",
    [TD_WR_TOP_ROPE_6D] = "top rope 6d",
    [TD_WR_TOP_ROPE_6E] = "top rope 6e",
    [TD_WR_TOP_ROPE_6F] = "top rope 6f",
    [TD_WR_ZONE_70] = "zone 70",
    [TD_WR_DOUBLE_TEAM_71] = "double team 71",
    [TD_WR_APRON_GRAPPLE] = "apron grapple",
    [TD_WR_APRON_GRAPPLED] = "apron grappled",
    [TD_WR_DOUBLE_TEAM_76] = "double team 76",
    [TD_WR_DOUBLE_TEAM_77] = "double team 77",
    [TD_WR_DOUBLE_TEAMED] = "double teamed",
    [TD_WR_FLOOR_TO_RING] = "floor to ring",
    [TD_WR_FLOOR_TO_RING_TAKEN] = "floor to ring taken",
    [TD_WR_FLOOR_TO_APRON] = "floor to apron",
    [TD_WR_FLOOR_TO_APRON_TAKEN] = "floor to apron taken",
    [TD_WR_TURNBUCKLE_GRAPPLE] = "turnbuckle grapple",
    [TD_WR_TURNBUCKLE_GRAPPLED] = "turnbuckle grappled",
    [TD_WR_TAG_7F] = "tag 7f",
    [TD_WR_TAG_80] = "tag 80",
    [TD_WR_TAG_81] = "tag 81",
    [TD_WR_ZONE_82] = "zone 82",
    [TD_WR_STEP_OVER] = "step over",
    [TD_WR_STEP_OVER_WALKING] = "step over walking",
    [TD_WR_GRAPPLE_FLAG_8B] = "grapple flag 8b",
    [TD_WR_GRAPPLE_FLAG_8C] = "grapple flag 8c",
    [TD_WR_RUN_IN] = "run in",
};

static const char *const kPhaseNames[] = {
    [TD_PHASE_SETUP] = "setup", [TD_PHASE_INTRO] = "intro", [TD_PHASE_FIGHT] = "fight",
    [TD_PHASE_ENDING] = "ending", [TD_PHASE_RESULTS] = "results",
};

extern s16 D_80047062; /* how many work blocks D_80046860 holds */

size_t dev_records_draw_blocks(DevDrawBlock *out, size_t max) {
    size_t n = (size_t)(u16)D_80047062;
    size_t i;

    for (i = 0; i < n && i < max; i++) {
        const Nw2SceneWork *w = D_80046860[i];
        const Nw2SceneObj *o = SLOT_TO_PTR(w->owner);

        out[i].group = (uint8_t)(o->group & 0x7F);
        out[i].hidden = (uint8_t)((o->group & 0x80) != 0 || (w->kind & 0x80) != 0);
        out[i].flat = (uint8_t)(o->flat != 0);
        out[i].mesh = (uint8_t)((w->kind & 0x7F) == 1);
        out[i].depth = w->depth;
        out[i].eye_z = w->eye[2];
    }
    return n;
}

/* The objects whose groups dev_records_hide_groups changed, and their groups before. */
static struct {
    Nw2SceneObj *obj;
    u8 group;
} g_hidden[512];
static size_t g_hidden_count;

void dev_records_hide_groups(uint32_t mask) {
    size_t n = (size_t)(u16)D_80047062;
    size_t i, k;

    g_hidden_count = 0;
    for (i = 0; i < n; i++) {
        Nw2SceneObj *o = SLOT_TO_PTR(((const Nw2SceneWork *)D_80046860[i])->owner);

        if ((o->group & 0x80) != 0 || (o->group & 0x7F) >= 32 || !(mask & (1u << (o->group & 0x7F)))) {
            continue;
        }
        for (k = 0; k < g_hidden_count && g_hidden[k].obj != o; k++) {
        }
        if (k == g_hidden_count && g_hidden_count < sizeof(g_hidden) / sizeof(g_hidden[0])) {
            g_hidden[g_hidden_count].obj = o;
            g_hidden[g_hidden_count].group = o->group;
            g_hidden_count++;
            o->group |= 0x80;
        }
    }
}

void dev_records_unhide_groups(void) {
    while (g_hidden_count > 0) {
        g_hidden_count--;
        g_hidden[g_hidden_count].obj->group = g_hidden[g_hidden_count].group;
    }
}

const char *dev_state_name(int state) {
    return state >= 0 && state < 0x92 && kStateNames[state] != NULL ? kStateNames[state] : "";
}

const char *dev_phase_name(int phase) {
    return phase >= 0 && phase <= TD_PHASE_RESULTS ? kPhaseNames[phase] : "?";
}

/* One variable of each enum, so that the debug information describes them and a debugger can cast to them. */
const enum TdWrState td_wr_state_names_for_debugger = TD_WR_STANDING;
const enum TdMatchPhase td_match_phase_names_for_debugger = TD_PHASE_FIGHT;
