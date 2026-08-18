/*
 * Staff roll content.
 *
 * The Mega Drive source ends on a scrolling roll that presents the cast by
 * name over a moving background.  Its own text is baked metasprite art that
 * this port does not carry, so the roll is rebuilt here from a text table and
 * the live enemy banks: main.c scrolls this list up the front plane over the
 * generated starfield and draws each named family's own VRAM art beside it.
 *
 * Editing note: every string is uppercase-only -- the generated font covers
 * ASCII 32..95 -- and no line may exceed 32 cells.
 */
#include "game_levels_generated.h"
#include "staff_roll.h"

#define T STAFF_PAL_TITLE
#define N STAFF_PAL_NAME
#define B STAFF_PAL_BODY
#define C STAFF_COL_CENTER
#define _ 0

/* A named cast member: four rows of pitch so the tallest 48-pixel metasprite
 * clears the entry above it, with the name on the last of the four. */
#define CAST(name, kind) \
    { 0, _, C, -1 }, \
    { 0, _, C, -1 }, \
    { 0, _, C, -1 }, \
    { name, N, 14, (signed char)(kind) }

const StaffLine g_staff_lines[] = {
    /* The roll opens on an empty screen: fifteen spacers is one full 240-line
       field, so the first title scrolls in from the bottom edge rather than
       being already on screen when the fade completes. */
    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },
    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },
    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },
    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },

    { "CONGRATULATIONS", T, C, -1 },
    { 0, _, C, -1 },
    { "YOU HAVE SAVED THE KINGDOM", B, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },

    { "- THE CAST -", T, C, -1 },
    { 0, _, C, -1 },

    CAST("BARNETT",  ENEMY_KIND_GENERIC),
    CAST("GORDON",   ENEMY_KIND_MONKEY),
    CAST("MATTHEW",  ENEMY_KIND_PTERODACTYL),
    CAST("SIDNEY",   ENEMY_KIND_STRIKE_SNAKE),
    CAST("WESLEY",   ENEMY_KIND_CRATE_MIMIC),
    CAST("NORMAN",   ENEMY_KIND_FLOOR_SNAKE),
    CAST("DENNIS",   ENEMY_KIND_FLYING_KOOPA),
    CAST("CHIP",     ENEMY_KIND_CHIP),
    CAST("HOWARD",   ENEMY_KIND_THROWER),
    CAST("LESTER",   ENEMY_KIND_SPEARMAN),
    CAST("RUFUS",    ENEMY_KIND_BOULDER),
    CAST("CLIFFORD", ENEMY_KIND_SPARKER),
    CAST("WALTER",   ENEMY_KIND_HOPPER),

    { 0, _, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },

    /* Original-game credits.  The Mega Drive roll's own staff names are baked
       art that has not been transcribed, so this section credits the original
       team as it is attributed rather than inventing individual names; drop
       the real ones in here as NAME lines when they are read off the roll. */
    { "- ORIGINAL GAME -", T, C, -1 },
    { 0, _, C, -1 },
    { "SUPER MARIO BROS.", N, C, -1 },
    { "FOR SEGA MEGA DRIVE", B, C, -1 },
    { 0, _, C, -1 },
    { "DEVELOPED IN TAIWAN", B, C, -1 },
    { "BY CHUANPU TECHNOLOGY", N, C, -1 },
    { "1997", B, C, -1 },
    { 0, _, C, -1 },
    { "PROGRAM  GRAPHICS", B, C, -1 },
    { "SOUND  LEVEL DESIGN", B, C, -1 },
    { "THE ORIGINAL STAFF", N, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },

    { "- PC-FX RECONSTRUCTION -", T, C, -1 },
    { 0, _, C, -1 },
    { "ENGINE AND ASSET PORT", B, C, -1 },
    { "GAMEBLABLA", N, C, -1 },
    { 0, _, C, -1 },
    { "BUILT ON LIBPCFX", B, C, -1 },
    { "MUSIC AS RED BOOK CD-DA", B, C, -1 },
    { "SOUND AS 32 KHZ ADPCM", B, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },
    { 0, _, C, -1 },

    { "- THANK YOU -", T, C, -1 },
    { 0, _, C, -1 },
    { "FOR PLAYING", B, C, -1 },

    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },
    { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 }, { 0, _, C, -1 },
};

const unsigned short g_staff_line_count =
    (unsigned short)(sizeof(g_staff_lines) / sizeof(g_staff_lines[0]));
