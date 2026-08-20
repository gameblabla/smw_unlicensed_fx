/*
 * Staff roll content.
 *
 * The Mega Drive source ends on a scrolling roll that presents the cast by
 * name over a moving background.  Its own text is baked metasprite art that
 * this port does not carry, so the roll is rebuilt here from a text table and
 * the live enemy banks: main.c scrolls this list up the front plane over the
 * generated starfield and draws each named member's own VRAM art beside it.
 *
 * The cast names variants rather than ENEMY_KIND families.  Most of the roster
 * -- the koopas, the ghost cat, the candle, the golfer, the duck -- is
 * ENEMY_KIND_GENERIC, so picking one member per kind showed the same walker
 * repeatedly and left everything else out.
 *
 * Editing note: every string is uppercase-only -- the generated font covers
 * ASCII 32..95 -- and no line may exceed 32 cells.
 */
#include "staff_roll.h"

/* Cast roster, in the order the roll presents it.  The variant numbers are
 * positions in g_enemy_variants; families with several palette variants are
 * represented by whichever one the player meets first. */
const StaffCast g_staff_cast[] = {
    {  0, STAFF_POSE_WALK  },  /* 0  shelled walker            */
    {  2, STAFF_POSE_WALK  },  /* 1  brown stomper             */
    { 16, STAFF_POSE_CRATE },  /* 2  crate mimic, walking      */
    { 17, STAFF_POSE_WALK  },  /* 3  monkey in a barrel        */
    { 20, STAFF_POSE_FLY   },  /* 4  green pterodactyl, flying */
    { 23, STAFF_POSE_WALK  },  /* 5  rearing snake             */
    { 24, STAFF_POSE_WALK  },  /* 6  fish                      */
    { 15, STAFF_POSE_WALK  },  /* 7  satchel flier             */
    { 29, STAFF_POSE_WALK  },  /* 8  golfer                    */
    { 27, STAFF_POSE_WALK  },  /* 9  spear thrower             */
    { 37, STAFF_POSE_WALK  },  /* 10 crested duck              */
    { 38, STAFF_POSE_WALK  },  /* 11 long-legged bird          */
    { 45, STAFF_POSE_WALK  },  /* 12 capped runner             */
    { 47, STAFF_POSE_WALK  },  /* 13 ghost cat                 */
    { 50, STAFF_POSE_WALK  },  /* 14 candle                    */
};
const unsigned short g_staff_cast_count =
    (unsigned short)(sizeof(g_staff_cast) / sizeof(g_staff_cast[0]));

#define T STAFF_PAL_TITLE
#define N STAFF_PAL_NAME
#define B STAFF_PAL_BODY
#define C STAFF_COL_CENTER
#define _ 0
#define BLANK { 0, _, C, -1 }

/* A named cast member: four rows of pitch so the tallest metasprite clears the
 * entry above it, with the name on the last of the four. */
#define CAST(name, index) \
    BLANK, BLANK, BLANK, { name, N, 14, (signed char)(index) }

const StaffLine g_staff_lines[] = {
    /* The roll opens on an empty screen: fifteen spacers is one full 240-line
       field, so the first title scrolls in from the bottom edge rather than
       being already on screen when the fade completes. */
    BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK,
    BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK,

    { "CONGRATULATIONS", T, C, -1 },
    BLANK,
    { "YOU HAVE SAVED THE KINGDOM", B, C, -1 },
    BLANK,
    BLANK,
    BLANK,

    { "- THE CAST -", T, C, -1 },
    BLANK,

    CAST("KOOPA",    0),
    CAST("GOOMBA",   1),
    CAST("DALE",     2),
    CAST("GORDON",   3),
    CAST("MATTHEW",  4),
    CAST("SIDNEY",   5),
    CAST("HARDY",    6),
    CAST("CHIP",     7),
    CAST("JEFF",     8),
    CAST("LESTER",   9),
    CAST("DICK",    10),
    CAST("WALTER",  11),
    CAST("RUFUS",   12),
    CAST("BELL",    13),
    CAST("CANDLE",  14),

    BLANK,
    BLANK,
    BLANK,

    /* Original-game credits.  The Mega Drive roll's own staff names are baked
       art that has not been transcribed, so this section credits the original
       team as it is attributed rather than inventing individual names; drop
       the real ones in here as NAME lines when they are read off the roll. */
    { "- ORIGINAL GAME -", T, C, -1 },
    BLANK,
    { "SUPER MARIO BROS.", N, C, -1 },
    { "FOR SEGA MEGA DRIVE", B, C, -1 },
    BLANK,
    { "DEVELOPED IN TAIWAN", B, C, -1 },
    { "BY GAMTEC", N, C, -1 },
    { "1994", B, C, -1 },
    BLANK,
    { "PROGRAM  GRAPHICS", B, C, -1 },
    { "SOUND  LEVEL DESIGN", B, C, -1 },
    { "THE ORIGINAL STAFF", N, C, -1 },
    BLANK,
    BLANK,
    BLANK,

    { "- PC-FX RECONSTRUCTION -", T, C, -1 },
    BLANK,
    { "ENGINE AND ASSET PORT", B, C, -1 },
    { "GAMEBLABLA", N, C, -1 },
    BLANK,
    { "BUILT ON LIBPCFX", B, C, -1 },
    { "MUSIC AS RED BOOK CD-DA", B, C, -1 },
    { "SOUND AS 16 KHZ ADPCM", B, C, -1 },
    BLANK,
    BLANK,
    BLANK,

    { "- THANK YOU -", T, C, -1 },
    BLANK,
    { "FOR PLAYING", B, C, -1 },

    BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK,
};

const unsigned short g_staff_line_count =
    (unsigned short)(sizeof(g_staff_lines) / sizeof(g_staff_lines[0]));
