/* Names for a wrestler's action state (Wrestler.state, include/wrestler.h) and the match phase (D_800EB465), from the
 * decompilation's docs/wrestler.md, "States and grapples". src/dev/dev_records.c, compiled with the game's settings,
 * uses both enums, so a debugger knows them: in VS Code's Watch, (enum TdWrState)D_800F9D10[1].state shows a state's
 * name, and (enum TdMatchPhase)D_800EB465 the phase's. States the document does not identify have no name and show as
 * numbers. C only. */
#ifndef TD_WR_STATES_H
#define TD_WR_STATES_H

enum TdWrState {
    TD_WR_PREMATCH = 0x00,          /* waiting for the match to start */
    TD_WR_STANDING = 0x01,          /* under control; every button action starts here */
    TD_WR_APRON_TO_CORNER = 0x02,   /* (guess) on the apron, moving toward a corner post */
    TD_WR_WALKING = 0x03,
    TD_WR_RUNNING = 0x04,
    TD_WR_BUMPED = 0x05,            /* bumped into another wrestler */
    TD_WR_STRIKE_06 = 0x06,         /* strikes and attacks; hits resolve in func_800C3A78 */
    TD_WR_STRIKE_07 = 0x07,
    TD_WR_STRIKE_08 = 0x08,
    TD_WR_APRON_BLOCK = 0x09,       /* (guess) block pressed on the apron */
    TD_WR_TURNBUCKLE = 0x0A,        /* climbing or standing on the turnbuckle */
    TD_WR_WHIPPER = 0x0B,           /* Irish whip: the whipper */
    TD_WR_WHIPPED = 0x0C,           /* Irish whip: the one whipped; can reverse */
    TD_WR_ROPE_WHIP_0D = 0x0D,      /* whip-like pairs from a grapple across the ropes */
    TD_WR_ROPE_WHIP_0E = 0x0E,
    TD_WR_ROPE_WHIP_0F = 0x0F,
    TD_WR_ROPE_WHIP_10 = 0x10,
    TD_WR_ROPE_REBOUND = 0x14,      /* running into the ropes and rebounding */
    TD_WR_TAUNT = 0x17,             /* turns full spirit into SPECIAL */
    TD_WR_DODGE = 0x19,             /* dodges or leapfrogs a running opponent */
    TD_WR_STRIKE_1B = 0x1B,
    TD_WR_STRIKE_1C = 0x1C,
    TD_WR_STRIKE_1D = 0x1D,
    TD_WR_STRIKE_1E = 0x1E,
    TD_WR_STRIKE_1F = 0x1F,
    TD_WR_STRIKE_20 = 0x20,
    TD_WR_STRIKE_21 = 0x21,
    TD_WR_STRIKE_22 = 0x22,
    TD_WR_STRIKE_23 = 0x23,
    TD_WR_STOMP = 0x24,             /* a strike on a downed opponent */
    TD_WR_STRIKE_25 = 0x25,
    TD_WR_STRIKE_26 = 0x26,
    TD_WR_STRIKE_27 = 0x27,
    TD_WR_HIT = 0x28,               /* hit reaction; +0x30 describes the hit */
    TD_WR_KNOCKED_OUT = 0x29,
    TD_WR_KO_DAZED = 0x2A,
    TD_WR_SETTLING = 0x2B,          /* settling after a move */
    TD_WR_LYING = 0x2C,             /* lying on the mat */
    TD_WR_ON_ROPES = 0x2D,          /* (guess) leaning on the ropes or in the corner */
    TD_WR_DAZED = 0x2E,
    TD_WR_STAGGER = 0x2F,
    TD_WR_RUN_ATTACK_DOWNED = 0x30, /* running attack on a downed opponent: attacker / victim */
    TD_WR_RUN_ATTACKED_DOWN = 0x31,
    TD_WR_PICKING_UP = 0x32,        /* picking up a downed opponent: picker / picked up */
    TD_WR_PICKED_UP = 0x33,
    TD_WR_PINNING = 0x38,
    TD_WR_PINNED = 0x39,
    TD_WR_GROUND_GRAPPLE = 0x3A,    /* ground grapple: attacker / downed victim */
    TD_WR_GROUND_GRAPPLED = 0x3B,
    TD_WR_SUBMISSION = 0x3C,        /* submission hold: holder / held */
    TD_WR_IN_SUBMISSION = 0x3D,
    TD_WR_PIN_MOVE = 0x3E,          /* a pin that is a move from the move list */
    TD_WR_PIN_MOVE_TAKEN = 0x3F,
    TD_WR_BLOCKING = 0x40,
    TD_WR_BLOCKED_HIT = 0x41,
    TD_WR_GETTING_UP = 0x43,
    TD_WR_GRAPPLE_REACH = 0x44,
    TD_WR_TIED_UP = 0x45,           /* grapple: tied up (the grappler / the other) */
    TD_WR_TIED_UP_OTHER = 0x46,
    TD_WR_LOCK_UP = 0x47,           /* (guess) lock-up between characters with the lock-up ability */
    TD_WR_LOCK_UP_OTHER = 0x48,
    TD_WR_MOVE = 0x49,              /* grapple move: performing / taking */
    TD_WR_MOVE_TAKEN = 0x4A,
    TD_WR_GRAPPLE_FLAG_4F = 0x4F,   /* a flag-driven grapple variant */
    TD_WR_GRAPPLE_FLAG_50 = 0x50,
    TD_WR_STOMP_55 = 0x55,
    TD_WR_STOMP_56 = 0x56,
    TD_WR_COUNTER_58 = 0x58,        /* counters to strikes */
    TD_WR_COUNTER_59 = 0x59,
    TD_WR_COUNTER_5A = 0x5A,
    TD_WR_COUNTER_5B = 0x5B,
    TD_WR_COUNTER_5C = 0x5C,
    TD_WR_COUNTER_5D = 0x5D,
    TD_WR_COUNTER_5E = 0x5E,
    TD_WR_GRAPPLE_DODGED = 0x5F,
    TD_WR_ZONE_60 = 0x60,           /* moving between ring, apron and floor */
    TD_WR_ZONE_61 = 0x61,
    TD_WR_ZONE_62 = 0x62,
    TD_WR_DIVE_63 = 0x63,           /* dives and springboards */
    TD_WR_DIVE_64 = 0x64,
    TD_WR_DIVE_65 = 0x65,
    TD_WR_DIVE_66 = 0x66,
    TD_WR_DIVE_67 = 0x67,
    TD_WR_DIVE_68 = 0x68,
    TD_WR_DIVE_69 = 0x69,
    TD_WR_TOP_ROPE_6B = 0x6B,       /* dives from the top rope; can land in a pin */
    TD_WR_TOP_ROPE_6C = 0x6C,
    TD_WR_TOP_ROPE_6D = 0x6D,
    TD_WR_TOP_ROPE_6E = 0x6E,
    TD_WR_TOP_ROPE_6F = 0x6F,
    TD_WR_ZONE_70 = 0x70,
    TD_WR_DOUBLE_TEAM_71 = 0x71,
    TD_WR_APRON_GRAPPLE = 0x72,     /* grapple on the apron */
    TD_WR_APRON_GRAPPLED = 0x73,
    TD_WR_DOUBLE_TEAM_76 = 0x76,    /* double team: the attackers / the victim */
    TD_WR_DOUBLE_TEAM_77 = 0x77,
    TD_WR_DOUBLE_TEAMED = 0x78,
    TD_WR_FLOOR_TO_RING = 0x79,     /* grapple from the floor to the ring */
    TD_WR_FLOOR_TO_RING_TAKEN = 0x7A,
    TD_WR_FLOOR_TO_APRON = 0x87,    /* grapple from the floor to the apron */
    TD_WR_FLOOR_TO_APRON_TAKEN = 0x88,
    TD_WR_TURNBUCKLE_GRAPPLE = 0x7B,
    TD_WR_TURNBUCKLE_GRAPPLED = 0x7C,
    TD_WR_TAG_7F = 0x7F,            /* tag: exchange, partner reaching from the apron */
    TD_WR_TAG_80 = 0x80,
    TD_WR_TAG_81 = 0x81,
    TD_WR_ZONE_82 = 0x82,
    TD_WR_STEP_OVER = 0x89,         /* (guess) stepping over a downed opponent: standing / walking */
    TD_WR_STEP_OVER_WALKING = 0x8A,
    TD_WR_GRAPPLE_FLAG_8B = 0x8B,
    TD_WR_GRAPPLE_FLAG_8C = 0x8C,
    TD_WR_RUN_IN = 0x8F,            /* a new wrestler entering */
};

enum TdMatchPhase {
    TD_PHASE_SETUP = 0,   /* resets the match's parts */
    TD_PHASE_INTRO = 1,   /* the intro playback */
    TD_PHASE_FIGHT = 2,   /* the match itself; its end checks move the phase on */
    TD_PHASE_ENDING = 3,
    TD_PHASE_RESULTS = 4, /* the results screen: the match has ended */
};

#endif
