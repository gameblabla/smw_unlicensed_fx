/*
 * Super Mario Bros. (unlicensed Mega Drive) -> PC-FX reconstruction
 * revision 12 regression repair pass, based directly on the r11 full-game tree:
 * independent reserve lives/continues, restored Level-2 moving platforms, source-palette
 * gameplay sprites, Y+16 cloud backgrounds, and full-face title-wall continuation.
 *
 * Video:
 *   TETSU  : 262-line, 5 MHz dot clock
 *   VDC0   : Mega Drive Plane B analogue (background/parallax)
 *   VDC1   : Mega Drive Plane A analogue + sprites/HUD
 *   KING/RAINBOW unused; KRAM page 1 holds 32 kHz ADPCM effects and music is
 *   Red Book CD-DA.
 *
 * Source geometry is not vertically centered. The MD V28 active picture starts
 * at the native raster position; the PC-FX BATs and metasprite anchors use the
 * extracted MD coordinates directly.
 */
#include <pcfx/types.h>
#include <pcfx/vdc.h>
#include <pcfx/tetsu.h>
#include <pcfx/contrlr.h>
#include "assets_generated.h"
#include "boot_exact.h"
#include "boot_dual.h"
#include "audio_stub.h"
#include "music.h"
#include "game_levels_generated.h"
/* Generated VRAM layout: see tools/vram_map.py, which owns every address
   below and rejects banks that would overlap while both are resident. */
#include "vram_map.h"

extern void pcfx_tetsu_wait_frame(void);

#define BACK_CHIP VDC0
#define FRONT_CHIP VDC1

#define FG_BAT_W 64
#define FG_BAT_H 32
#define FG_BAT_WORDS (FG_BAT_W * FG_BAT_H)
#define BG_BAT_W 64
#define BG_BAT_H 64
#define BG_BAT_WORDS (BG_BAT_W * BG_BAT_H)
#define TILE_WORDS 16
/* Black in the Y8U4V4 form tools/build_assets.py emits for RGB 0,0,0. */
#define YUV_BLACK 0x0088

#define BOSS_BG_TILE_BASE_WORD BG_TILE_BASE_WORD
#define BOSS_BG_TILE_BASE_INDEX (BOSS_BG_TILE_BASE_WORD/TILE_WORDS)
#define FG_TILE_BASE_INDEX (FG_TILE_BASE_WORD / TILE_WORDS)
#define BG_TILE_BASE_INDEX (BG_TILE_BASE_WORD / TILE_WORDS)
#define BOOT_TILE_BASE_WORD BG_TILE_BASE_WORD
#define BOOT_TILE_BASE_INDEX (BOOT_TILE_BASE_WORD / TILE_WORDS)

#define HUD_LIFE_BASE_WORD HUD_BASE_WORD
#define HUD_FIRE_BASE_WORD (HUD_BASE_WORD + 128)
/* The four 32x16 strips of the down-right wedge come first; its 16-wide
   remainder column follows them. */
#define LEVEL8_WEDGE_R_RIGHT_WORD (LEVEL8_WEDGE_R_BASE_WORD+512)
#define LEVEL8_OBJ_SPR_PAL 12
#define CONTINUE_TILE_BASE_INDEX (CONTINUE_TILE_BASE_WORD / TILE_WORDS)
#define CONTINUE_LUIGI_BASE_WORD (CONTINUE_PLAYER_BASE_WORD + 2 * PLAYER_FRAME_WORDS_LOCAL)
#define CONTINUE_PLAYER_SPR_PAL 14
#define CONTINUE_PLAYER_FRAME_TICKS 8

#define PLAYER_FRAME_WORDS_LOCAL 384
#define CONTINUE_PLAYER_FRAME_COUNT 2
#define PLAYER_STRIP_WORDS 128
#define BOOT_PLAYER_BANK_WORDS (PLAYER_FRAME_COUNT * PLAYER_FRAME_WORDS_LOCAL)
/* Bank overlap and 128-word sprite alignment are checked for every scene by
   tools/vram_map.py before src/vram_map.h is written; the hand-written #if
   guards this file used to carry only covered four of the banks. */
#define ENEMY_VARIANT_WORDS ENEMY_VARIANT_VRAM_WORDS
#define BOWSER_SPECIAL_FRAME_WORDS (7*6*64)
#define BOWSER_ANIM_BASE_WORD BOSS_BASE_WORD
#define BOWSER_SPECIAL_NORMAL 0
#define BOWSER_SPECIAL_FIRE 2
#define BOWSER_SPECIAL_TELEPORT 4
#define BOWSER_SPECIAL_RUN 7
#define BOWSER_SPECIAL_HURT 9
#define BOWSER_FIRE_FRAME_TICKS 7
#define BOWSER_TELEPORT_FRAME_TICKS 3
#define BOWSER_RUN_FRAME_TICKS 7
#define BOSS_RING_BASE_WORD BOSS_AUX_BASE_WORD
#define BOSS_HAMMER_BASE_WORD (BOSS_AUX_BASE_WORD+256)
#define BOSS_BELL_BASE_WORD (BOSS_AUX_BASE_WORD+768)
#define BOSS_BOMB_BASE_WORD (BOSS_AUX_BASE_WORD+1024)
#define BOSS_CRATE4_BASE_WORD (BOSS_AUX_BASE_WORD+1536)
#define BOSS13_PLATFORM_BASE_WORD (BOSS_AUX_BASE_WORD+1792)
#define BOSS_AUX_SPR_PAL 12
#define BOSS_ITEM_SPR_PAL 11
#define BOSS13_PLATFORM_SPR_PAL 10
#define BOSS_WATER_SPR_PAL 15
#define BOSS_SPR_PAL_AUX 9
#define BOSS_CROUCH_SPR_PAL 8
#define PLAYER_SPR_PAL 13
#define BOSS_SPR_PAL 14
#define BOSS_HURT_FRAMES 30
#define SPR_ORIGIN_X 0x20
#define SPR_ORIGIN_Y 0x40

#define SCREEN_W 256
#define FIX_SHIFT 8
#define FIX_ONE (1 << FIX_SHIFT)

/* Player object anchor matches the MD object/metasprite coordinate system.
 * Standard Mario metasprite 17 begins at anchor Y + 8. The collision body is
 * narrower and begins at anchor + (8,16), which puts the first-stage standing
 * body on collision row 24 (y=192) exactly. */
#define PLAYER_BODY_XOFF 8
#define PLAYER_BODY_YOFF 16
#define PLAYER_BODY_W 16
#define PLAYER_BODY_H 32

#define MAX_CRATES 48
#define MAX_GEMS 72
#define MAX_ENEMIES 32
#define MAX_FIREBALLS 3
/* Items freed from crates go into the source's four-slot $600..$660 pool. */
#define MAX_ITEMS 4
#define ITEM_W 16
#define ITEM_H 16
/* Level 8 streams 37 carrier records across its route; the engine has no
   trigger-driven spawner, so every record is instantiated at level load and
   simulated only while it is near the camera. */
/* Level 9 fills 55 of these on its own now that its five $B31E boulders join
   the $10B2A records, so the pool keeps a little headroom above that. */
#define MAX_MOVING_PLATFORMS 64
#define MOVING_PLATFORM_W 48
#define MOVING_PLATFORM_H 16

/* Level-8 carrier kinds, named after the $F4FA handler each record selects.
   0 is the vertical 48x8 beam used by levels 2/6 and the $8CEC boss room. */
#define PLATFORM_KIND_LIFT      0   /* handler 1  */
#define PLATFORM_KIND_BRICK     1   /* handler 16: 48x16, bobs +/-32 */
#define PLATFORM_KIND_WEDGE_R   2   /* handler 14: 48x64, slides down-right */
#define PLATFORM_KIND_WEDGE_L   3   /* handler 15: 32x48, slides down-left */
/* Records from the generic $10B2A tables, drawn out of the per-level stage
   object bank.  build_full_game.py's STAGE_OBJECT_KINDS assigns these. */
#define PLATFORM_KIND_STATIC    4   /* handler 0  */
#define PLATFORM_KIND_SHUTTLE   5   /* handler 3  */
#define PLATFORM_KIND_FALLER    6   /* handler 4  */
#define PLATFORM_KIND_DESCEND   7   /* handler 5  */
#define PLATFORM_KIND_ASCEND    8   /* handler 6  */
#define PLATFORM_KIND_PATH      9   /* handler 7  */
#define PLATFORM_KIND_DIAMOND  10   /* handler 11 */
#define PLATFORM_KIND_SINKER   11   /* handler 1: the level 2/6 lift elsewhere */
#define PLATFORM_KIND_BOULDER  12   /* $B086, from the $B31E table */
#define PLATFORM_KIND_IS_STAGE_OBJECT(k) ((k)>=PLATFORM_KIND_STATIC)
/* $F934 wraps a descending carrier from y 0x1C2 back to 0x6E and $F980 wraps a
   climbing one from 0x64 back to 0x17C, in the object space the records store,
   which sits 128 lines below the playfield. */
#define PLATFORM_WRAP_BOTTOM (0x1C2 - 128)
#define PLATFORM_WRAP_TOP    (0x6E - 128)
#define PLATFORM_WRAP_CEIL   (0x64 - 128)
#define PLATFORM_WRAP_REBASE (0x17C - 128)
/* $B086's boulder falls 8px a frame from 0x64 until it reaches 0x128, bounces
   through the arc at $B1A6, and recycles at 0x190 back to 0x64 and 23px right,
   in the same object space. */
#define BOULDER_TOP_Y   (0x64 - 128)
#define BOULDER_LAND_Y  (0x128 - 128)
#define BOULDER_WRAP_Y  (0x190 - 128)
#define PLATFORM_STATE_IDLE  0
#define PLATFORM_STATE_SLIDE 1
#define PLATFORM_STATE_FALL  2
#define PLAYER_MAX_HEARTS 5
#define HURT_FRAMES 60
/* $167B8 flickers the player every fourth frame and counts to 15 for an
   ordinary hit but to 127 for the shield item, so the shield lasts 508
   frames against the hurt state's 60. */
#define SHIELD_FRAMES 508
/* The stopwatch writes $150 to FF00A6, which $B706 tests to skip the whole
   enemy behavior dispatch until it counts back down. */
#define STOPWATCH_FRAMES 0x150
#define HURT_FLICKER_HALF_PERIOD 6

/* MODE1 toggles this at runtime.  It deliberately lives in the release build:
   the requested debug mode is a testing aid, not a forced gameplay rule. */
#define DEBUG_AMMO_DISPLAY 9

#define STATE_BOOT       0
#define STATE_TITLE_FADE 1
#define STATE_MENU       2
#define STATE_MENU_WALK  3
#define STATE_PLAY       4
#define STATE_STAGE_LOAD 5
#define STATE_CONTINUE_LOAD 6
#define STATE_CONTINUE   7
#define STATE_CONTINUE_FADE 8
#define STATE_LEVEL_FADE 9
#define STATE_STAGE_PREP 10

#define PMODE_NORMAL     0
#define PMODE_DYING      1
#define PMODE_FADE_OUT   2
#define PMODE_FADE_IN    3

#define CRATE_W 24
#define CRATE_H 24
#define GEM_W 16
#define GEM_H 16

#define MENU_1P 0
#define MENU_2P 1

typedef struct {
    s32 x, y, vx, vy;
    unsigned char active;
    unsigned char carried;
    unsigned char thrown;
    unsigned char extra;
    unsigned char flight_timer;
} Crate;

typedef struct {
    short x, y;
    unsigned char active;
} Gem;

typedef struct {
    s32 x, y, vx, vy;
    unsigned char active;
    unsigned char spawned;
    unsigned char variant;
    unsigned char width;
    unsigned char height;
    unsigned char state;
    unsigned char kind;
    unsigned char flags;
    unsigned char defeated;
    unsigned char flying;
    unsigned char flight_phase;
    unsigned char behavior_phase;
    unsigned char behavior_timer;
    unsigned char face_right;
    /* Slot+1 of the projectile this enemy currently owns, 0 when it has none.
       $C25C/$BB98 both drive their throw phase off "is my next object slot
       still occupied", which is what this reproduces. */
    unsigned char shot_slot;
} Enemy;

typedef struct {
    s32 x, y, vx;
    unsigned char active;
    unsigned char life;
} Fireball;

/* The hidden crate contents.  A crate record's tenth byte carries one of these
   ids; throwing the crate frees the item, which then sits where the crate was
   until the player walks into it.  Numbered as the source's collect handler
   indexes its jump table at $15B96. */
#define ITEM_FULL_HEAL   1
#define ITEM_FIRST_AID   2
#define ITEM_POISON      3
#define ITEM_EXTRA_LIFE  4
#define ITEM_STOPWATCH   5
#define ITEM_SHIELD      6
#define ITEM_FIREBALL    7

typedef struct {
    s32 x, y, vy;
    unsigned char active;
    unsigned char kind;      /* ITEM_*, 1..ITEM_KIND_COUNT */
    unsigned char grounded;  /* cleared while the item is still dropping */
} Item;

/* mode 0 is the monkey's thrown banana, which arcs under the normal object
   gravity.  Mode 1 is $C25C's rock: its horizontal speed walks the $C360
   deceleration table while $C2A8 adds a flat two pixels of drop.  Mode 2 is
   $BB98's spear, which only ever changes X. */
#define SHOT_MODE_ARC   0
#define SHOT_MODE_ROCK  1
#define SHOT_MODE_SPEAR 2
typedef struct { s32 x,y,vx,vy; u16 base; unsigned char active,palette,cols,mode,step; } EnemyShot;

typedef struct {
    s32 x, y;
    short min_y, max_y;
    signed char dir;
    unsigned char speed;
    /* Level-8 carriers ($11120) additionally keep their record position so a
       consumed wedge is restored once it has left the camera, exactly as the
       Mega Drive respawns the object on the next tile trigger. */
    s32 home_x, home_y;
    unsigned char kind, state, timer;
    /* Index into the level's stage object shape table, and the step counter
       handler 7 uses to walk its 80-entry path. */
    unsigned char shape, step;
} MovingPlatform;

/* Exact Level-2 carrier metasprite 741 ($01D956): tiles 491..496 form a
   48x8 riveted beam on source palette line 0. It is stored as three 16x16
   VDC cells; the unused lower eight rows remain transparent. */
static const u16 g_level2_platform_words[192] = {
    0xFFFF,0x0000,0xC3C3,0x8181,0x8181,0xC3C3,0xFFFF,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0x0000,0xC3C3,0x8181,0x8181,0xC3C3,0xFFFF,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0x0000,0xC3C3,0x8181,0x8181,0xC3C3,0xFFFF,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0xFFFF,0xC3C3,0xC3C3,0xC3C3,0xFFFF,0xFFFF,0xFFFF,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
};

static int g_state;
static unsigned g_state_frame;
static u16 g_prev_buttons;
static int g_menu_choice;
static int g_prompt_visible;

static s32 g_player_x, g_player_y;
static s32 g_vx, g_vy;
static int g_grounded;
static int g_facing; /* -1 left, +1 right */
static int g_camera_x;
static int g_camera_target_x;
static int g_camera_tile;
/* Route steps whose mode is 2 or 3 stack the next screen above or below the
   current one instead of beside it ($145E2 drives VSRAM instead of HSCROLL).
   Levels 11..12, 15, 16 and 19 of the source use them, so the stage is a path
   through a 2-D grid of screens, not a single horizontal ribbon. */
static int g_camera_y;
static int g_camera_target_y;
static int g_camera_row;   /* world tile row at the top of the streamed window */
static int g_level_rows=1; /* stacked screens; 1 for a purely horizontal level */
static int g_level_cols;
static int g_satb_page;
static int g_player_anim_clock;
static int g_player_down;
static int g_lift_timer;
static int g_carry_index;
static int g_gem_count;
static int g_invuln;
static int g_player_hearts;
static int g_lives; /* reserve lives shown after the source HUD multiplication sign */
static int g_continues; /* continue credits; independent of the HUD life reserve */
static int g_hurt_pending_death;
static int g_jump_active;
static int g_jump_index;
static int g_bounce_active;
static int g_bounce_index;
static int g_player_mode;
static int g_death_timer;
static int g_game_fade_level;
static s32 g_menu_mario_x;
static int g_menu_sprite_fade=16;
static int g_throw_timer;
static int g_camera_hold;
static int g_prev_body_y;
static unsigned g_boot_dual_load_pos;
static u16 g_title_exact_palette[256];

static int g_fireball_ammo;
static int g_continue_digit;
static int g_continue_resume;
static int g_current_level=0;
static int g_level_width_tiles=416;
static const GameLevelData *g_level;
static const StageSetData *g_stage_set;
static unsigned char g_enemy_palette_line[16];
/* Enemy variants no longer share one cell grid: each carries the cols x rows
   its widest pose needs, so the per-level banks are packed rather than
   strided.  upload_game_sprites() records where each one landed. */
static u16 g_enemy_vram[16],g_enemy_hit_vram[16],g_enemy_alt_vram[16];
/* Attack poses share the ENEMY_ALT arena: only two variants ever own a third
   live cel, so the rest of that bank is free for the state-1 script pose the
   throwing handlers select. */
static u16 g_enemy_atk_vram[16],g_enemy_atk2_vram[16],g_enemy_shot_vram[16];
/* $D244/$D3F8 cel durations: 15 ticks of wind-up, then 7 on the release cel. */
#define THROWER_WINDUP_TICKS  15
#define THROWER_RELEASE_TICKS  7
/* Size of the ENEMY_ALT and ENEMY_SHOT banks in tools/vram_map.py. */
#define ENEMY_ALT_WORDS 2304
#define ENEMY_SHOT_WORDS 256
#define enemy_frame_words(v) ((int)(v)->cols*(int)(v)->rows*64)
static int g_in_boss=0;
static int g_after_boss=0;
static s32 g_boss_x,g_boss_y,g_boss_vx,g_boss_vy;
static int g_boss_hp,g_boss_timer,g_boss_invuln,g_boss_defeated;
static int g_boss_class,g_boss_phase,g_boss_face=1,g_boss_ground_y;
static int g_boss_action,g_boss_action_timer,g_boss_action_dir;
/* The storey the ostrich belongs to: its Y while it is not mid-hop. */
static int g_boss10_floor_y;
/* Bowser's script cursor is not itself the Mega Drive action/state number.
   Each six-byte record selected by $6F3A supplies the action separately. */
static const unsigned char g_bowser_action_script[12]={1,5,2,6,3,4,1,5,2,5,3,4};
static const unsigned char g_bowser_direction_script[12]={0,1,1,1,0,1,0,1,1,0,0,1};
static int g_boss_script_index;
static int g_boss8_crate=-1;
static s32 g_boss_shot_x,g_boss_shot_y,g_boss_shot_vx,g_boss_shot_vy;
static int g_boss_shot_active;
static unsigned g_boss_drop_rng;
static int g_boss_random_drop_timer;

/* Boss 3's handler at MD $806A is a byte-pair action script.  The first byte
   selects the movement state and the second is the source direction flag.
   Keep the recovered data here instead of approximating it with a velocity
   pattern; the direction flag is also the value used by the original sprite
   selector. */
/* Boss 3 room geometry, in room space.  X is the source object coordinate
   minus $A0 (the ordinary -$80 room offset plus the pig metasprite's -32
   bbox origin); Y keeps the plain -$80 offset because the port's boss room
   is not the full-height MD plane, less a further 32 that lifts the pig to
   the height it flies at in the source.  $A8/$158 -> 8/184, $C8/$130 -> 40/144. */
#define BOSS8_LEFT_X   8
#define BOSS8_RIGHT_X  184
#define BOSS8_TOP_Y    40
#define BOSS8_BOTTOM_Y 144
/* $827C refuses to spawn a drop below $F0; $82DA splashes it at $15E.
   $F0 carries the same -32 lift as the anchored positions above. */
#define BOSS8_DROP_MAX_Y   80
#define BOSS8_DROP_END_Y   222

static const unsigned char g_boss8_arc[45]={
    1,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,
    3,4,4,4,4,4,4,4,4,4,4,3,3,3,3,3,
    3,2,2,2,2,2,1,1,1,1,1,1,0x88
};
static const unsigned char g_boss8_script[]={
    1,0, 8,1, 2,1, 8,0, 1,0, 2,1, 1,0, 8,1,
    2,1, 8,0, 3,0, 3,1, 1,0, 2,1, 8,0, 1,0,
    2,1, 8,0, 1,0, 4,1, 4,0, 2,1, 1,0, 2,1,
    8,0, 6,0, 4,0, 8,1, 4,1, 1,0, 2,1, 1,0,
    3,1, 8,0, 1,0, 8,0, 2,1, 1,0, 7,0, 8,0,
    3,0, 3,1, 1,0, 8,1, 2,1, 1,0, 7,0, 8,0,
    3,0, 2,1, 1,0, 2,1, 1,0, 2,1, 4,0, 8,1,
    2,1, 6,0, 4,0, 7,0, 8,0, 1,0, 2,1, 1,0,
    8,1, 2,1, 0x88
};

/* Boss 4's handler at MD $8764 is the same shape: the byte-pair script at
   $8C61 names one of the eleven move handlers in the $8CC0 jump table and
   supplies the source direction flag.  Move 9 is the death state and 10 the
   victory state; neither appears in the script, they are forced by the HP
   check at $87D2, which the port's own defeat path already covers. */
static const unsigned char g_boss10_script[]={
     1,0, 11,1,  1,1,  3,0,  6,1, 11,1,  8,0,  3,0,
     7,0,  8,1,  2,1, 11,1,  2,0,  4,1,  4,1,  1,1,
    11,1,  5,1, 11,0,  1,1, 11,1,  2,0,  4,1,  7,0,
     3,0,  6,1, 11,1,  4,1,  7,0,  1,1, 11,1,  8,1,
     1,1, 11,1,  8,0,  2,0, 11,0,  4,1,  7,0,  3,0,
     6,1, 11,1,  3,0,  3,0,  5,0,  1,1, 11,1, 0x88
};
/* $8BC8: the acceleration ramp every charge indexes with its own frame
   counter.  The source runs off the end of the table into the hop arc below
   for a charge that lasts more than 64 frames; none can, because both ends
   of the room are reached first, so the port clamps on the saturated 10. */
static const unsigned char g_boss10_ramp[64]={
    0x01,0x01,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,0x04,0x04,0x04,0x04,
    0x05,0x05,0x05,0x05,0x06,0x06,0x06,0x06,0x06,0x07,0x07,0x07,0x07,0x07,0x08,0x08,
    0x08,0x08,0x08,0x08,0x08,0x09,0x09,0x09,0x09,0x09,0x09,0x0A,0x0A,0x0A,0x0A,0x0A,
    0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A
};
/* $8C1B: the hop arc, applied to Y one entry a frame. */
static const unsigned char g_boss10_hop[39]={
    0xFA,0xFA,0xFB,0xFB,0xFB,0xFC,0xFC,0xFC,0xFC,0xFD,0xFD,0xFD,0xFD,
    0xFE,0xFE,0xFE,0xFE,0xFF,0xFF,0x01,0x01,0x02,0x02,0x02,0x02,0x03,
    0x03,0x03,0x03,0x04,0x04,0x04,0x04,0x05,0x05,0x05,0x06,0x06,0x88
};
/* $8C42: the arc the spat pot follows before $8B58 hands it to the room's
   object slot and it becomes an ordinary throwable. */
static const unsigned char g_boss10_pot[31]={
    0xFE,0xFE,0xFE,0xFE,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x01,
    0x01,0x01,0x01,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x88
};

/* $B844's three bounce arcs, read one signed word a frame while the rock
   rolls two pixels right.  The first is the same 45-step table $17948 gives
   the player's jump; the port's copy of that one carries a deliberate change
   for Mario's clearance, so the rock keeps its own. */
static const signed char g_boulder_bounce1[45]={
    -6,-6,-5,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
    -2,-2,-2,-2,-2,-2,-2,0,0,0,2,2,2,2,2,2,2,
    4,4,4,4,4,4,4,4,4,4,4,5,6,6
};
static const signed char g_boulder_bounce2[32]={
    -4,-4,-3,-2,-2,-2,-2,-2,-2,-1,-1,-1,-1,-1,0,0,
    1,1,1,1,1,2,2,2,2,2,2,2,3,4,4,4
};
static const signed char g_boulder_bounce3[19]={
    -3,-3,-2,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,2,3,3
};

/* $B1A6, the arc $B086's stage-object boulder follows after its first landing:
   one signed word a frame, a short hop and then a long fall until the object
   passes 0x190 and is recycled above the ceiling. */
#define BOULDER_ARC_LEN 77
static const signed char g_boulder_arc[BOULDER_ARC_LEN]={
    -4,-4,-3,-2,-2,-2,-2,-2,-2,-1,-1,-1,-1,-1,0,0,
    1,1,1,1,1,2,2,2,2,2,2,2,3,4,4,4,4,4,4,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
};

/* $C6E0: the leap of the water fish, one signed word a frame for the fifty-one
   steps $C62C counts off.  It is symmetric, so the fish returns to the exact
   surface line it left. */
static const signed char g_fish_leap_dy[51]={
    -8,-8,-8,-7,-7,-7,-6,-6,-6,-5,-5,-5,-4,-4,-3,-3,-3,
    -3,-2,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,2,3,3,3,3,
    4,4,5,5,5,6,6,6,7,7,7,8,8,8
};
#define FISH_LEAP_STEPS 51
/* $C630 gates the handler on a sixteen-tick counter before the first step, and
   counts the same sixteen again after the splash before the slot is freed. */
#define FISH_LEAP_DELAY 16
/* $D4D4 re-creates the fish while the camera stands in the one-to-three-tile
   window its record names, which places the fish 136..160 pixels right of the
   camera -- 24..48 ahead of Mario, who rides 112 into the raster. */
#define FISH_LEAP_RANGE 64
/* State 0 rests on the second cel after showing the first for four ticks. */
#define FISH_IDLE_CEL0_TICKS 4

/* $8764's object coordinates, in room space.  The ostrich metasprite (IDs
   588-591) is anchored bottom-centre with bbox x0 = -40, and the supplied
   strip puts its art at canvas x 8 and bottom-aligns every pose on canvas
   row 107, so room_x = md_x - $B0 and room_y = md_y - $EC.  The check on
   that pair is the handler's own centre: $100 lands the 80-pixel body on the
   middle of the 256-pixel room, and $B0/$150 come out symmetric about it. */
#define BOSS10_X(md) ((md)-176)
#define BOSS10_Y(md) ((md)-236)
#define BOSS10_MIN_X      BOSS10_X(0x28)    /* charge target, fully off left  */
#define BOSS10_MAX_X      BOSS10_X(0x1CC)   /* charge target, fully off right */
#define BOSS10_MID_X      BOSS10_X(0x100)   /* where the walk-on stops        */
#define BOSS10_RIGHT_STOP BOSS10_X(0x150)
#define BOSS10_LEFT_STOP  BOSS10_X(0xB0)
#define BOSS10_AIR_Y      BOSS10_Y(0xC8)
#define BOSS10_GROUND_Y   BOSS10_Y(0x110)
/* Room 2's collision grid holds a one-way platform on row 9 (y=72..79) and
   the arena floor on row 18 (y=144), so the arena is two storeys tall. */
#define BOSS10_PLATFORM_TOP    72
#define BOSS10_PLATFORM_BOTTOM 80

static Crate g_crates[MAX_CRATES];
static Gem g_gems[MAX_GEMS];
static Enemy g_enemies[MAX_ENEMIES];
static EnemyShot g_enemy_shots[4];

static int level_has_enemy_kind(unsigned char kind)
{
    int i;
    if (!g_level) return 0;
    for (i=0;i<g_level->enemy_count;++i)
        if (g_level->enemies[i*ENEMY_RECORD_WORDS+7]==kind) return 1;
    return 0;
}
static Fireball g_fireballs[MAX_FIREBALLS];
static Item g_items[MAX_ITEMS];
/* Frames left on the stopwatch item, mirroring FF00A6. */
static int g_enemy_freeze;
static MovingPlatform g_moving_platforms[MAX_MOVING_PLATFORMS];
static int g_moving_platform_count;
static int g_on_moving_platform=-1;
static int g_drop_platform_timer;
static const StageObjectData *stage_objects(void);
static int throw_enemy_shot(Enemy *e, int yoff, int xoff, int speed, int mode);
static int platform_width(const MovingPlatform *p);
static int platform_height(const MovingPlatform *p);
static int platform_draw_x(const MovingPlatform *p);
static int platform_draw_y(const MovingPlatform *p);
static int g_debug_mode;
static int g_crate_hide;
static int g_paused;

static void enter_menu(void);
static void enter_boot(void);
static void start_boss(int fade_in);

/* Boss 3 (level 8 slot).  $80D8 selects animation $B=5 for every action from
   6 up, so the two spin paths (6/7) and the crate drop (8) all play the spin
   strip; only the patrol actions below 6 use the walk poses. */
static int boss8_spinning(void)
{
    return g_boss_class==1 && g_boss_action>=6;
}

static void submit_front_satb(u16 *satb);
static void clear_front_sprites(void);
static void hurt_player(void);
static void set_front_sprites_enabled(int enabled);
static void set_sprite_entry(u16 *satb, int index, int x, int y, u16 addr, u16 ctrl);

static void wait_frame(void)
{
    /* The HuC6261 can return a transitional raster value on a single read.
       Use the supplied double-read assembly wait and return exactly at the
       262-line frame wrap so every BAT/SATB/palette update starts in VBlank. */
    pcfx_tetsu_wait_frame();
}


static u16 read_buttons(void)
{
    return (u16)(contrlr_pad_read(0) & 0xFFFFu);
}

static int iabs_i(int v) { return v < 0 ? -v : v; }

static int aabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void clear_satb(void)
{
    int i;
    vdc_set_vram_write(FRONT_CHIP, SATB_BASE0);
    for (i = 0; i < 0x200; ++i) vdc_vram_write(FRONT_CHIP, 0);
    vdc_set_vram_write(FRONT_CHIP, SATB_BASE1);
    for (i = 0; i < 0x200; ++i) vdc_vram_write(FRONT_CHIP, 0);
    vdc_set_satb_address(FRONT_CHIP, SATB_BASE0);
    g_satb_page = 0;
}

static void clear_bat(int chip, int words)
{
    int i;
    vdc_set_vram_write(chip, 0);
    for (i = 0; i < words; ++i) vdc_vram_write(chip, 0);
}

/* Only the first 2048 words of each VDC hold a BAT; the tile patterns start
   at $0800.  A 64x64 virtual map therefore aliases pattern data into map rows
   32..63, so a plane that has to scroll vertically must be told it is 64x32
   and wrap every 256 pixels instead. */
static void set_map_height(int chip,int rows32)
{
    vdc_setreg(chip,VDC_REG_MWR,
               (u16)(rows32?VDC_MWR_SCREEN_64x32:VDC_MWR_SCREEN_64x64));
}

static void show_black(void)
{
    int i;
    /* Hide both VDC background planes before clearing their BATs.  This is
       used when returning to the title from gameplay, so clearing visible
       BATs in place can otherwise expose a partially-written frame. */
    vdc_setreg(BACK_CHIP, VDC_REG_CR, 0);
    vdc_setreg(FRONT_CHIP, VDC_REG_CR, 0);
    clear_bat(BACK_CHIP, BG_BAT_WORDS);
    clear_bat(FRONT_CHIP, FG_BAT_WORDS);
    set_map_height(BACK_CHIP,0); set_map_height(FRONT_CHIP,0);
    for (i = 0; i < 256; ++i) tetsu_set_palette((u16)i, 0x0088);
    vdc_set_scroll(BACK_CHIP, 0, 0);
    vdc_set_scroll(FRONT_CHIP, 0, 0);
    clear_satb();
}

static void init_video(void)
{
    vdc_init_5MHz(BACK_CHIP);
    vdc_init_5MHz(FRONT_CHIP);
    vdc_setreg(BACK_CHIP, VDC_REG_MWR, VDC_MWR_SCREEN_64x64);
    vdc_setreg(FRONT_CHIP, VDC_REG_MWR, VDC_MWR_SCREEN_64x64);
    vdc_setreg(BACK_CHIP, VDC_REG_CR, VDC_CR_BB);
    vdc_setreg(FRONT_CHIP, VDC_REG_CR, (u16)(VDC_CR_BB | VDC_CR_SB));
    vdc_set_scroll(BACK_CHIP, 0, 0);
    vdc_set_scroll(FRONT_CHIP, 0, 0);
    clear_bat(BACK_CHIP, BG_BAT_WORDS);
    clear_bat(FRONT_CHIP, FG_BAT_WORDS);

    tetsu_init();
    /* libpcfx's VDC sprite-offset write is unreliable in this build.
       Keep both bases at zero and select explicit, otherwise-unused palette
       lines in each sprite SAT attribute. */
    tetsu_set_vdc_palette(0, 0);
    tetsu_set_king_palette(0, 0, 0, 0);
    tetsu_set_rainbow_palette(0);
    tetsu_set_priorities(1, 2, 0, 0, 0, 0, 0);
    tetsu_set_chroma_key(0, 1, 0, 1, 0, 1);
    tetsu_set_video_mode(TETSU_LINES_262, 0, TETSU_DOTCLOCK_5MHz,
                         TETSU_COLORS_16, TETSU_COLORS_16,
                         1, 1, 0, 0, 0, 0, 0);
    clear_satb();
}

static u16 fade_yuv(u16 c, int level)
{
    int y=(c>>8)&255, u=(c>>4)&15, v=c&15;
    if (level < 0) level=0;
    if (level > 16) level=16;
    y=(y*level)/16;
    u=8+((u-8)*level)/16;
    v=8+((v-8)*level)/16;
    return (u16)((y<<8)|((u&15)<<4)|(v&15));
}


static void set_asset_palette_fade(const BgAsset *a, int level)
{
    int i;
    for (i=0;i<256;++i) tetsu_set_palette((u16)i,fade_yuv(a->palette[i],level));
}

static void set_title_exact_palette_fade(int level)
{
    int i;
    for (i=0;i<256;++i)
        tetsu_set_palette((u16)i,fade_yuv(g_title_exact_palette[i],level));
}

static void upload_tiles(int chip, u16 base, const BgAsset *a)
{
    unsigned i, n = (unsigned)a->tile_count * TILE_WORDS;
    vdc_set_vram_write(chip, base);
    for (i = 0; i < n; ++i) vdc_vram_write(chip, a->tile_words[i]);
}

static u16 relocate_bat(u16 e, u16 base_index)
{
    return (u16)((e & 0xF000u) | ((e & 0x0FFFu) + base_index));
}


static void load_menu_asset(void)
{
    int x,y;
    /* By the time this runs the title has faded to black, so the title-only
       CHR region can be reused without exposing a transient tile mismatch.
       CHR and the matching BAT rows 32..61 must be written together: uploading
       one without the other leaves the menu interpreting stale title cells. */
    upload_tiles(BACK_CHIP,FRONT_TILE_BASE_WORD,&g_menu_combo_asset);
    for(y=0;y<30;++y){
        vdc_set_vram_write(BACK_CHIP,(u16)((32+y)*BG_BAT_W));
        for(x=0;x<32;++x){
            u16 e=g_menu_combo_asset.bat[y*g_menu_combo_asset.width_tiles+x];
            vdc_vram_write(BACK_CHIP,relocate_bat(e,(u16)(FRONT_TILE_BASE_WORD/TILE_WORDS)));
        }
    }
}



static void boot_exact_setup(void)
{
    int y,x;
    clear_bat(BACK_CHIP,BG_BAT_WORDS);
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    set_map_height(BACK_CHIP,0); set_map_height(FRONT_CHIP,0);
    for (y=0;y<30;++y) {
        vdc_set_vram_write(BACK_CHIP,(u16)(y*BG_BAT_W));
        for (x=0;x<32;++x) {
            int ci=y*32+x;
            vdc_vram_write(BACK_CHIP,(u16)((g_boot_exact_cell_palette[ci]<<12)|(BOOT_TILE_BASE_INDEX+ci)));
        }
    }
    vdc_set_scroll(BACK_CHIP,0,0);
    vdc_set_scroll(FRONT_CHIP,0,0);
    vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_front_sprites();
}

static void set_boot_visible(int chip)
{
    /* The exact full-screen transition uses the two VDC backgrounds as a
       double buffer.  Only one VDC background is visible at a time. */
    if (chip==BACK_CHIP) {
        vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
        vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    } else if (chip==FRONT_CHIP) {
        vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
        vdc_setreg(FRONT_CHIP,VDC_REG_CR,VDC_CR_BB);
    } else {
        vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
        vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    }
}

static void boot_dual_preload_step(unsigned source_frame)
{
    const unsigned total=(unsigned)BOOT_DUAL_BACK_TILE_WORDS+
                         (unsigned)BOOT_DUAL_BACK_PRE_WORDS;
    unsigned left,pos,n,i;
    if (source_frame<30 || g_boot_dual_load_pos>=total) return;
    left=128;
    while (left && g_boot_dual_load_pos<total) {
        pos=g_boot_dual_load_pos;
        if (pos<BOOT_DUAL_BACK_TILE_WORDS) {
            n=BOOT_DUAL_BACK_TILE_WORDS-pos; if (n>left) n=left;
            vdc_set_vram_write(BACK_CHIP,(u16)(BOOT_DUAL_BACK_BASE_WORD+pos));
            for (i=0;i<n;++i) vdc_vram_write(BACK_CHIP,g_boot_dual_back_tiles[pos+i]);
        } else {
            pos-=BOOT_DUAL_BACK_TILE_WORDS;
            n=BOOT_DUAL_BACK_PRE_WORDS-pos; if (n>left) n=left;
            for (i=0;i<n;++i) {
                unsigned ci=pos+i;
                vdc_set_vram_write(BACK_CHIP,(u16)((32+(ci>>5))*BG_BAT_W+(ci&31)));
                vdc_vram_write(BACK_CHIP,g_boot_dual_back_pre[ci]);
            }
        }
        g_boot_dual_load_pos+=n; left-=n;
    }
}

static void boot_apply_title_delta(unsigned source_frame)
{
    unsigned index=source_frame-434;
    unsigned p=g_boot_dual_title_offsets[index],end=g_boot_dual_title_offsets[index+1];
    while (p<end) {
        unsigned cell=g_boot_dual_title_stream[p++];
        u16 value=g_boot_dual_title_stream[p++];
        vdc_set_vram_write(BACK_CHIP,(u16)((32+(cell>>5))*BG_BAT_W+(cell&31)));
        vdc_vram_write(BACK_CHIP,value);
    }
}

static void boot_dual_apply(unsigned source_frame)
{
    if (source_frame==433) {
        /* ADR_9CE advances one resident tilemap.  Reveal the prebuilt lower
           half in one scroll write, then mutate only its BAT cells. */
        vdc_set_scroll(BACK_CHIP,0,256);
        set_boot_visible(BACK_CHIP);
        clear_front_sprites();
    } else if (source_frame<=600) {
        boot_apply_title_delta(source_frame);
    }
}

static void draw_boot_motion(unsigned f);

static void boot_exact_apply(unsigned frame)
{
    const unsigned short *p;
    unsigned i,n,source_frame;
    if (frame>=BOOT_EXACT_FRAME_COUNT) return;
    source_frame=frame+BOOT_EXACT_FIRST_MD_FRAME;
    if (source_frame<433) boot_dual_preload_step(source_frame);
    p=&g_boot_exact_stream[g_boot_exact_offsets[frame]];
    n=*p++;
    for (i=0;i<n;++i) {
        u16 pi=*p++, value=*p++;
        tetsu_set_palette(pi,value);
        g_title_exact_palette[pi]=value;
    }
    n=*p++;
    if (source_frame>=433) {
        boot_dual_apply(source_frame);
        return;
    }
    for (i=0;i<n;++i) {
        u16 cell=*p++;
        int k;
        vdc_set_vram_write(BACK_CHIP,(u16)(BOOT_TILE_BASE_WORD+cell*TILE_WORDS));
        for (k=0;k<TILE_WORDS;++k) vdc_vram_write(BACK_CHIP,*p++);
    }
    draw_boot_motion(source_frame);
}

static void apply_bat_pairs(int chip,const unsigned short *stream,unsigned words,int row_base)
{
    unsigned p=0;
    int width=(chip==BACK_CHIP)?BG_BAT_W:FG_BAT_W;
    while (p<words) {
        unsigned cell=stream[p++]; u16 value=stream[p++];
        vdc_set_vram_write(chip,(u16)((row_base+(cell>>5))*width+(cell&31)));
        vdc_vram_write(chip,value);
    }
}

static void set_baked_title_prompt(int visible)
{
    /* Source frame 600 is the settled no-PUSH title, so the first transition
       out of the initial state is always blink-on. */
    if (g_prompt_visible==visible) return;
    if (visible) apply_bat_pairs(BACK_CHIP,g_boot_dual_blink_on,BOOT_DUAL_BLINK_ON_WORDS,32);
    else if (g_prompt_visible>0) apply_bat_pairs(BACK_CHIP,g_boot_dual_blink_off,BOOT_DUAL_BLINK_OFF_WORDS,32);
    g_prompt_visible=visible;
}



static void set_sprite_entry(u16 *satb, int index, int x, int y, u16 addr, u16 ctrl)
{
    u16 *e;
    if (index < 0 || index >= 64) return;
    e = satb + index * 4;
    e[0] = (u16)(y + SPR_ORIGIN_Y);
    e[1] = (u16)(x + SPR_ORIGIN_X);
    e[2] = VDC_SPR_PATTERN(addr);
    e[3] = ctrl;
}

static void upload_front_sprites(void)
{
    int i;
    /* Opening animation uses dedicated, non-overlapping character banks.
       Gameplay/death graphics are uploaded later by upload_game_sprites(). */
    vdc_set_vram_write(FRONT_CHIP,BOOT_MARIO_BASE_WORD);
    for (i=0;i<PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL;++i)
        vdc_vram_write(FRONT_CHIP,g_player_frame_words[i]);
    vdc_set_vram_write(FRONT_CHIP,BOOT_LUIGI_BASE_WORD);
    for (i=0;i<PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL;++i)
        vdc_vram_write(FRONT_CHIP,g_luigi_frame_words[i]);
    vdc_set_vram_write(FRONT_CHIP,PUSH_BASE_WORD);
    for (i=0;i<384;++i) vdc_vram_write(FRONT_CHIP,g_push_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,STAR_BASE_WORD);
    for (i=0;i<128;++i) vdc_vram_write(FRONT_CHIP,g_menu_star_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,COIN_BASE_WORD);
    for (i=0;i<4096;++i) vdc_vram_write(FRONT_CHIP,g_coin_sprite_words[i]);
    /* Keep menu characters resident at a non-conflicting high VRAM address.
       This avoids changing tile/BAT layouts during the title->menu fade. */
    vdc_set_vram_write(FRONT_CHIP,MENU_MARIO_BASE_WORD);
    for (i=0;i<1536;++i) vdc_vram_write(FRONT_CHIP,g_menu_mario_words[i]);
    vdc_set_vram_write(FRONT_CHIP,MENU_LUIGI_BASE_WORD);
    for (i=0;i<384;++i) vdc_vram_write(FRONT_CHIP,g_menu_luigi_words[i]);
}

static int palettes_equal(const unsigned short *a, const unsigned short *b)
{
    int i;
    for (i=0;i<16;++i) if (a[i]!=b[i]) return 0;
    return 1;
}

static void upload_game_sprites(void)
{
    int i,j,k,uniq=0,walk_top=0,hit_top=0,alt_top=0,shot_top=0;
    const unsigned short *seen[12];
    /* Keep a dedicated copy of the player palette for boss rooms. Normal
       stages use source palette line 0 directly; every generated level carries
       the same Mario/object colors there. */
    for (i=0;i<16;++i) tetsu_set_palette((u16)(PLAYER_SPR_PAL*16+i),g_player_palette[i]);
    for (i=0;i<16;++i) tetsu_set_palette((u16)(CONTINUE_PLAYER_SPR_PAL*16+i),g_continue_player_palette[i]);
    if(g_in_boss)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_CROUCH_SPR_PAL*16+i),g_continue_player_palette[i]);

    vdc_set_vram_write(FRONT_CHIP, PLAYER_BASE_WORD);
    for (i=0;i<PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL;++i) vdc_vram_write(FRONT_CHIP,g_player_frame_words[i]);
    vdc_set_vram_write(FRONT_CHIP,THROW_BASE_WORD);
    for (i=0;i<256;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,(u16)(THROW_BASE_WORD+256));
    for (i=256;i<320;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,(u16)(THROW_BASE_WORD+384));
    for (i=320;i<384;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,CONTINUE_PLAYER_BASE_WORD);
    for (i=0;i<768;++i) vdc_vram_write(FRONT_CHIP,g_continue_mario_words[i]);
    vdc_set_vram_write(FRONT_CHIP,CONTINUE_LUIGI_BASE_WORD);
    for (i=0;i<768;++i) vdc_vram_write(FRONT_CHIP,g_continue_luigi_words[i]);
    vdc_set_vram_write(FRONT_CHIP,GAMEPLAY_DOWN_MARIO_BASE_WORD);
    for (i=0;i<384;++i) vdc_vram_write(FRONT_CHIP,g_player_down_words[i]);
    vdc_set_vram_write(FRONT_CHIP,GAMEPLAY_DOWN_LUIGI_BASE_WORD);
    for (i=0;i<384;++i) vdc_vram_write(FRONT_CHIP,g_luigi_down_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_LIFE_BASE_WORD); for(i=0;i<128;++i)vdc_vram_write(FRONT_CHIP,g_hud_life_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_FIRE_BASE_WORD); for(i=0;i<64;++i)vdc_vram_write(FRONT_CHIP,g_hud_fire_words[i]);
    vdc_set_vram_write(FRONT_CHIP,CRATE_BASE_WORD); for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_crate_sprite_words[i]);
    /* Level 3 uses the same supplied pot object as boss 3, while the later
       boss rooms upload their complete auxiliary bank in start_boss(). */
    vdc_set_vram_write(FRONT_CHIP,BOSS_POT_BASE_WORD); for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_pot_words[i]);
    vdc_set_vram_write(FRONT_CHIP,GEM_BASE_WORD); for(i=0;i<192;++i)vdc_vram_write(FRONT_CHIP,g_gem_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,FIREBALL_BASE_WORD); for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_fireball_sprite_words[i]);
    if(!g_in_boss){vdc_set_vram_write(FRONT_CHIP,ITEM_BASE_WORD); for(i=0;i<ITEM_KIND_COUNT*ITEM_CELL_WORDS;++i)vdc_vram_write(FRONT_CHIP,g_item_sprite_words[i]);}
    vdc_set_vram_write(FRONT_CHIP,HUD_HEART_BASE_WORD); for(i=0;i<64;++i)vdc_vram_write(FRONT_CHIP,g_hud_heart_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_DIGIT_BASE_WORD); for(i=0;i<640;++i)vdc_vram_write(FRONT_CHIP,g_hud_digit_words[i]);
    if (!g_in_boss && (g_current_level==1 || g_current_level==5 || g_current_level==7)) {
        const u16 *platform_words=(g_current_level==5)?g_level6_platform_words:
                                  (g_current_level==7)?g_level8_platform_words:
                                  g_level2_platform_words;
        vdc_set_vram_write(FRONT_CHIP,PLATFORM_BASE_WORD);
        for(i=0;i<192;++i)vdc_vram_write(FRONT_CHIP,platform_words[i]);
    }
    /* Every other level's stage objects share one bank and one palette line. */
    if (!g_in_boss) {
        const StageObjectData *so=stage_objects();
        if (so->word_count) {
            vdc_set_vram_write(FRONT_CHIP,STAGE_OBJECT_BASE_WORD);
            for(i=0;i<so->word_count;++i)vdc_vram_write(FRONT_CHIP,so->words[i]);
            for(i=0;i<16;++i)tetsu_set_palette((u16)(LEVEL8_OBJ_SPR_PAL*16+i),so->palette[i]);
        }
    }

    /* Level 8's two sliding wedges (metasprites 168/169). */
    if (!g_in_boss && g_current_level==7) {
        vdc_set_vram_write(FRONT_CHIP,LEVEL8_WEDGE_R_BASE_WORD);
        for(i=0;i<768;++i)vdc_vram_write(FRONT_CHIP,g_level8_wedge_right_words[i]);
        vdc_set_vram_write(FRONT_CHIP,LEVEL8_WEDGE_L_BASE_WORD);
        for(i=0;i<384;++i)vdc_vram_write(FRONT_CHIP,g_level8_wedge_left_words[i]);
    }

    /* F3AE/F3E4 in the 68000 disassembly load an enemy palette from F426 ->
       72338 and a 0x180-byte graphics chunk selected through 7246C. The
       generated table reproduces those combinations. Only variants referenced
       by the current level are copied to VDC1. */
    if (!g_in_boss && g_level) {
        if(level_has_enemy_kind(ENEMY_KIND_MONKEY)){vdc_set_vram_write(FRONT_CHIP,ENEMY_BANANA_BASE_WORD);for(i=0;i<64;++i)vdc_vram_write(FRONT_CHIP,g_monkey_banana_words[i]);}
        walk_top=ENEMY_BASE_WORD; hit_top=ENEMY_HIT_BASE_WORD; alt_top=ENEMY_ALT_BASE_WORD;
        shot_top=ENEMY_SHOT_BASE_WORD;
        for(i=0;i<g_level->variant_count;++i) {
            int gi=g_level->variant_ids[i]; const EnemyVariantData *v=&g_enemy_variants[gi]; int line=-1;
            int fw=enemy_frame_words(v);
            for(j=0;j<uniq;++j) if(palettes_equal(seen[j],v->palette)){ line=4+j; break; }
            if(line<0){ seen[uniq]=v->palette; line=4+uniq; for(j=0;j<16;++j)tetsu_set_palette((u16)(line*16+j),v->palette[j]); ++uniq; }
            g_enemy_palette_line[i]=(unsigned char)line;
            g_enemy_vram[i]=(u16)walk_top;
            vdc_set_vram_write(FRONT_CHIP,(u16)walk_top);
            for(j=0;j<fw*2;++j)vdc_vram_write(FRONT_CHIP,v->words[j]);
            walk_top+=fw*2;
            g_enemy_hit_vram[i]=(u16)hit_top;
            if(v->has_hit_frame){
                vdc_set_vram_write(FRONT_CHIP,(u16)hit_top);
                for(j=0;j<fw;++j)vdc_vram_write(FRONT_CHIP,v->words[fw*2+j]);
                hit_top+=fw;
            }
            /* The three extra live cels are shared arrays: a family's DMA
               buffer variants decode to the same image, so upload each
               distinct one once and point the rest of the family at it.  The
               arena only holds four 3x3 cels and level 9 needs exactly four,
               which does not fit without this. */
            for(k=0;k<3;++k){
                const u16 *src=(k==0)?v->alt_words:(k==1)?v->atk_words:v->atk2_words;
                u16 *slot=(k==0)?&g_enemy_alt_vram[i]:(k==1)?&g_enemy_atk_vram[i]:&g_enemy_atk2_vram[i];
                *slot=0;
                if(!src)continue;
                for(j=0;j<i;++j){
                    const EnemyVariantData *p=&g_enemy_variants[g_level->variant_ids[j]];
                    if(p->alt_words==src){*slot=g_enemy_alt_vram[j];break;}
                    if(p->atk_words==src){*slot=g_enemy_atk_vram[j];break;}
                    if(p->atk2_words==src){*slot=g_enemy_atk2_vram[j];break;}
                }
                if(*slot)continue;
                if(alt_top+fw>ENEMY_ALT_BASE_WORD+ENEMY_ALT_WORDS)continue;
                *slot=(u16)alt_top;
                vdc_set_vram_write(FRONT_CHIP,(u16)alt_top);
                for(j=0;j<fw;++j)vdc_vram_write(FRONT_CHIP,src[j]);
                alt_top+=fw;
            }
            /* Every variant of a throwing family shares one projectile array,
               so upload it once and point the rest of the family at it. */
            g_enemy_shot_vram[i]=0;
            if(v->shot_words){
                for(j=0;j<i;++j)
                    if(g_enemy_variants[g_level->variant_ids[j]].shot_words==v->shot_words){
                        g_enemy_shot_vram[i]=g_enemy_shot_vram[j];break;
                    }
                if(!g_enemy_shot_vram[i]&&shot_top+v->shot_cols*64<=ENEMY_SHOT_BASE_WORD+ENEMY_SHOT_WORDS){
                    g_enemy_shot_vram[i]=(u16)shot_top;
                    vdc_set_vram_write(FRONT_CHIP,(u16)shot_top);
                    for(j=0;j<v->shot_cols*64;++j)vdc_vram_write(FRONT_CHIP,v->shot_words[j]);
                    shot_top+=v->shot_cols*64;
                }
            }
        }
        /* The snake strike is a genuine 64x40 metasprite and cannot share the
           legacy 3x3-cell enemy frame. Its authored variant set leaves room
           at $9800 for the three 4x4 poses. */
        if(level_has_enemy_kind(ENEMY_KIND_STRIKE_SNAKE)){
            vdc_set_vram_write(FRONT_CHIP,STRIKE_SNAKE_BASE_WORD);
            for(i=0;i<3072;++i)vdc_vram_write(FRONT_CHIP,g_level8_snake_words[i]);
        }
        if(level_has_enemy_kind(ENEMY_KIND_MONKEY)){
            vdc_set_vram_write(FRONT_CHIP,MONKEY_SPECIAL_BASE_WORD);
            for(i=0;i<1152;++i)vdc_vram_write(FRONT_CHIP,g_monkey_special_words[i]);
        }
        if(level_has_enemy_kind(ENEMY_KIND_CRATE_MIMIC)){
            vdc_set_vram_write(FRONT_CHIP,CRATE_MIMIC_WALK_BASE_WORD);
            for(i=0;i<1152;++i)vdc_vram_write(FRONT_CHIP,g_crate_walk_words[i]);
        }
    }
}

static void set_game_palette_fade(int level)
{
    int i,j,uniq=0; const unsigned short *seen[12];
    const BossVisualData *bv=(g_in_boss&&g_boss_class>0)?&g_later_boss_visuals[g_boss_class-1]:0;
    const BossRoomData *br=(g_in_boss&&g_boss_class==0)?&g_bowser_rooms[(g_current_level==5)?1:0]:0;
    const unsigned short *pal = g_in_boss ? (bv?bv->bg_palette:br->palette) : g_level->palette;
    for(i=0;i<256;++i)tetsu_set_palette((u16)i,fade_yuv(pal[i],level));
    for(i=0;i<16;++i)tetsu_set_palette((u16)(PLAYER_SPR_PAL*16+i),fade_yuv(g_player_palette[i],level));
    for(i=0;i<16;++i)tetsu_set_palette((u16)(CONTINUE_PLAYER_SPR_PAL*16+i),fade_yuv(g_continue_player_palette[i],level));
    if(g_in_boss){
        if(bv){
            for(j=0;j<bv->palette_count;++j){
                int line=j?BOSS_SPR_PAL_AUX:BOSS_SPR_PAL;
                for(i=0;i<16;++i)tetsu_set_palette((u16)(line*16+i),fade_yuv(bv->sprite_palettes[j*16+i],level));
            }
        }else for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_SPR_PAL*16+i),fade_yuv(g_bowser_palette[i],level));
    }
    if(g_in_boss&&g_boss_class>0)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_AUX_SPR_PAL*16+i),fade_yuv(g_final_aux_palette[i],level));
    if(g_in_boss&&g_boss_class>0){
        const unsigned short *item_pal=g_boss_pot_palette;
        if(g_boss_class==3)item_pal=g_boss_bomb_palette;
        else if(g_boss_class==2)item_pal=g_boss_crate4_palette;
        else if(g_boss_class==4)item_pal=g_boss_bell_palette;
        for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_ITEM_SPR_PAL*16+i),fade_yuv(item_pal[i],level));
    }
    if(g_in_boss&&g_boss_class==1)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_WATER_SPR_PAL*16+i),fade_yuv(g_boss_water_palette[i],level));
    if(g_in_boss&&g_boss_class==3)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS13_PLATFORM_SPR_PAL*16+i),fade_yuv(g_boss13_platform_palette[i],level));
    else if(g_level){
        for(i=0;i<g_level->variant_count;++i){
            const EnemyVariantData *v=&g_enemy_variants[g_level->variant_ids[i]]; int line=-1;
            for(j=0;j<uniq;++j)if(palettes_equal(seen[j],v->palette)){line=4+j;break;}
            if(line<0){seen[uniq]=v->palette;line=4+uniq;++uniq;}
            g_enemy_palette_line[i]=(unsigned char)line;
            for(j=0;j<16;++j)tetsu_set_palette((u16)(line*16+j),fade_yuv(v->palette[j],level));
        }
    }
    /* World 3's stage crates (levels 7, 8 and 9; zero-based slots 6..8) are
       the same pot object used by boss 3. Keep their supplied purple/grey
       palette on a dedicated sprite line. */
    if(!g_in_boss&&g_current_level>=6&&g_current_level<=8)
        for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_ITEM_SPR_PAL*16+i),fade_yuv(g_boss_pot_palette[i],level));
    /* Level 8's carriers and wedges are all on source palette line 2. */
    if(!g_in_boss&&g_current_level==7)
        for(i=0;i<16;++i)tetsu_set_palette((u16)(LEVEL8_OBJ_SPR_PAL*16+i),fade_yuv(g_level8_object_palette[i],level));
    else if(stage_objects()->palette)
        for(i=0;i<16;++i)tetsu_set_palette((u16)(LEVEL8_OBJ_SPR_PAL*16+i),fade_yuv(stage_objects()->palette[i],level));
    /* Boss bodies own palette line 14, which the continue room normally uses
       for the separately indexed crouch art. Keep an independent boss-room
       copy after every boss/background palette update. */
    if(g_in_boss)for(i=0;i<16;++i)
        tetsu_set_palette((u16)(BOSS_CROUCH_SPR_PAL*16+i),fade_yuv(g_continue_player_palette[i],level));
    g_game_fade_level=level;
}

static int level_stage_chip(void);
static int level_backdrop_chip(void);

/* Level 6 draws its jungle scenery on the front VDC (level_uses_front_backdrop),
   the same chip the sprites live on, so the canopy only hangs in front of the
   cast if the sprites give up their in-front-of-background priority bit. */
static int level_front_scenery_over_sprites(void)
{
    return !g_in_boss && g_current_level==6;
}

/* Tile rows 28 and 29 sit below the Mega Drive's 224-line V28 picture.  The
   source has no art there, so the generator continues the row-27 texture into
   them; behind the HUD that reads as a band of repeated scenery.  Cover the
   band with a solid black cell instead.  It has to be a real opaque cell: a
   blank one leaves colour index 0, which the VDC treats as transparent, so the
   other chip's plane would simply show through it. */
#define HUD_BAND_FIRST_ROW 28
static u16 g_hud_band_cell;

/* Named for what the band shows, not for how: the stage plane keeps its own
   cells there and is simply covered. */
static int level_blank_hud_band(void)
{
    return !g_in_boss && g_current_level==6;
}

/* Palette entry for the band, as a BAT palette line in 0..3 plus a colour
   index in 1..15.  Only the level's own four background lines are searched:
   lines 4 and up are handed to the enemy variants by set_game_palette_fade().
   Colour 0 of any line is the transparent one, so it cannot be used. */
static int level_black_palette_entry(void)
{
    int i;
    if(!g_level)return 0;
    for(i=1;i<64;++i)
        if((i&15)&&g_level->palette[i]==YUV_BLACK)return i;
    return 0;
}

/* Build the band cell and upload its pattern.  The cell is one flat colour, so
   every bitplane row is either all ones or all zeros.  Levels that blank the
   band are exactly the ones whose backdrop is on VDC1 (level 6), which is
   where HUD_BAND_BLACK_TILE_WORD is reserved. */
static void prepare_hud_band_cell(void)
{
    int entry=level_black_palette_entry(),i;
    g_hud_band_cell=0;
    if(!entry||level_backdrop_chip()!=FRONT_CHIP)return;
    vdc_set_vram_write(FRONT_CHIP,HUD_BAND_BLACK_TILE_WORD);
    for(i=0;i<8;++i)
        vdc_vram_write(FRONT_CHIP,(u16)(((entry&1)?0x00FFu:0)|((entry&2)?0xFF00u:0)));
    for(i=0;i<8;++i)
        vdc_vram_write(FRONT_CHIP,(u16)(((entry&4)?0x00FFu:0)|((entry&8)?0xFF00u:0)));
    g_hud_band_cell=(u16)(((entry>>4)<<12)|(HUD_BAND_BLACK_TILE_WORD/TILE_WORDS));
}

/* --- Screen grid -------------------------------------------------------
   $14370 walks the route one record at a time; the record's mode selects
   where the incoming screen is pasted relative to the outgoing one (table
   $144B6: mode 0 right, 1 left, 2 above, 3 below).  Reconstruct that walk
   once at level load so every world coordinate can be resolved to a route
   index, and so the camera knows how tall the stage really is.

   The first route entry is always the stage entrance, so after normalising
   the upward steps it sits on the bottom row of the grid. */
#define LEVEL_SCREEN_H     224           /* MD V28 picture: 28 tile rows */
#define LEVEL_SCREEN_ROWS  28
#define LEVEL_MAX_SCREENS  40
#define LEVEL_GRID_COLS    40
#define LEVEL_GRID_ROWS    20   /* slot 12 is a 17-screen tower */
static unsigned char g_screen_col[LEVEL_MAX_SCREENS];
static unsigned char g_screen_row[LEVEL_MAX_SCREENS];
static unsigned char g_screen_grid[LEVEL_GRID_ROWS*LEVEL_GRID_COLS];
static int g_screen_count;

static void build_level_layout(void)
{
    signed char rows[LEVEL_MAX_SCREENS];
    int i,c=0,r=0,min_r=0,max_r=0,max_c=0,n=g_level->route_len;
    if(n>LEVEL_MAX_SCREENS)n=LEVEL_MAX_SCREENS;
    g_screen_count=n;
    for(i=0;i<LEVEL_GRID_ROWS*LEVEL_GRID_COLS;++i)g_screen_grid[i]=255;
    g_screen_col[0]=0;rows[0]=0;
    for(i=1;i<n;++i){
        switch(g_level->route_modes[i]){
        case 1: if(c>0)--c; break;
        case 2: --r; break;
        case 3: ++r; break;
        default: ++c; break;
        }
        if(c>LEVEL_GRID_COLS-1)c=LEVEL_GRID_COLS-1;
        if(r<min_r)min_r=r;
        if(r>max_r)max_r=r;
        if(c>max_c)max_c=c;
        g_screen_col[i]=(unsigned char)c;rows[i]=(signed char)r;
    }
    if(max_r-min_r>LEVEL_GRID_ROWS-1)max_r=min_r+LEVEL_GRID_ROWS-1;
    g_level_rows=max_r-min_r+1;
    g_level_cols=max_c+1;
    for(i=0;i<n;++i){
        int gr=rows[i]-min_r;
        if(gr<0)gr=0; if(gr>=g_level_rows)gr=g_level_rows-1;
        g_screen_row[i]=(unsigned char)gr;
        /* A later pass over the same cell wins, exactly as the streamer's
           quadrant overwrite does when a route doubles back. */
        g_screen_grid[gr*LEVEL_GRID_COLS+g_screen_col[i]]=(unsigned char)i;
    }
}

/* Split a world tile row into (grid row, row inside the 30-row segment).
   Only the bottom screen owns the two extension rows below the 224-line
   picture, which is where the HUD band lives. */
static int split_world_row(int tile_row,int *local)
{
    int r=0;
    while(tile_row>=LEVEL_SCREEN_ROWS && r+1<g_level_rows){tile_row-=LEVEL_SCREEN_ROWS;++r;}
    *local=tile_row;
    return r;
}

/* Route index covering a world tile, or -1 where the path never goes. */
static int screen_index_at(int world_col,int grid_row)
{
    int sc=world_col>>5,si;
    if(sc<0||sc>=LEVEL_GRID_COLS||grid_row<0||grid_row>=g_level_rows)return -1;
    si=g_screen_grid[grid_row*LEVEL_GRID_COLS+sc];
    return (si==255)?-1:si;
}

/* Flat generator coordinates are (route index * 256 + offset); with stacked
   screens the route index no longer equals the world column, so both axes
   have to be rebuilt from it. */
static int route_index_for_flat_x(int flat_x)
{
    int si=flat_x>>8;
    if(si<0)si=0;
    if(si>=g_screen_count)si=g_screen_count-1;
    return si;
}

static int world_x_from_flat(int flat_x)
{
    if(g_in_boss||!g_level)return flat_x;
    return (int)g_screen_col[route_index_for_flat_x(flat_x)]*256+(flat_x&255);
}

static int world_y_from_flat(int flat_x,int y)
{
    if(g_in_boss||!g_level)return y;
    return (int)g_screen_row[route_index_for_flat_x(flat_x)]*LEVEL_SCREEN_H+y;
}

static u16 level_fg_bat_entry(int world_col,int tile_row)
{
    int seg,lx,si,lr;
    if(!g_level||!g_stage_set||world_col<0||world_col>=g_level_width_tiles||tile_row<0)
        return FG_TILE_BASE_INDEX;
    si=screen_index_at(world_col,split_world_row(tile_row,&lr));
    if(si<0||lr<0||lr>=30)return FG_TILE_BASE_INDEX;
    lx=world_col&31;
    seg=g_level->route[si]; if(seg==255||seg>=g_stage_set->segment_count)return FG_TILE_BASE_INDEX;
    return relocate_bat(g_stage_set->segment_bat[(seg*30+lr)*32+lx],FG_TILE_BASE_INDEX);
}

static void stream_level_column(int world_col, int display_enabled)
{
    int row,chip=level_stage_chip(),map_col=world_col&(FG_BAT_W-1);
    /* Only VDC1 carries the SATB.  Levels 6 and 7 stream their stage plane
       into VDC0, so restoring a blanket BB|SB here switched sprites on for a
       chip with no sprite table, which drew shifting garbage over the scenery
       from the first scrolled column onward. */
    u16 planes=display_enabled
        ? (u16)(VDC_CR_BB|((chip==FRONT_CHIP)?VDC_CR_SB:0)) : 0;
    if(world_col<0||world_col>=g_level_width_tiles)return;

    /* A BAT column is contiguous when MAWR advances by the 64-word map
       stride. Select the address once, stream the column, then restore the
       normal +1 CHR/BAT mode used by sprite and pattern uploads.

       The column covers the whole 32-row BAT, which is the vertical window
       the camera can be showing: world row R always lands on BAT row R&31,
       so the burst has to be split where it wraps off the bottom. */
    vdc_setreg(chip,VDC_REG_CR,
               (u16)(planes|VDC_CR_IW_40));
    {
        int first=g_camera_row-1,bat=first&(FG_BAT_H-1),head=FG_BAT_H-bat;
        vdc_set_vram_write(chip,(u16)(bat*FG_BAT_W+map_col));
        for(row=0;row<head;++row)
            vdc_vram_write(chip,level_fg_bat_entry(world_col,first+row));
        if(bat){
            vdc_set_vram_write(chip,(u16)map_col);
            for(;row<FG_BAT_H;++row)
                vdc_vram_write(chip,level_fg_bat_entry(world_col,first+row));
        }
    }
    vdc_setreg(chip,VDC_REG_CR,
               (u16)(planes|VDC_CR_IW_01));
}

/* The vertical counterpart: one full 64-cell BAT row, used when a stacked
   level scrolls a new tile row into the window. */
static void stream_level_row(int world_row)
{
    int col,chip=level_stage_chip(),map_row=world_row&(FG_BAT_H-1);
    if(world_row<0)return;
    vdc_set_vram_write(chip,(u16)(map_row*FG_BAT_W));
    for(col=0;col<FG_BAT_W;++col){
        int world_col=g_camera_tile+((col-g_camera_tile)&(FG_BAT_W-1));
        vdc_vram_write(chip,level_fg_bat_entry(world_col,world_row));
    }
}

static int level_uses_front_backdrop(void)
{
    return !g_in_boss && (g_current_level == 6 || g_current_level == 7);
}

static int level_stage_chip(void)
{
    return level_uses_front_backdrop() ? BACK_CHIP : FRONT_CHIP;
}

static int level_backdrop_chip(void)
{
    return level_uses_front_backdrop() ? FRONT_CHIP : BACK_CHIP;
}

static void upload_level_backdrop(void)
{
    int x,y,chip=level_backdrop_chip();
    prepare_hud_band_cell();
    vdc_set_vram_write(chip,FG_TILE_BASE_WORD);
    for(x=0;x<g_stage_set->tile_count*TILE_WORDS;++x)vdc_vram_write(chip,g_stage_set->tile_words[x]);
    for(y=0;y<30;++y){
        vdc_set_vram_write(chip,(u16)(y*BG_BAT_W));
        /* The background plane is the Mega Drive's full 64-cell width; it
           wraps every 512 pixels, not every 256. */
        int band=(y>=HUD_BAND_FIRST_ROW)&&level_blank_hud_band()&&g_hud_band_cell;
        for(x=0;x<64;++x){
            if(band)vdc_vram_write(chip,g_hud_band_cell);
            else vdc_vram_write(chip,relocate_bat(g_level->bg_bat[y*BG_BAT_W+x],FG_TILE_BASE_INDEX));
        }
    }
}

static void init_level_ring(void)
{
    int col,i,chip;
    g_level=&g_game_levels[g_current_level]; g_stage_set=&g_stage_sets[g_level->set_index];
    build_level_layout(); g_level_width_tiles=g_level_cols*32;
    clear_bat(BACK_CHIP,BG_BAT_WORDS); clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    set_map_height(BACK_CHIP,0); set_map_height(FRONT_CHIP,0);
    if(g_level_rows>1) set_map_height(level_stage_chip(),1);
    upload_level_backdrop();
    chip=level_stage_chip();
    vdc_set_vram_write(chip,FG_TILE_BASE_WORD);
    for(i=0;i<g_stage_set->tile_count*TILE_WORDS;++i)vdc_vram_write(chip,g_stage_set->tile_words[i]);
    /* The route always starts on the bottom row of the grid, so the camera
       opens there and climbs. */
    g_camera_x=0;g_camera_target_x=0;g_camera_tile=0;
    g_camera_y=g_camera_target_y=(g_level_rows-1)*LEVEL_SCREEN_H;
    g_camera_row=g_camera_y>>3;
    for(col=0;col<FG_BAT_W;++col)stream_level_column(col,0);
    vdc_set_scroll(BACK_CHIP,0,0);
    vdc_set_scroll(FRONT_CHIP,0,0);
    vdc_set_scroll(level_stage_chip(),0,(u16)(g_camera_y&0x00FF));
    upload_game_sprites(); set_game_palette_fade(16);
}

static unsigned char collision_at(int x,int y)
{
    int tx,ty,screen,seg,lx;
    if(g_in_boss){
        int ci=(g_boss_class<0||g_boss_class>5)?0:g_boss_class;
        if(x<0||x>=256)return 1;if(y<0)return 0;if(y>=256)return 1;
        /* Slot 13's three gold carriers are separate source objects rather
           than entries in the room grid. Their captured positions remain
           one-way surfaces in the PC-FX reconstruction. */
        if(ci==3&&((x<64&&y>=64&&y<72)||(x>=160&&x<224&&y>=88&&y<96)||(x>=192&&y>=176&&y<184)))return 2;
        /* Exact 32x32 collision grids selected by ROM table $7038C. */
        return g_boss_collision[ci*1024+(y>>3)*32+(x>>3)];
    }
    if(x<0)return 1;tx=x>>3;ty=y>>3;
    if(!g_level||tx<0||tx>=g_level_width_tiles)return 1;if(ty<0)return 0;
    {int lr;screen=screen_index_at(tx,split_world_row(ty,&lr));ty=lr;}
    if(screen<0||ty<0||ty>=32)return 0;
    lx=tx&31;seg=g_level->route[screen];if(seg==255||seg>=g_stage_set->segment_count)return 0;
    return g_stage_set->collision[seg*1024+ty*32+lx];
}

/* Extracted collision bytes are directional: 1 is a full terrain boundary,
 * 2 marks horizontal surfaces, 4 marks vertical surfaces. Treating every
 * nonzero byte as a full solid block was the main reason the earlier player
 * controller snagged on castle/ledge geometry. */
static int solid_h(int x, int y)
{
    unsigned char c = collision_at(x,y);
    return c == 1 || c == 4;
}

static int solid_v(int x, int y)
{
    unsigned char c = collision_at(x,y);
    return c == 1 || c == 2;
}

/* Collision value 2 is the Stage-1 brown-brick/platform surface. It is solid
   only when approached from above. Full terrain (1) still blocks Mario's head. */
static int solid_ceiling(int x, int y)
{
    return collision_at(x,y) == 1;
}

static int player_body_x(void) { return (int)(g_player_x >> FIX_SHIFT) + PLAYER_BODY_XOFF; }
static int player_body_y(void) { return (int)(g_player_y >> FIX_SHIFT) + PLAYER_BODY_YOFF; }

/* The first two arenas retain the six-pixel raster lead from their captured
   Mega Drive room plane.  Bowser's extracted poses include that lead, whereas
   Mario and the generic crate metasprites do not. */
static int bowser_room_object_y_offset(void)
{
    return (g_in_boss && g_boss_class == 0) ? -6 : 0;
}

static int crate_at_side(int bx, int by, int moving_right)
{
    int i;
    /* In the original object interaction Mario can rise past the crate side
       and settle onto its top.  Only treat the crate as a side wall while he
       is actually grounded; otherwise the old solver blocked every attempt
       to jump onto it from beside the box. */
    if (!g_grounded) return -1;
    for (i=0;i<MAX_CRATES;++i) {
        int cx,cy;
        Crate *c=&g_crates[i];
        if (!c->active || c->carried) continue;
        cx=(int)(c->x>>FIX_SHIFT); cy=(int)(c->y>>FIX_SHIFT);
        if (by+PLAYER_BODY_H<=cy+2 || by>=cy+CRATE_H-2) continue;
        if (moving_right) {
            if (bx+PLAYER_BODY_W>cx && bx<cx) return i;
        } else {
            if (bx<cx+CRATE_W && bx+PLAYER_BODY_W>cx+CRATE_W) return i;
        }
    }
    return -1;
}

static void resolve_player_horizontal(s32 next_x)
{
    int anchor=(int)(next_x>>FIX_SHIFT);
    int bx=anchor+PLAYER_BODY_XOFF;
    int by=player_body_y();
    if (g_vx>0) {
        int side=bx+PLAYER_BODY_W-1;
        int ci=crate_at_side(bx,by,1);
        if (solid_h(side,by+3)||solid_h(side,by+PLAYER_BODY_H-3)) {
            int tile_left=(side>>3)<<3;
            anchor=tile_left-PLAYER_BODY_XOFF-PLAYER_BODY_W;
            next_x=(s32)anchor<<FIX_SHIFT; g_vx=0;
        } else if (ci>=0) {
            int cx=(int)(g_crates[ci].x>>FIX_SHIFT);
            anchor=cx-PLAYER_BODY_XOFF-PLAYER_BODY_W;
            next_x=(s32)anchor<<FIX_SHIFT; g_vx=0;
        }
    } else if (g_vx<0) {
        int side=bx;
        int ci=crate_at_side(bx,by,0);
        if (solid_h(side,by+3)||solid_h(side,by+PLAYER_BODY_H-3)) {
            int tile_right=((side>>3)+1)<<3;
            anchor=tile_right-PLAYER_BODY_XOFF;
            next_x=(s32)anchor<<FIX_SHIFT; g_vx=0;
        } else if (ci>=0) {
            int cx=(int)(g_crates[ci].x>>FIX_SHIFT);
            anchor=cx+CRATE_W-PLAYER_BODY_XOFF;
            next_x=(s32)anchor<<FIX_SHIFT; g_vx=0;
        }
    }
    g_player_x=next_x;
}

static int crate_floor_top(int bx, int old_bottom, int new_bottom)
{
    int i,best=0x7FFF;
    for (i=0;i<MAX_CRATES;++i) {
        int cx,cy;
        Crate *c=&g_crates[i];
        if (!c->active || c->carried) continue;
        cx=(int)(c->x>>FIX_SHIFT); cy=(int)(c->y>>FIX_SHIFT);
        if (bx+PLAYER_BODY_W-3<=cx || bx+3>=cx+CRATE_W) continue;
        if (old_bottom<=cy+1 && new_bottom>=cy && cy<best) best=cy;
    }
    return best;
}

static int crate_ceiling_bottom(int bx, int old_top, int new_top)
{
    int i,best=-0x7FFF;
    for (i=0;i<MAX_CRATES;++i) {
        int cx,cy,bottom;
        Crate *c=&g_crates[i];
        if (!c->active || c->carried || c->thrown) continue;
        cx=(int)(c->x>>FIX_SHIFT); cy=(int)(c->y>>FIX_SHIFT);
        if (bx+PLAYER_BODY_W-3<=cx || bx+3>=cx+CRATE_W) continue;
        bottom=cy+CRATE_H;
        if (old_top>=bottom-1 && new_top<=bottom && bottom>best) best=bottom;
    }
    return best;
}

static void resolve_player_vertical(s32 next_y)
{
    int bx=player_body_x();
    int old_by=player_body_y();
    int old_bottom=old_by+PLAYER_BODY_H-1;
    int anchor=(int)(next_y>>FIX_SHIFT);
    int by=anchor+PLAYER_BODY_YOFF;
    g_grounded=0;
    if (g_vy>=0) {
        int new_bottom=by+PLAYER_BODY_H-1;
        int probe,best_top=0x7FFF,best_platform=-1,i;
        int crate_top=crate_floor_top(bx,old_bottom,new_bottom+1);
        if (crate_top<best_top) best_top=crate_top;
        g_on_moving_platform=-1;

        /* Moving platforms are one-way floors: Mario can pass upward through
           them, then land when his feet cross the top from above. */
        for (i=0;i<g_moving_platform_count && g_drop_platform_timer==0;++i) {
            MovingPlatform *p=&g_moving_platforms[i];
            int px, top, platform_w;
            /* The boulder is a hazard rolling through the stage, not one of
               the carriers: $B086 keeps no standing surface. */
            if (p->kind==PLATFORM_KIND_BOULDER) continue;
            px=platform_draw_x(p); top=platform_draw_y(p);
            platform_w=platform_width(p);
            if (bx+PLAYER_BODY_W-3<=px || bx+3>=px+platform_w) continue;
            if (old_bottom<=top+2 && new_bottom+1>=top && top<best_top) {
                best_top=top;
                best_platform=i;
            }
        }

        /* Scan every crossed pixel so the source jump table's 4-6 px steps
           cannot tunnel through one-way/horizontal collision records. */
        for (probe=old_bottom;probe<=new_bottom+1;++probe) {
            unsigned char left=collision_at(bx+3,probe),right=collision_at(bx+PLAYER_BODY_W-4,probe);
            int hit=(left==1||right==1)||
                (g_drop_platform_timer==0&&(left==2||right==2));
            if (hit) {
                int top=(probe>>3)<<3;
                if (top<best_top) {
                    best_top=top;
                    /* -2 identifies a collision-map one-way floor; nonnegative
                       values continue to identify sprite platforms. */
                    best_platform=(left==2||right==2)?-2:-1;
                }
                break;
            }
        }
        if (best_top!=0x7FFF) {
            anchor=best_top-PLAYER_BODY_YOFF-PLAYER_BODY_H;
            next_y=(s32)anchor<<FIX_SHIFT;
            g_vy=0; g_grounded=1; g_jump_active=0;
            g_on_moving_platform=best_platform;
        }
    } else {
        int old_top=old_by, new_top=by, probe;
        int stop=crate_ceiling_bottom(bx,old_top,new_top);
        g_on_moving_platform=-1;
        for (probe=old_top-1;probe>=new_top;--probe) {
            if (solid_ceiling(bx+3,probe)||solid_ceiling(bx+PLAYER_BODY_W-4,probe)) {
                int tile_bottom=((probe>>3)+1)<<3;
                if (tile_bottom>stop) stop=tile_bottom;
                break;
            }
        }
        if (stop>-0x7FFF) {
            anchor=stop-PLAYER_BODY_YOFF;
            next_y=(s32)anchor<<FIX_SHIFT;
            g_vy=0; g_jump_active=0;
        }
    }
    g_player_y=next_y;
}

/* ROM $17948: signed per-frame displacement for the original normal jump.
 * Full hold rises 83 pixels; releasing I during the rising half mirrors into
 * the falling half, reproducing the original short-jump behaviour.
 *
 * The four entries marked below are -2 in the Mega Drive table.  Carrying the
 * -4 run through them adds half a tile of clearance, which the port needs
 * because its terrain lands the level 8 ledges an 8px row above the original
 * and the 75px arc stops just short of the second one.  The apex and the
 * descent are untouched, so the mirrored short jump still behaves. */
static const signed char g_md_jump_dy[45]={
    -6,-6,-5,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
    -4,-4,-4,-4,-2,-2,-2,0,0,0,2,2,2,2,2,2,2,
    4,4,4,4,4,4,4,4,4,4,4,5,6,6
};

/* Short automatic rebound after a stomp. It is deliberately lower than a
   player jump and does not require the jump button to remain held. */
static const signed char g_stomp_bounce_dy[18]={
    -4,-4,-3,-3,-3,-2,-2,-1,-1,0,1,1,2,2,3,3,4,4
};

/* Exact state-18 flying-enemy path table at Mega Drive ROM $C832. Handler
   $B794 subtracts one pixel from X, adds one signed entry to Y, then wraps
   the index before 58. The entries sum to zero, so the patrol never drifts. */
static const signed char g_flying_koopa_dy[58]={
    -4,-4,-4,-3,-3,-3,-3,-2,-2,-2,-2,-1,-1,-1, 0,
     1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
     4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 0,-1,
    -1,-1,-2,-2,-2,-2,-3,-3,-3,-3,-4,-4,-4
};

static void update_camera(void)
{
    if(g_in_boss){
        g_camera_x=0;g_camera_target_x=0;g_camera_tile=0;
        g_camera_y=0;g_camera_target_y=0;g_camera_row=0;
        vdc_set_scroll(FRONT_CHIP,0,0);
        /* Handler $940E continuously advances the final room's wall plane;
           its collision/platform plane remains fixed. */
        vdc_set_scroll(BACK_CHIP,0,(g_boss_class==5)?(u16)((g_boss_timer>>2)%224):0);
        return;
    }
    int px=(int)(g_player_x>>FIX_SHIFT);
    int py=(int)(g_player_y>>FIX_SHIFT);
    int max_camera=g_level_width_tiles*8-SCREEN_W;
    int max_camera_y=(g_level_rows-1)*LEVEL_SCREEN_H;
    int target=g_camera_x;
    int target_y=g_camera_y;
    int new_tile,new_row;

    /* The MD stage camera does not re-center on Mario every frame. Keep Mario
       inside a forward dead-zone and never pull the map backwards because an
       action temporarily stops him. This also prevents world-object snapping
       on throw/reversal frames. */
    if (!g_camera_hold && px-g_camera_x>112)
        target=px-112;
    if (target<0) target=0;
    if (target>max_camera) target=max_camera;

    /* Prepare wrapped BAT columns before publishing the target. These cells
       are outside the committed viewport, so the VRAM burst is not visible
       and does not consume the leading blanking interval used by KING. */
    /* Keep one complete tile behind the logical viewport before recycling its
       BAT column.  The VDC fetch window retains a few pixels to the left of
       BXR, so replacing a column at the exact 8-pixel boundary exposes a
       sliver of the far-ahead column as scrolling garbage on the left edge. */
    /* Vertical follow first: a new BAT row has to be pasted with the window
       the columns are about to be streamed against.  There is no one-way
       rule here -- the source's climbs are walked back down again -- so the
       camera keeps Mario inside a band and clamps to the stacked height. */
    if(g_level_rows>1){
        if(py-target_y<72)target_y=py-72;
        if(py-target_y>144)target_y=py-144;
        if(target_y<0)target_y=0;
        if(target_y>max_camera_y)target_y=max_camera_y;
        new_row=target_y>>3;
        while(g_camera_row<new_row){++g_camera_row;stream_level_row(g_camera_row+30);}
        while(g_camera_row>new_row){--g_camera_row;stream_level_row(g_camera_row-1);}
    }else target_y=0;
    g_camera_target_y=target_y;

    new_tile=(target>=16)?((target>>3)-1):0;
    while (g_camera_tile<new_tile) {
        ++g_camera_tile;
        stream_level_column(g_camera_tile+FG_BAT_W-1,1);
    }
    while (g_camera_tile>new_tile) {
        --g_camera_tile;
        stream_level_column(g_camera_tile,1);
    }
    g_camera_target_x=target;
}

static void commit_stage_camera(void)
{
    int stage_chip,backdrop_chip;
    if(g_in_boss)return;

    /* update_camera() runs after gameplay simulation and only publishes a
       target after its off-screen BAT column is ready. Commit only the scroll
       registers here, immediately after frame wrap: this prevents the visible
       split from late scroll writes without putting a VRAM-write burst in the
       leading blanking interval, where it can disturb KING ADPCM playback. */
    g_camera_x=g_camera_target_x;
    g_camera_y=g_camera_target_y;
    stage_chip=level_stage_chip();
    backdrop_chip=level_backdrop_chip();
    /* The stage BAT is 64x32 cells, so the vertical scroll wraps every 256
       pixels; the streamer keeps world row R on BAT row R&31 to match. */
    vdc_set_scroll(stage_chip,(u16)(g_camera_x&0x01FF),(u16)(g_camera_y&0x00FF));
    /* Level 6/7's "backdrop" map is really the stage's foreground scenery and
       is dithered against the stage plane behind it.  Half-rate parallax slides
       that dither over a plane moving at a different speed, so the checkerboard
       cells keep re-mixing with whatever is underneath and appear to change
       colour as the level scrolls.  Lock it to the stage rate. */
    vdc_set_scroll(backdrop_chip,
                   (u16)((level_uses_front_backdrop()?g_camera_x:(g_camera_x>>1))&0x01FF),0);
}


/* The level's stage object table, or a null entry outside gameplay. */
static const StageObjectData *stage_objects(void)
{
    static const StageObjectData none={0,0,0,0,0,0,0};
    if (g_in_boss || !g_level) return &none;
    return &g_stage_objects[g_current_level];
}

/* (word offset, cols, rows, x offset, y offset, width, height) */
static const unsigned short *platform_shape(const MovingPlatform *p)
{
    const StageObjectData *so=stage_objects();
    if (!PLATFORM_KIND_IS_STAGE_OBJECT(p->kind) || p->shape>=so->shape_count) return 0;
    return so->shapes+p->shape*STAGE_OBJECT_SHAPE_WORDS;
}

/* Screen width of each carrier kind; also the width of its standing surface. */
static int platform_width(const MovingPlatform *p)
{
    const unsigned short *sh=platform_shape(p);
    if (sh) return sh[5];
    if (g_in_boss && g_boss_class==3) return 64;
    if (p->kind==PLATFORM_KIND_WEDGE_L) return 32;
    return MOVING_PLATFORM_W;
}

static int platform_height(const MovingPlatform *p)
{
    const unsigned short *sh=platform_shape(p);
    if (sh) return sh[6];
    if (p->kind==PLATFORM_KIND_WEDGE_R) return 64;
    if (p->kind==PLATFORM_KIND_WEDGE_L) return 48;
    return MOVING_PLATFORM_H;
}

/* Stage object records store the object position; the metasprite hangs off it
   by the shape's own origin, which is negative for a few of them. */
static int platform_draw_x(const MovingPlatform *p)
{
    const unsigned short *sh=platform_shape(p);
    return (int)(p->x>>FIX_SHIFT)+(sh?(short)sh[3]:0);
}

static int platform_draw_y(const MovingPlatform *p)
{
    const unsigned short *sh=platform_shape(p);
    return (int)(p->y>>FIX_SHIFT)+(sh?(short)sh[4]:0);
}

static void init_moving_platforms(void)
{
    MovingPlatform *p;
    int i;
    for(i=0;i<MAX_MOVING_PLATFORMS;++i){
        g_moving_platforms[i].kind=PLATFORM_KIND_LIFT;
        g_moving_platforms[i].state=PLATFORM_STATE_IDLE;
        g_moving_platforms[i].timer=0;
    }
    g_moving_platform_count=0;
    g_on_moving_platform=-1;
    g_drop_platform_timer=0;
    if (g_in_boss) {
        /* The $8CEC room owns two live carriers. They are not decoration:
           both Mario and the frog use their current Y while waiting/jumping. */
        if(g_boss_class==3){
            p=&g_moving_platforms[g_moving_platform_count++];
            p->x=(s32)16<<FIX_SHIFT;p->y=(s32)48<<FIX_SHIFT;p->min_y=48;p->max_y=120;p->dir=1;p->speed=1;
            p=&g_moving_platforms[g_moving_platform_count++];
            p->x=(s32)192<<FIX_SHIFT;p->y=(s32)152<<FIX_SHIFT;p->min_y=80;p->max_y=152;p->dir=-1;p->speed=1;
        }
        return;
    }
    /* Exact 12-byte records loaded through $10B2A/$10A48. Handler 1 at $F5B0
       moves each 48x8 carrier down one pixel for 100 frames, then up for 100.
       World X is reconstructed from (route segment,tile trigger). */
    if (g_current_level==1) {
        p=&g_moving_platforms[g_moving_platform_count++];
        p->x=(s32)1296<<FIX_SHIFT;p->y=(s32)72<<FIX_SHIFT;p->min_y=72;p->max_y=172;p->dir=1;p->speed=1;
        p=&g_moving_platforms[g_moving_platform_count++];
        p->x=(s32)1376<<FIX_SHIFT;p->y=(s32)88<<FIX_SHIFT;p->min_y=88;p->max_y=188;p->dir=1;p->speed=1;
        p=&g_moving_platforms[g_moving_platform_count++];
        p->x=(s32)2208<<FIX_SHIFT;p->y=(s32)48<<FIX_SHIFT;p->min_y=48;p->max_y=148;p->dir=1;p->speed=1;
    } else if (g_current_level==5) {
        /* Level 6 record $10BA2: segment 5, tile 16, (x,y)=(384,200),
           metasprite 742, handler 1 -> world (1408,72). */
        p=&g_moving_platforms[g_moving_platform_count++];
        p->x=(s32)1408<<FIX_SHIFT;p->y=(s32)72<<FIX_SHIFT;p->min_y=72;p->max_y=172;p->dir=1;p->speed=1;
    } else if (g_current_level==7) {
        /* Level 8 record table $11120: the bobbing brick carriers (handler
           16) plus the pairs of grass wedges that handlers 14/15 slide out
           from under the player. */
        for (i=0;i<(int)g_level8_carrier_count && g_moving_platform_count<MAX_MOVING_PLATFORMS;++i) {
            const short *rec=g_level8_carriers[i];
            p=&g_moving_platforms[g_moving_platform_count++];
            p->x=p->home_x=(s32)rec[0]<<FIX_SHIFT;
            p->y=p->home_y=(s32)rec[1]<<FIX_SHIFT;
            p->kind=(unsigned char)rec[2];
            p->state=PLATFORM_STATE_IDLE;p->timer=0;
            p->dir=1;p->speed=1;
            p->min_y=rec[1];p->max_y=(short)(rec[1]+32);
        }
    }
    /* Every other level's records come straight from the $10B2A table.  Level
       1 and the boss stages have none; levels 2/6/8 are spawned above. */
    {
        const StageObjectData *so=stage_objects();
        for (i=0;i<so->record_count && g_moving_platform_count<MAX_MOVING_PLATFORMS;++i) {
            const short *rec=so->records+i*STAGE_OBJECT_RECORD_WORDS;
            int ry=world_y_from_flat(rec[0],rec[1]);
            p=&g_moving_platforms[g_moving_platform_count++];
            p->x=p->home_x=(s32)world_x_from_flat(rec[0])<<FIX_SHIFT;
            p->y=p->home_y=(s32)ry<<FIX_SHIFT;
            p->shape=(unsigned char)rec[2];
            p->kind=(unsigned char)rec[3];
            p->state=PLATFORM_STATE_IDLE;p->timer=0;p->step=0;
            /* $F78E starts its shuttle moving left; $F5B0 starts sinking. */
            p->dir=(rec[3]==PLATFORM_KIND_SINKER)?1:-1;p->speed=1;
            p->min_y=(short)ry;p->max_y=(short)(ry+32);
        }
    }
}

/* One $F4FA stage-object handler per kind.  All of them move the record and
   let the shared rider fix-up below carry whoever is standing on it, which is
   what $F546 does for the 68000. */
/* The $F934/$F980 wrap thresholds are screen-space constants.  On a stacked
   level each carrier belongs to the storey it was authored on, so anchor the
   wrap to that screen instead of to the top of the world. */
static int platform_storey_base(const MovingPlatform *p)
{
    int home=(int)(p->home_y>>FIX_SHIFT);
    if(g_level_rows<=1||home<0)return 0;
    return (home/LEVEL_SCREEN_H)*LEVEL_SCREEN_H;
}

static void update_stage_object(MovingPlatform *p, int index)
{
    int y,base;
    switch (p->kind) {
    case PLATFORM_KIND_STATIC:
        break;
    case PLATFORM_KIND_SHUTTLE:      /* $F78E */
        p->x+=(s32)p->dir*FIX_ONE;
        if (++p->timer>=70) { p->timer=0; p->dir=(signed char)-p->dir; }
        break;
    case PLATFORM_KIND_FALLER:       /* $F8E2 */
        base=platform_storey_base(p);
        if (p->state==PLATFORM_STATE_IDLE) {
            if (g_on_moving_platform==index) p->state=PLATFORM_STATE_FALL;
            break;
        }
        p->y+=4*FIX_ONE;
        if ((int)(p->y>>FIX_SHIFT)>=base+PLATFORM_WRAP_BOTTOM) {
            /* $1312A despawns it; the record is restored once it is off camera
               so a second pass over the same trigger finds it again. */
            if (g_on_moving_platform==index) { g_on_moving_platform=-1; g_grounded=0; }
            p->x=p->home_x; p->y=p->home_y; p->state=PLATFORM_STATE_IDLE;
        }
        break;
    case PLATFORM_KIND_DESCEND:      /* $F934 */
        base=platform_storey_base(p);
        p->y+=FIX_ONE;
        y=(int)(p->y>>FIX_SHIFT);
        if (y>=base+PLATFORM_WRAP_BOTTOM) {
            if (g_on_moving_platform==index) { g_on_moving_platform=-1; g_grounded=0; }
            p->y=(s32)(base+PLATFORM_WRAP_TOP)<<FIX_SHIFT;
        }
        break;
    case PLATFORM_KIND_ASCEND:       /* $F980 */
        base=platform_storey_base(p);
        p->y-=FIX_ONE;
        y=(int)(p->y>>FIX_SHIFT);
        if (y<=base+PLATFORM_WRAP_CEIL) {
            if (g_on_moving_platform==index) { g_on_moving_platform=-1; g_grounded=0; }
            p->y=(s32)(base+PLATFORM_WRAP_REBASE)<<FIX_SHIFT;
        }
        break;
    case PLATFORM_KIND_PATH:         /* $F9C6 */
        /* $F9CA gates the step on bit 0 of the frame counter. */
        if (g_state_frame&1) break;
        p->x+=(s32)g_stage_object_path[p->step*2]<<FIX_SHIFT;
        p->y+=(s32)g_stage_object_path[p->step*2+1]<<FIX_SHIFT;
        if (++p->step>=80) p->step=0;
        break;
    case PLATFORM_KIND_SINKER:       /* $F5B0, as the level 2/6 lifts */
        p->y+=(s32)p->dir*FIX_ONE;
        if (++p->timer>=100) { p->timer=0; p->dir=(signed char)-p->dir; }
        break;
    case PLATFORM_KIND_BOULDER:      /* $B086 */
        /* $B242 streams these in on their tile trigger; with every record
           instantiated at level load instead, the boulder waits above its own
           column until the camera reaches it, which is the same moment. */
        if (p->state==PLATFORM_STATE_IDLE) {
            int sx=(int)(p->x>>FIX_SHIFT)-g_camera_x;
            if (sx>SCREEN_W+96 || sx<-96) break;
            p->state=PLATFORM_STATE_FALL; p->timer=0; p->step=0;
        }
        if (p->timer==0) {
            p->y+=8*FIX_ONE;
            if ((int)(p->y>>FIX_SHIFT)>=BOULDER_LAND_Y) p->timer=1;
            break;
        }
        p->y+=(s32)g_boulder_arc[p->step]<<FIX_SHIFT;
        if (p->step<BOULDER_ARC_LEN-1) ++p->step;
        if ((int)(p->y>>FIX_SHIFT)>=BOULDER_WRAP_Y) {
            p->y=(s32)BOULDER_TOP_Y<<FIX_SHIFT;
            p->x+=23*FIX_ONE;
            p->timer=0; p->step=0;
        }
        /* $B0F0 rolls it one pixel left on even frames only. */
        if ((g_state_frame&1)==0) p->x-=FIX_ONE;
        /* The rock is a hazard: touching it costs a heart, on the same
           invulnerability window every other contact damage uses. */
        if (g_invuln==0) {
            const unsigned short *sh=platform_shape(p);
            int bw=sh?(int)sh[5]:32, bh=sh?(int)sh[6]:32;
            if (aabb(player_body_x(),player_body_y(),PLAYER_BODY_W,PLAYER_BODY_H,
                     platform_draw_x(p),platform_draw_y(p),bw,bh))
                hurt_player();
        }
        break;
    case PLATFORM_KIND_DIAMOND:      /* $FF3A */
        p->x+=(p->state==0||p->state==1)?FIX_ONE:-FIX_ONE;
        p->y+=(p->state==0||p->state==3)?FIX_ONE:-FIX_ONE;
        if (++p->timer>=48) { p->timer=0; p->state=(unsigned char)((p->state+1)&3); }
        break;
    default:
        break;
    }
}

static void update_moving_platforms(void)
{
    int i;
    /* $1077C/$105D4/$106C6 all gate their one-pixel steps on bit 0 of the
       global frame counter, so every level-8 carrier moves at half speed. */
    int half=((g_state_frame&1)==0);
    for (i=0;i<g_moving_platform_count;++i) {
        MovingPlatform *p=&g_moving_platforms[i];
        s32 old_y=p->y,old_x=p->x;
        if (PLATFORM_KIND_IS_STAGE_OBJECT(p->kind)) {
            update_stage_object(p,i);
        } else if (p->kind==PLATFORM_KIND_LIFT) {
            p->y+=(s32)p->dir*(s32)p->speed*FIX_ONE;
            if ((int)(p->y>>FIX_SHIFT)<=p->min_y) {
                p->y=(s32)p->min_y<<FIX_SHIFT; p->dir=1;
            } else if ((int)(p->y>>FIX_SHIFT)>=p->max_y) {
                p->y=(s32)p->max_y<<FIX_SHIFT; p->dir=-1;
            }
        } else {
            int sx=(int)(p->x>>FIX_SHIFT)-g_camera_x;
            /* Off-camera records return to their table position; the source
               respawns the object when its tile trigger is crossed again. */
            if (p->state!=PLATFORM_STATE_IDLE && (sx<-192 || sx>448)) {
                if (g_on_moving_platform==i) g_on_moving_platform=-1;
                p->x=p->home_x; p->y=p->home_y;
                p->state=PLATFORM_STATE_IDLE; p->timer=0; p->dir=1;
            } else if (p->kind==PLATFORM_KIND_BRICK) {
                if (half) {
                    p->y+=(s32)p->dir*FIX_ONE;
                    if (++p->timer>=32) { p->timer=0; p->dir=(signed char)-p->dir; }
                }
            } else if (p->state==PLATFORM_STATE_IDLE) {
                /* Handlers 14/15 stay parked until a player stands on them. */
                if (g_on_moving_platform==i) { p->state=PLATFORM_STATE_SLIDE; p->timer=0; }
            } else if (p->state==PLATFORM_STATE_SLIDE) {
                int span=(p->kind==PLATFORM_KIND_WEDGE_R)?48:32;
                if (half) {
                    p->y+=FIX_ONE;
                    p->x+=(p->kind==PLATFORM_KIND_WEDGE_R)?FIX_ONE:-FIX_ONE;
                    if (++p->timer>=span) { p->timer=0; p->state=PLATFORM_STATE_FALL; }
                }
            } else {
                p->y+=2*FIX_ONE;
                if ((int)(p->y>>FIX_SHIFT)>=352) {
                    if (g_on_moving_platform==i) { g_on_moving_platform=-1; g_grounded=0; }
                    p->x=p->home_x; p->y=p->home_y;
                    p->state=PLATFORM_STATE_IDLE; p->timer=0;
                }
            }
        }
        if (g_on_moving_platform==i && g_grounded) {
            g_player_y+=p->y-old_y;
            g_player_x+=p->x-old_x;
        }
    }
}

static void init_objects(void)
{
    int i,ycrop=(!g_in_boss&&g_current_level==2)?8:0;
    for(i=0;i<MAX_CRATES;++i){g_crates[i].active=0;g_crates[i].carried=0;g_crates[i].thrown=0;}
    for(i=0;i<MAX_GEMS;++i)g_gems[i].active=0;
    for(i=0;i<MAX_ENEMIES;++i)g_enemies[i].active=0;
    for(i=0;i<4;++i)g_enemy_shots[i].active=0;
    if(!g_in_boss && g_level){
        for(i=0;i<g_level->crate_count && i<MAX_CRATES;++i){
            int fx=g_level->crates[i*3];
            g_crates[i].x=(s32)world_x_from_flat(fx)<<FIX_SHIFT;
            g_crates[i].y=(s32)world_y_from_flat(fx,g_level->crates[i*3+1]-ycrop)<<FIX_SHIFT;
            g_crates[i].vx=g_crates[i].vy=0;g_crates[i].extra=(unsigned char)g_level->crates[i*3+2];g_crates[i].active=1;g_crates[i].flight_timer=0;
        }
        for(i=0;i<g_level->gem_count && i<MAX_GEMS;++i){
            int fx=g_level->gems[i*2];
            g_gems[i].x=(short)world_x_from_flat(fx);
            g_gems[i].y=(short)world_y_from_flat(fx,g_level->gems[i*2+1]-ycrop);
            g_gems[i].active=1;
        }
        for(i=0;i<g_level->enemy_count && i<MAX_ENEMIES;++i){
            const short *e=&g_level->enemies[i*ENEMY_RECORD_WORDS];
            g_enemies[i].x=(s32)world_x_from_flat(e[0])<<FIX_SHIFT;
            g_enemies[i].y=(s32)world_y_from_flat(e[0],e[1]-ycrop)<<FIX_SHIFT;
            g_enemies[i].vx=-FIX_ONE;g_enemies[i].vy=0;
            g_enemies[i].variant=(unsigned char)e[2];g_enemies[i].state=(unsigned char)e[3];g_enemies[i].flags=(unsigned char)e[4];g_enemies[i].kind=(unsigned char)e[7];
            g_enemies[i].width=(unsigned char)e[5];g_enemies[i].height=(unsigned char)e[6];g_enemies[i].active=1;g_enemies[i].spawned=0;g_enemies[i].defeated=0;
            /* Flying Koopas and CHIP use the terrain-independent patrol.
               State-5 pterodactyls retain their separate $C472 behavior. */
            g_enemies[i].flying=(e[7]==ENEMY_KIND_FLYING_KOOPA||e[7]==ENEMY_KIND_CHIP);
            g_enemies[i].flight_phase=0;
            g_enemies[i].behavior_phase=0;g_enemies[i].behavior_timer=0;
            g_enemies[i].face_right=0;g_enemies[i].shot_slot=0;
#ifdef FORCE_ENEMY_ACTION
            if(g_enemies[i].state==2||g_enemies[i].state==4||g_enemies[i].state==5||g_enemies[i].state==6||g_enemies[i].state==7){
                g_enemies[i].behavior_phase=1;g_enemies[i].behavior_timer=40;
            }
#endif
        }
    }
    init_moving_platforms();
    g_carry_index=-1;g_lift_timer=0;g_gem_count=0;g_invuln=0;
    g_crate_hide=0;
    for(i=0;i<MAX_FIREBALLS;++i)g_fireballs[i].active=0;
    for(i=0;i<MAX_ITEMS;++i)g_items[i].active=0;
    g_enemy_freeze=0;
}

static void update_crates(void)
{
    int i;
    for (i=0;i<MAX_CRATES;++i) {
        Crate *c=&g_crates[i];
        int x,y,bottom,old_bottom,crate_top;
        if (!c->active) continue;
        if (c->carried) {
            c->x=g_player_x+(s32)4*FIX_ONE;
            /* The disguised 24px crate rests on Mario's standing surface
               (anchor + 24), rather than floating six pixels above it. */
            c->y=g_player_y+(s32)(g_crate_hide?24:-13)*FIX_ONE;
            c->vx=c->vy=0;
            c->thrown=0;
            continue;
        }

        if (c->thrown) {
            int sx,sy;
            /* Source-style thrown boxes are screen attacks: no terrain
               collision, no gravity and no impact response. They may pass
               through walls and defeated enemies and only vanish after they
               have completely left the visible 256-pixel playfield. */
            c->x+=c->vx;c->y+=c->vy;
            sx=(int)(c->x>>FIX_SHIFT)-g_camera_x;
            sy=(int)(c->y>>FIX_SHIFT)-g_camera_y;
            if ((c->vx>0 && sx>256) || (c->vx<0 && sx+CRATE_W<0) ||
                sy+CRATE_H<0 || sy>240)
                c->active=0;
            continue;
        }

        x=(int)(c->x>>FIX_SHIFT);
        if (x<g_camera_x-48 || x>g_camera_x+320) continue;
        c->vx=0;
        c->x=(c->x>>FIX_SHIFT)<<FIX_SHIFT;
        y=(int)(c->y>>FIX_SHIFT);
        old_bottom=y+CRATE_H-1;
        c->vy+=80;
        if (c->vy>5*FIX_ONE) c->vy=5*FIX_ONE;
        c->y+=c->vy;
        x=(int)(c->x>>FIX_SHIFT); y=(int)(c->y>>FIX_SHIFT);
        bottom=y+CRATE_H-1;
        crate_top=0x7FFF;
        if (c->vy>=0) {
            int j;
            /* Resting and falling crates are solid supports for other crates.
               Test the fall sweep rather than just the final position so a
               box cannot skip through another box at terminal velocity. */
            for (j=0;j<MAX_CRATES;++j) {
                Crate *support=&g_crates[j];
                int sx,sy;
                if (support==c || !support->active || support->carried || support->thrown) continue;
                sx=(int)(support->x>>FIX_SHIFT); sy=(int)(support->y>>FIX_SHIFT);
                if (x+CRATE_W-3<=sx || x+3>=sx+CRATE_W) continue;
                if (old_bottom<=sy+1 && bottom>=sy && sy<crate_top) crate_top=sy;
            }
        }
        if (c->vy>=0 && (solid_v(x+3,bottom)||solid_v(x+CRATE_W-4,bottom))) {
            int top=(bottom>>3)<<3;
            if (crate_top<top) top=crate_top;
            c->y=(s32)(top-CRATE_H)<<FIX_SHIFT;
            c->vy=0;
        } else if (crate_top!=0x7FFF) {
            c->y=(s32)(crate_top-CRATE_H)<<FIX_SHIFT;
            c->vy=0;
        }
    }
}

static int find_pickup_crate(void)
{
    int i;
    int px = player_body_x();
    int py = player_body_y();
    int best = -1, bestd = 9999;
    for (i = 0; i < MAX_CRATES; ++i) {
        int cx, cy, d;
        if (!g_crates[i].active || g_crates[i].carried) continue;
        cx = (int)(g_crates[i].x >> FIX_SHIFT);
        cy = (int)(g_crates[i].y >> FIX_SHIFT);
        if (!aabb(px - 20, py - 8, PLAYER_BODY_W + 40, PLAYER_BODY_H + 16,
                  cx, cy, CRATE_W, CRATE_H)) continue;
        d = iabs_i((cx + 12) - (px + 8));
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

/* $1756E: throwing a carried crate frees whatever the crate record's tenth
   byte named into the four-slot item pool, at the crate's own position nudged
   right 4 and down 8.  The source then destroys the carried crate object and
   rebuilds it as the player's thrown projectile, so a crate can only ever give
   up its contents once; here the crate survives the throw, and clearing extra
   is what keeps a single box from being farmed for ammo.

   A crate is carried at chest height, so that spawn point lands inside the
   player's own 28x44 pickup box: collected on the very frame it appears, the
   item was never drawn once.  The item is therefore released a little above
   the crate and dropped (see update_items), which is what puts it on screen
   and lets it be walked into where it lands.

   Boss rooms are excluded because their crate slots reuse extra to carry the
   boss class for the pot/bomb/bell variants rather than an item id. */
static void spawn_crate_item(Crate *c)
{
    int i;
    if (g_in_boss || !c->extra || c->extra > ITEM_KIND_COUNT) return;
    for (i = 0; i < MAX_ITEMS; ++i) {
        if (g_items[i].active) continue;
        g_items[i].x = c->x + (s32)4*FIX_ONE;
        g_items[i].y = c->y - (s32)12*FIX_ONE;
        g_items[i].vy = -(s32)3*FIX_ONE;
        g_items[i].grounded = 0;
        g_items[i].kind = c->extra;
        g_items[i].active = 1;
        c->extra = 0;
        return;
    }
    /* Pool full: the source leaves the contents in place, so the crate keeps
       them for a later throw. */
}

static void do_crate_action(void)
{
    if (g_carry_index >= 0) {
        Crate *c = &g_crates[g_carry_index];
        spawn_crate_item(c);
        c->carried = 0;
        if (read_buttons()&JOY_UP) {
            c->x=g_player_x+(s32)4*FIX_ONE;c->y=g_player_y-(s32)8*FIX_ONE;
            c->vx=0;c->vy=-6*FIX_ONE;
        } else if (read_buttons()&JOY_DOWN) {
            c->x=g_player_x+(s32)4*FIX_ONE;c->y=g_player_y+(s32)34*FIX_ONE;
            c->vx=0;c->vy=6*FIX_ONE;
        } else {
            c->x=g_player_x+(s32)(g_facing>0?18:-14)*FIX_ONE;
            c->y=g_player_y+(s32)26*FIX_ONE;
            c->vx=(s32)g_facing*6*FIX_ONE;c->vy=0;
        }
        c->thrown = 1;
        c->flight_timer = 0;
        g_carry_index = -1;
        g_lift_timer = 0;
        g_throw_timer = 31;
        /* Ground throws lock horizontal control; airborne throws preserve
           Mario's current air control and therefore must not pre-emptively
           zero velocity/camera here. update_play() applies the ground lock. */
        Play_Sound_Sample(SFX_CRATE_THROW,0,0);
    } else {
        int i = find_pickup_crate();
        if (i >= 0) {
            g_carry_index = i;
            g_crates[i].carried = 1;
            g_crates[i].thrown = 0;
            g_lift_timer = 18;
            g_vx = 0;
        }
    }
}

static void fire_fireball(void)
{
    int i;
    Fireball *f=0;
    if ((!g_debug_mode && g_fireball_ammo<=0) || g_player_mode!=PMODE_NORMAL) return;
    for (i=0;i<MAX_FIREBALLS;++i) if (!g_fireballs[i].active) { f=&g_fireballs[i]; break; }
    if (!f) return;
    if (!g_debug_mode) --g_fireball_ammo;
    f->active=1;
    f->x=g_player_x+(s32)(g_facing>0?32:-24)*FIX_ONE;
    f->y=g_player_y+(s32)16*FIX_ONE;
    f->vx=(s32)g_facing*7*FIX_ONE;
    f->life=52;
    g_throw_timer=31;
    /* As with a crate throw, air firing keeps normal horizontal control. */
    Play_Sound_Sample(SFX_FIREBALL,0,0);
}

static void defeat_enemy(Enemy *e)
{
    if (!e->active || e->defeated) return;
    e->defeated=1;
    e->vx=0;
    e->vy=FIX_ONE;
}

static void update_fireballs(void)
{
    int i,j;
    for (i=0;i<MAX_FIREBALLS;++i) {
        Fireball *f=&g_fireballs[i];
        int x,y;
        if (!f->active) continue;
        f->x+=f->vx;
        x=(int)(f->x>>FIX_SHIFT); y=(int)(f->y>>FIX_SHIFT);
        if ((f->vx>0 && solid_h(x+23,y+12)) || (f->vx<0 && solid_h(x,y+12)) ||
            x<g_camera_x-32 || x>g_camera_x+288 || f->life==0) {
            f->active=0; continue;
        }
        --f->life;
        for (j=0;j<MAX_ENEMIES;++j) {
            Enemy *e=&g_enemies[j];
            int ex,ey,eh;
            if (!e->active || !e->spawned || e->defeated) continue;
            ex=(int)(e->x>>FIX_SHIFT); ey=(int)(e->y>>FIX_SHIFT); eh=e->height;
            if (aabb(x+4,y+5,16,14,ex+3,ey+3,18,eh-5)) {
                /* Fire travels through a target after defeating it. */
                defeat_enemy(e);
            }
        }
    }
}

static void update_gems(void)
{
    int i;
    int px = player_body_x();
    int py = player_body_y();
    for (i = 0; i < MAX_GEMS; ++i) {
        if (!g_gems[i].active) continue;
        if (aabb(px, py, PLAYER_BODY_W, PLAYER_BODY_H,
                 g_gems[i].x, g_gems[i].y, GEM_W, GEM_H)) {
            g_gems[i].active = 0;
            ++g_gem_count;
            Play_Sound_Sample(SFX_BLUE_CRYSTAL,0,0);
        }
    }
}

static void begin_player_death(void)
{
    if (g_debug_mode) return;
    if (g_player_mode!=PMODE_NORMAL) return;
    if (g_carry_index>=0) {
        Crate *c=&g_crates[g_carry_index];
        c->carried=0; c->thrown=0; c->vx=c->vy=0;
        g_carry_index=-1;
    }
    g_jump_active=0;
    g_bounce_active=0;
    g_grounded=0;
    g_player_mode=PMODE_DYING;
    g_death_timer=0;
    g_invuln=0;
    g_hurt_pending_death=0;
    g_vx=0;
    g_vy=-(6*FIX_ONE+FIX_ONE/2);
    /* A retry must begin its level cue at index 00, never at the position the
       player died.  Start the fade while the death animation is still shown. */
    music_stop_for_transition();
    Play_Sound_Sample(SFX_PLAYER_DEATH,0,0);
}

static void hurt_player(void)
{
    if (g_debug_mode) return;
    /* Holding DOWN while carrying enters the source box-disguise state: the
       crate-with-eyes is the only visible body and attacks pass through it. */
    if (g_crate_hide && g_carry_index>=0) return;
    if (g_player_mode!=PMODE_NORMAL || g_invuln>0) return;
    if (g_player_hearts>0) --g_player_hearts;
    g_invuln=HURT_FRAMES;
    g_hurt_pending_death=(g_player_hearts<=0);
    /* Source behavior is a one-second hurt interval rather than immediate
       death. */
    Play_Sound_Sample(SFX_PLAYER_HURT,0,0);
}

/* $15AB0 dispatches on the item id through the jump table at $15B96.  Sound
   $9C -- the port's SFX_MENU_CONFIRM -- is the shared collect chime played by
   the caller; the $B0 jingle the life and ammo entries add on top of it has no
   capture in the ADPCM bank, so those two are left with the chime alone. */
static void apply_item(unsigned char kind)
{
    switch (kind) {
    case ITEM_FULL_HEAL:
        g_player_hearts = PLAYER_MAX_HEARTS;
        break;
    case ITEM_FIRST_AID:
        if (g_player_hearts < PLAYER_MAX_HEARTS) ++g_player_hearts;
        break;
    case ITEM_POISON:
        /* The one harmful box.  $15AF4 spends a heart with the ordinary hurt
           sound and kills outright at zero, granting no mercy invulnerability
           the way an enemy hit does. */
        if (g_player_hearts > 0) --g_player_hearts;
        Play_Sound_Sample(SFX_PLAYER_HURT,0,0);
        if (g_player_hearts <= 0) g_hurt_pending_death = 1;
        break;
    case ITEM_EXTRA_LIFE:
        if (g_lives < 9) ++g_lives;
        break;
    case ITEM_STOPWATCH:
        g_enemy_freeze = STOPWATCH_FRAMES;
        break;
    case ITEM_SHIELD:
        g_invuln = SHIELD_FRAMES;
        break;
    case ITEM_FIREBALL:
        if (g_fireball_ammo < 9) ++g_fireball_ammo;
        break;
    default:
        break;
    }
}

/* $15984 tests the item pool against the player's whole 28x44 object box, not
   the narrower body rectangle the port uses for terrain and enemies.

   The drop is the port's own: a freed item pops out of the crate and falls
   under the same gravity a loose crate uses until it rests on the floor.  It
   is only collectable once it has landed, so the pickup always plays out on
   screen instead of being swallowed on its first frame, and an item that
   lands away from the player waits there to be walked into. */
static void update_items(void)
{
    int i;
    int px = (int)(g_player_x >> FIX_SHIFT) + 4;
    int py = (int)(g_player_y >> FIX_SHIFT);
    if (g_enemy_freeze > 0) --g_enemy_freeze;
    for (i = 0; i < MAX_ITEMS; ++i) {
        Item *it = &g_items[i];
        int x, y, bottom;
        if (!it->active) continue;
        if (!it->grounded) {
            it->vy += 48;
            if (it->vy > 5*FIX_ONE) it->vy = 5*FIX_ONE;
            it->y += it->vy;
            x = (int)(it->x >> FIX_SHIFT); y = (int)(it->y >> FIX_SHIFT);
            bottom = y + ITEM_H - 1;
            if (y > g_camera_y+240) { it->active = 0; continue; }
            if (it->vy >= 0 && (solid_v(x+3,bottom) || solid_v(x+ITEM_W-4,bottom))) {
                it->y = (s32)(((bottom>>3)<<3) - ITEM_H) << FIX_SHIFT;
                it->vy = 0;
                it->grounded = 1;
            }
            continue;
        }
        if (g_player_mode != PMODE_NORMAL) continue;
        if (!aabb(px, py, 28, 44,
                  (int)(it->x >> FIX_SHIFT) + 4, (int)(it->y >> FIX_SHIFT),
                  ITEM_W - 4, ITEM_H - 4)) continue;
        it->active = 0;
        Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
        apply_item(it->kind);
    }
}

/* A resting crate is terrain for ordinary level enemies.  Carried crates have
   no world body, and a thrown crate keeps its projectile behavior below. */
static int enemy_crate_blocked(const Enemy *e, s32 next_x, int y)
{
    int i;
    int x=(int)(next_x>>FIX_SHIFT);
    for (i=0;i<MAX_CRATES;++i) {
        const Crate *c=&g_crates[i];
        int cx,cy;
        if (!c->active || c->carried || c->thrown) continue;
        cx=(int)(c->x>>FIX_SHIFT);
        cy=(int)(c->y>>FIX_SHIFT);
        if (aabb(x,y+3,e->width,e->height-6,cx,cy,CRATE_W,CRATE_H))
            return 1;
    }
    return 0;
}

/* Resting crates are walkable floors as well as side obstacles.  Keeping the
   top contact separate prevents a falling enemy from treating its landing as
   a side collision and endlessly turning in place. */
static int enemy_crate_floor_top(const Enemy *e, int x, int old_bottom,
                                 int new_bottom)
{
    int i,best=0x7FFF;
    for (i=0;i<MAX_CRATES;++i) {
        const Crate *c=&g_crates[i];
        int cx,cy;
        if (!c->active || c->carried || c->thrown) continue;
        cx=(int)(c->x>>FIX_SHIFT);
        cy=(int)(c->y>>FIX_SHIFT);
        if (x+e->width-3<=cx || x+3>=cx+CRATE_W) continue;
        if (old_bottom<=cy+1 && new_bottom>=cy && cy<best) best=cy;
    }
    return best;
}

static void update_enemies(void)
{
    int i;
    int px=player_body_x();
    int py=player_body_y();
    int old_py=g_prev_body_y;

    for (i=0;i<MAX_ENEMIES;++i) {
        Enemy *e=&g_enemies[i];
        int x,y,h,bottom;

        if (!e->active) continue;
        x=(int)(e->x>>FIX_SHIFT);
        y=(int)(e->y>>FIX_SHIFT);
        h=e->height;

        if (e->defeated) {
            /* Defeated enemies remain visible and simply drop out of the
               raster. They no longer collide with terrain/player. */
            e->vy+=48;
            if (e->vy>6*FIX_ONE) e->vy=6*FIX_ONE;
            e->y+=e->vy;
            if ((int)(e->y>>FIX_SHIFT)>g_camera_y+256) e->active=0;
            continue;
        }

        /* The source only advances encounter objects close to the raster.
           Keep the same generous pre-roll: 48 px left, 64 px right. */
        if (!e->spawned) {
            /* Every other record is authored at object X $180, which is the
               right edge when the streamer's tile trigger fires, so the shared
               pre-roll matches the source.  The rocks are authored at $D8..$E8
               -- screen 88..104, the middle of the view -- because they are
               meant to come down into the hole the player is walking up to.
               Starting them on the 320-pixel pre-roll dropped them some 200
               pixels of camera travel early: they had bounced away east and
               been retired by $130C6 before the hole was ever on screen, which
               is why none of level 10's five rocks were ever seen. */
            if (x>g_camera_x+((e->kind==ENEMY_KIND_BOULDER)?112:320)) continue;
            e->spawned=1;
        }
        if (x<g_camera_x-48 || x>g_camera_x+320) continue;
        /* Stacked levels repeat the same world X on every storey, so an
           enemy two screens up is horizontally in range while being nowhere
           near the view. */
        if (g_level_rows>1 && (y<g_camera_y-96 || y>g_camera_y+336)) continue;

        /* The stopwatch item skips $B6E0's behavior dispatch and nothing else,
           so a frozen enemy still collides: it can be stomped, hit by a thrown
           crate, and it still hurts on contact.  The defeated-drop above sits
           ahead of the source's FF00A6 test as well and keeps running. */
        if (g_enemy_freeze) goto enemy_attack_checks;

        if(e->kind!=ENEMY_KIND_GENERIC&&e->kind!=ENEMY_KIND_CHIP){
            int near=iabs_i(px-x);
            if(e->kind==ENEMY_KIND_MONKEY){
                e->vx=e->vy=0;
                /* State 7, ROM $C166.  The source bounds $90..$E0 map to
                   8..88 in the extracted 240-line stage coordinates.  Keep
                   direction in flight_phase: recomputing it at an endpoint
                   made the old approximation alternate 95/96 forever. */
                if(e->behavior_phase==0){
                    if(++e->behavior_timer>=63){e->behavior_phase=1;e->behavior_timer=0;}
                }else if(e->behavior_phase==1){
                    e->behavior_timer=(unsigned char)(30+(g_state_frame&63));
                    e->flight_phase=(unsigned char)((g_state_frame>>2)&1);
                    e->behavior_phase=2;
                }else if(e->behavior_phase==2){
                    if(e->flight_phase){
                        e->y-=FIX_ONE;
                        if((int)(e->y>>FIX_SHIFT)<=8){e->y=(s32)8<<FIX_SHIFT;e->flight_phase=0;}
                    }else{
                        e->y+=FIX_ONE;
                        if((int)(e->y>>FIX_SHIFT)>=88){e->y=(s32)88<<FIX_SHIFT;e->flight_phase=1;}
                    }
                    if(--e->behavior_timer==0){
                        int k;e->behavior_phase=2;e->behavior_timer=0;
                        for(k=0;k<4;++k)if(!g_enemy_shots[k].active){
                            EnemyShot *s=&g_enemy_shots[k];s->active=1;s->x=e->x+16*FIX_ONE;s->y=e->y+24*FIX_ONE;
                            s->vx=(px<x)?-2*FIX_ONE:2*FIX_ONE;s->vy=-2*FIX_ONE;s->palette=g_enemy_palette_line[e->variant];
                            s->base=ENEMY_BANANA_BASE_WORD;s->cols=1;s->mode=SHOT_MODE_ARC;s->step=0;break;
                        }
                        e->behavior_phase=3;
                    }
                }else if(++e->behavior_timer>=25){e->behavior_phase=0;e->behavior_timer=0;}
                y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_PTERODACTYL){
                e->vx=e->vy=0;
                /* State 5, ROM $C472: wake once Mario reaches x-70, home at
                   one pixel per axis for two 144-frame legs, then escape
                   diagonally up-left. */
                if(e->behavior_phase==0&&px>=x-70){e->behavior_phase=1;e->behavior_timer=0;}
                if(e->behavior_phase==1||e->behavior_phase==2){
                    if(px<x){e->x-=FIX_ONE;e->face_right=0;}
                    else if(px>x){e->x+=FIX_ONE;e->face_right=1;}
                    if(py<y)e->y-=FIX_ONE;else if(py>y)e->y+=FIX_ONE;
                    if(++e->behavior_timer>=144){
                        e->behavior_timer=0;
                        ++e->behavior_phase;
                    }
                }else if(e->behavior_phase>=3){
                    e->x-=FIX_ONE;e->y-=FIX_ONE;
                }
                x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_STRIKE_SNAKE){ /* hold, rear up, then long strike */
                e->vx=e->vy=0;
                if(e->behavior_phase==0&&near<=72){e->behavior_phase=1;e->behavior_timer=0;}
                else if(e->behavior_phase&&++e->behavior_timer>=72){e->behavior_phase=0;e->behavior_timer=0;}
                goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_CRATE_MIMIC){ /* reveal, pause, then walk */
#ifdef FORCE_CRATE_STAND
                e->behavior_phase=1;e->behavior_timer=0;e->vx=0;
#else
                if(e->behavior_phase==0){
                    e->vx=0;
                    if(near<=60){e->behavior_phase=1;e->behavior_timer=0;}
                }
                else if(e->behavior_phase==1&&++e->behavior_timer>=24){e->behavior_phase=2;e->behavior_timer=0;e->vx=-FIX_ONE;}
#endif
                /* Its reveal state only controls the animation.  Once it is
                   awake it deliberately falls through to the normal enemy
                   solver, so it walks left/right, lands on terrain and turns
                   at walls/crates just like a Goomba instead of hovering at
                   its authored Y coordinate. */
            }else if(e->kind==ENEMY_KIND_THROWER){
                /* State 8, ROM $C0C6.  This one never walks: it stands for 96
                   frames, turns to face the player, plays the attack pose
                   while $C25C carries its rock, then recovers for 48 more.
                   The port used to run it through the generic Goomba solver,
                   which is why it wandered off its authored spot.

                   Its attack script at $D244 is two cels, not one: $E1 for
                   15 ticks of wind-up, $F9 for 7 while the rock leaves the
                   hand, then $E1 again.  Phase 1 is that wind-up, so the
                   throw now lands on the second cel the way the ROM does. */
                e->vx=e->vy=0;
                if(e->behavior_phase==0){
                    if(++e->behavior_timer>=96){
                        e->behavior_timer=0;e->behavior_phase=1;
                        e->face_right=(unsigned char)(px>=x);
                    }
                }else if(e->behavior_phase==1){
                    if(++e->behavior_timer>=THROWER_WINDUP_TICKS){
                        e->behavior_timer=0;e->behavior_phase=2;
                        e->shot_slot=(unsigned char)(throw_enemy_shot(e,18,0,6,SHOT_MODE_ROCK)+1);
                    }
                }else if(e->behavior_phase==2){
                    /* $C2BC waits for the projectile slot to clear. */
                    if(e->behavior_timer<255)++e->behavior_timer;
                    if(!e->shot_slot||!g_enemy_shots[e->shot_slot-1].active){
                        e->shot_slot=0;e->behavior_phase=3;e->behavior_timer=0;
                    }
                }else if(++e->behavior_timer>=48){e->behavior_phase=0;e->behavior_timer=0;}
                y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_SPEARMAN){
                /* State 13, ROM $BAD6.  Unlike the thrower this one really
                   does patrol, so it keeps the generic terrain solver below
                   and only stops while $BB98 has a spear in flight. */
                if(e->behavior_phase==0){
                    if(e->behavior_timer==0)e->behavior_timer=(unsigned char)(30+(g_state_frame&127));
                    if(--e->behavior_timer==0){
                        e->behavior_phase=1;e->vx=0;
                        e->face_right=(unsigned char)(px>=x);
                        e->shot_slot=(unsigned char)(throw_enemy_shot(e,8,0,2,SHOT_MODE_SPEAR)+1);
                    }
                }else{
                    e->vx=0;
                    if(e->behavior_timer<255)++e->behavior_timer;
                    if(!e->shot_slot||!g_enemy_shots[e->shot_slot-1].active){
                        e->shot_slot=0;e->behavior_phase=0;e->behavior_timer=0;
                        e->vx=e->face_right?FIX_ONE:-FIX_ONE;
                    }
                    y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
                }
            }else if(e->kind==ENEMY_KIND_FLOOR_SNAKE){
                /* $C62C.  The fish waits sixteen ticks in the water, then
                   walks the fifty-one-entry arc at $C6E0 one step a frame --
                   switching to animation state 1 on the second step and to the
                   splash state on the last -- and $1312A finally clears its
                   object slot.  The port used to hold it perfectly still at
                   its authored spot and only swap cels when Mario came within
                   64 pixels, which is why it never jumped.

                   Clearing the slot is not the end of the fish.  The spawn
                   loop at $D4D4 walks the level's record list every frame and
                   re-fills any *empty* slot whose camera-tile window it is
                   standing in, and every fish record names a window one to
                   three tiles wide with the fish 160 pixels right of the
                   camera -- barely more than Mario's own dead-zone offset.  So
                   the source's fish rests, leaps, sinks, and is rebuilt from
                   scratch to rest and leap again for exactly as long as Mario
                   stays beside it.  Cycling back to the idle phase here is
                   that loop, with the range test standing in for the window. */
                e->vx=e->vy=0;
                if(e->behavior_phase==0){
                    /* The source has no waiting fish at all: outside the
                       window there is no object, and $C630's sixteen ticks are
                       the whole of its life before the leap.  Those ticks are
                       state 0, which surfaces -- flat cel for four, then the
                       rising cel -- so run them as the approach, and let the
                       fish lie flat in the water until Mario is in range. */
                    if(near>FISH_LEAP_RANGE)e->behavior_timer=0;
                    else if(++e->behavior_timer>=FISH_LEAP_DELAY){
                        e->behavior_phase=1;e->behavior_timer=0;
                    }
                }else if(e->behavior_phase==1){
                    e->y+=(s32)g_fish_leap_dy[e->behavior_timer]*FIX_ONE;
                    if(++e->behavior_timer>=FISH_LEAP_STEPS){e->behavior_phase=2;e->behavior_timer=0;}
                }else if(++e->behavior_timer>=FISH_LEAP_DELAY){
                    e->behavior_phase=0;e->behavior_timer=0;
                }
                y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_BOULDER){
                /* $B844.  The rock is authored above the ceiling and drops at
                   eight pixels a frame until $17D50 reports ground under it.
                   It then runs three shrinking bounce arcs -- $BA7A, $B918,
                   $B95A -- rolling two pixels right on every step of them,
                   and afterwards just keeps rolling, falling four pixels a
                   frame whenever nothing is beneath it.  The port used to
                   hand it to the Goomba solver, which left it hanging in the
                   air on one frame of its four-cel rotation. */
                int bottom;
                e->vx=e->vy=0;
                if(e->behavior_phase==0){
                    e->y+=(s32)8*FIX_ONE;
                    y=(int)(e->y>>FIX_SHIFT);bottom=y+h-1;
                    if(solid_v(x+3,bottom)||solid_v(x+e->width-4,bottom)){
                        e->y=(s32)(((bottom>>3)<<3)-h)<<FIX_SHIFT;
                        e->behavior_phase=1;e->behavior_timer=0;
                    }
                }else if(e->behavior_phase<4){
                    const signed char *arc=(e->behavior_phase==1)?g_boulder_bounce1:
                                           (e->behavior_phase==2)?g_boulder_bounce2:g_boulder_bounce3;
                    int len=(e->behavior_phase==1)?45:(e->behavior_phase==2)?32:19;
                    if(e->behavior_timer>=len){++e->behavior_phase;e->behavior_timer=0;}
                    else{
                        e->y+=(s32)arc[e->behavior_timer]*FIX_ONE;
                        e->x+=(s32)2*FIX_ONE;++e->behavior_timer;
                    }
                }else{
                    e->x+=(s32)2*FIX_ONE;
                    x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);bottom=y+h-1;
                    if(!solid_v(x+3,bottom+4)&&!solid_v(x+e->width-4,bottom+4))
                        e->y+=(s32)4*FIX_ONE;
                }
                x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
                /* $130C6 retires it once it has rolled past the view. */
                if(x-g_camera_x>320||y>g_camera_y+256)e->active=0;
                goto enemy_attack_checks;
            }
        }

        if (e->flying) {
            /* Handler $B794 moves left and consumes one signed entry from
               the exact 58-step ROM path on every update. */
            e->x-=FIX_ONE;
            e->y+=(s32)g_flying_koopa_dy[e->flight_phase]*FIX_ONE;
            if(++e->flight_phase>=58)e->flight_phase=0;
            x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
            /* $B794 never calls the terrain probes: the predetermined patrol
               deliberately passes through stage tiles. */
            goto enemy_attack_checks;
        }

        e->vy+=64;
        if (e->vy>4*FIX_ONE) e->vy=4*FIX_ONE;

        /* Test horizontal collision before committing the step so reversing
           at a wall does not visibly snap one pixel back and forth. */
        {
            s32 nx=e->x+e->vx;
            int nxi=(int)(nx>>FIX_SHIFT);
            int side=e->vx>0?nxi+e->width-1:nxi;
            y=(int)(e->y>>FIX_SHIFT);
            if (solid_h(side,y+8)||solid_h(side,y+h-3) ||
                enemy_crate_blocked(e,nx,y))
                e->vx=-e->vx;
            else
                e->x=nx;
        }

        {
            int old_bottom=y+h-1;
            e->y+=e->vy;
            x=(int)(e->x>>FIX_SHIFT);
            y=(int)(e->y>>FIX_SHIFT);
            bottom=y+h-1;
            {
                int top=0x7FFF;
                int crate_top=enemy_crate_floor_top(e,x,old_bottom,bottom);
                if (e->vy>=0 && (solid_v(x+3,bottom)||solid_v(x+e->width-4,bottom)))
                    top=(bottom>>3)<<3;
                if (crate_top<top) top=crate_top;
                if (top!=0x7FFF) {
                    e->y=(s32)(top-h)<<FIX_SHIFT;
                    e->vy=0;
                    y=top-h;
                }
            }
        }

enemy_attack_checks:
        /* A thrown crate is the single-hit projectile: it defeats one enemy
           and disappears. Fireballs deliberately differ and pass through. */
        {
            int c;
            for (c=0;c<MAX_CRATES;++c) {
                int cx,cy;
                Crate *cr=&g_crates[c];
                if (!cr->active || cr->carried || !cr->thrown) continue;
                cx=(int)(cr->x>>FIX_SHIFT); cy=(int)(cr->y>>FIX_SHIFT);
                if (aabb(cx+3,cy+4,CRATE_W-6,CRATE_H-8,x+3,y+3,e->width-6,h-5)) {
                    defeat_enemy(e);
                    cr->active=0;
                    cr->thrown=0;
                    break;
                }
            }
        }
        if (e->defeated) continue;

        /* Use separate stomp and hurt volumes. The visible sprites are much
           wider than their gameplay bodies in the MD code; r6's single broad
           rectangle made side contacts feel unfair. */
        {
            int old_bottom=old_py+PLAYER_BODY_H-1;
            int new_bottom=py+PLAYER_BODY_H-1;
            int stomp_x=aabb(px+3,py+PLAYER_BODY_H-6,10,8,x+2,y-2,e->width-4,8);
            if (g_vy>0 && stomp_x && old_bottom<=y+5 && new_bottom>=y) {
                defeat_enemy(e);
                g_jump_active=0;
                g_bounce_active=1;
                g_bounce_index=0;
                g_grounded=0;
                g_player_y=(s32)(y-PLAYER_BODY_YOFF-PLAYER_BODY_H)<<FIX_SHIFT;
                g_vy=-4*FIX_ONE;
            } else if (g_invuln==0) {
                int hurt_x=x+4,hurt_y=y+5,hurt_w=e->width-8,hurt_h=h-10;
                /* The long-strike snake's final pose is a 64x40 metasprite
                   drawn at x-32,y+16, rather than its 32x48 standing body.
                   Its attack must damage across that extended frame; keeping
                   the normal body hitbox here leaves the visible head unable
                   to hurt Mario.  Stomps above intentionally remain tied to
                   the standing body. */
                if(e->kind==ENEMY_KIND_STRIKE_SNAKE && e->behavior_phase &&
                   e->behavior_timer>=28) {
                    hurt_x=x-28;
                    hurt_y=y+21;
                    hurt_w=56;
                    hurt_h=30;
                }
                if(aabb(px+4,py+5,8,PLAYER_BODY_H-10,
                        hurt_x,hurt_y,hurt_w,hurt_h))
                    hurt_player();
            }
        }
    }
}

/* $C31A and $BC02 both create the projectile in the object slot after the
   thrower, at its own X and a fixed Y offset, then hand it a horizontal speed
   in the facing direction.  Returns the slot used, or -1 when the four-shot
   pool ($600..$660 in the source) is full. */
static int throw_enemy_shot(Enemy *e, int yoff, int xoff, int speed, int mode)
{
    int k;
    for(k=0;k<4;++k){
        EnemyShot *s=&g_enemy_shots[k];
        if(s->active)continue;
        s->active=1;
        s->x=e->x+(s32)(e->face_right?xoff:-xoff)*FIX_ONE;
        s->y=e->y+(s32)yoff*FIX_ONE;
        s->vx=(s32)(e->face_right?speed:-speed)*FIX_ONE;
        s->vy=0;
        s->mode=(unsigned char)mode;
        s->step=0;
        s->palette=g_enemy_palette_line[e->variant];
        s->base=g_enemy_shot_vram[e->variant];
        s->cols=g_enemy_variants[g_level->variant_ids[e->variant]].shot_cols;
        if(!s->base||!s->cols){s->base=ENEMY_BANANA_BASE_WORD;s->cols=1;}
        return k;
    }
    return -1;
}

static void update_enemy_shots(void)
{
    int i,px=player_body_x(),py=player_body_y();
    /* $C360: the rock loses speed over its first 25 frames and then crawls. */
    static const unsigned char rock_speed[25]={6,6,5,5,5,4,4,4,4,3,3,3,3,3,3,3,3,2,2,2,2,1,1,1,1};
    for(i=0;i<4;++i){EnemyShot *s=&g_enemy_shots[i];int x,y,w;if(!s->active)continue;
        if(s->mode==SHOT_MODE_ROCK){
            int sp=rock_speed[s->step<25?s->step:24];
            if(s->step<255)++s->step;
            s->x+=(s32)((s->vx<0)?-sp:sp)*FIX_ONE;
            s->y+=2*FIX_ONE;
        }else if(s->mode==SHOT_MODE_SPEAR){
            s->x+=s->vx;
        }else{
            s->x+=s->vx;s->vy+=48;if(s->vy>4*FIX_ONE)s->vy=4*FIX_ONE;s->y+=s->vy;
        }
        x=(int)(s->x>>FIX_SHIFT);y=(int)(s->y>>FIX_SHIFT);
        if(x<g_camera_x-40||x>g_camera_x+280||y>g_camera_y+240){s->active=0;continue;}
        w=s->cols?s->cols*16:16;
        if(g_invuln==0&&aabb(px,py,PLAYER_BODY_W,PLAYER_BODY_H,x,y,w,16)){hurt_player();s->active=0;}
    }
}

static int current_player_frame(void)
{
    static const unsigned char walk_frames[4] = { 1, 2, 1, 3 };
    if (g_lift_timer > 0) return (g_lift_timer > 8) ? 10 : 11;
    if (g_carry_index >= 0) {
        if (!g_grounded) return 12;
        if (g_vx != 0) {
            static const unsigned char carry_walk[4] = { 10, 14, 15, 14 };
            return carry_walk[(g_player_anim_clock / 5) & 3];
        }
        return 10;
    }
    if (!g_grounded) return 4;
    if (g_vx != 0) {
        int t = g_player_anim_clock % 28;
        if (t < 6) return walk_frames[0];
        if (t < 14) return walk_frames[1];
        if (t < 20) return walk_frames[2];
        return walk_frames[3];
    }
    return 3;
}

static int later_boss_frame(const BossVisualData *v)
{
    int frame,t=g_boss_timer;
    /* Frame groups follow the source sheet order.  Each handler's locomotion
       cadence comes from its recovered 44/64/80/160-frame state timing; the
       trailing poses are attack states and are selected while its projectile
       object is live. */
    switch(g_boss_class){
    case 1:
        /* Pig strip: 0/1 are the two propeller-flight poses, 2/3/4 the three
           tucked spin poses, 5 hurt, 6 death.  Flight used to pick one static
           pose per action, so the propeller never turned. */
        if(g_boss_defeated)frame=6;
        else if(g_boss_invuln>0)frame=5;
        else if(boss8_spinning())frame=2+(t/4)%3;
        else frame=(t/4)&1;
        break; /* Pig */
    /* Ostrich strip: 0 and 1 are the two 96-pixel poses used for everything
       it normally does (standing, running, spitting) and 2 is the taller
       recoil pose, which belongs to being hit alone.  Every pose is
       bottom-aligned in the shared canvas, so the extra height grows upward
       and the feet stay put. */
    case 2: frame=(g_boss_invuln>0)?2:((t/6)&1);break;        /* ostrich */
    case 3: frame=g_boss_shot_active?3+(t/8)%7:(t/8)%3;break; /* tank */
    case 4: frame=g_boss_shot_active?3+(t/8)%2:(t/8)%3;break; /* mage */
    default:frame=g_boss_shot_active?2+(t/8)%3:(t/8)%2;break; /* elephant */
    }
    return frame%v->frame_count;
}

static void draw_game_sprites(void)
{
    static u16 satb[64 * 4];
    int i, n = 0;
    int frame = current_player_frame();
    int px = (int)(g_player_x >> FIX_SHIFT) - g_camera_x;
    int py = (int)(g_player_y >> FIX_SHIFT) - g_camera_y + bowser_room_object_y_offset();
    u16 base = (u16)(g_satb_page ? SATB_BASE1 : SATB_BASE0);
    /* Every normal-level palette carries the original Mario/object colors in
       source line 0 (identical across all 19 level palettes).  r11 moved
       normal sprites to line 13 and produced the observed yellow/green Mario
       regression.  Keep the separate line only for boss rooms, where the
       arena uses lines 0..2. */
    u16 player_pal=(u16)(g_in_boss?PLAYER_SPR_PAL:0);
    u16 pctrl = (u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_1 | player_pal);
    u16 paddr = (u16)(PLAYER_BASE_WORD + (frame-1) * PLAYER_FRAME_WORDS_LOCAL);
    if (g_player_down && g_grounded) {
        paddr=(g_menu_choice==MENU_2P)?GAMEPLAY_DOWN_LUIGI_BASE_WORD:GAMEPLAY_DOWN_MARIO_BASE_WORD;
        pctrl=(u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_1 |
                    (g_in_boss?BOSS_CROUCH_SPR_PAL:CONTINUE_PLAYER_SPR_PAL));
        py+=8;
    }
    int player_visible=1;
    int hud_yoff=(g_in_boss||(!g_in_boss&&g_current_level==2))?-16:0;

    for (i = 0; i < 64 * 4; ++i) satb[i] = 0;

    /* Exact Mega Drive HUD object boundaries, emitted first in SATB so a busy
       room cannot drop them: 24x16 life icon/x, 16x16 fire icon, then live
       digits and individual heart sprites. */
    set_sprite_entry(satb,n++,8,209+hud_yoff,HUD_LIFE_BASE_WORD,
        (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|player_pal));
    set_sprite_entry(satb,n++,80,209+hud_yoff,HUD_FIRE_BASE_WORD,
        (u16)(VDC_SPR_PRIO_SP|player_pal));
    if (n<64) {
        int lives=g_lives; if (lives<0) lives=0; if (lives>9) lives=9;
        set_sprite_entry(satb,n++,32,217+hud_yoff,(u16)(HUD_DIGIT_BASE_WORD+lives*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
    }
    for (i=0;i<g_player_hearts && i<PLAYER_MAX_HEARTS && n<64;++i)
        set_sprite_entry(satb,n++,40+i*8,217+hud_yoff,HUD_HEART_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|player_pal));
    if (n<64) {
        int digit=g_fireball_ammo;
        if (digit<0) digit=0; if (digit>9) digit=9;
        set_sprite_entry(satb,n++,96,217+hud_yoff,(u16)(HUD_DIGIT_BASE_WORD+digit*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
    }
    /* Everything emitted from here on is world content and may be occluded by
       a front-plane scenery layer; the HUD above never is. */
    int hud_entries=n;

    /* SATB entries with lower indices win sprite/sprite overlap.  A carried
       crate is the one exception to the normal object ordering: it must cover
       Mario's hands/body, while every world crate remains behind Mario. */
    if (g_carry_index>=0 && g_carry_index<MAX_CRATES &&
        g_crates[g_carry_index].active && g_crates[g_carry_index].carried && n<64) {
        Crate *c=&g_crates[g_carry_index];
        int sx=(int)(c->x>>FIX_SHIFT)-g_camera_x;
        int sy=(int)(c->y>>FIX_SHIFT)-g_camera_y+bowser_room_object_y_offset();
        if (sx>=-32 && sx<=255 && sy>=-32 && sy<=239) {
            u16 addr=CRATE_BASE_WORD,pal=player_pal;
            int width=2,height=2;
            if((!g_in_boss&&g_current_level>=6&&g_current_level<=8)||
               (g_in_boss&&g_boss_class==1)) {
                addr=BOSS_POT_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;
            } else if(g_in_boss&&g_boss_class==2) {
                addr=BOSS_CRATE4_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;width=1;
            } else if(g_in_boss&&g_boss_class==3) {
                addr=(u16)(BOSS_BOMB_BASE_WORD+((g_state_frame>>3)&1)*256);pal=BOSS_ITEM_SPR_PAL;
            } else if(g_in_boss&&g_boss_class==4) {
                addr=BOSS_BELL_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;
            } else if(g_in_boss&&g_boss_class>0) {
                addr=BOSS_HAMMER_BASE_WORD;pal=BOSS_AUX_SPR_PAL;
            }
            set_sprite_entry(satb,n++,sx,sy,addr,(u16)(VDC_SPR_PRIO_SP |
                (width==2?VDC_SPR_X_WIDTH_2:0) |
                (height==2?VDC_SPR_Y_HEIGHT_2:0) | pal));
        }
    }

    if (g_invuln>0 && g_player_mode==PMODE_NORMAL)
        player_visible=((g_invuln/HURT_FLICKER_HALF_PERIOD)&1)!=0;
    if(g_crate_hide&&g_carry_index>=0)player_visible=0;

    if (g_player_mode==PMODE_DYING) {
        /* The source game-over fall advances through four body poses. */
        static const unsigned char death_frames[4]={4,5,6,5};
        int df=death_frames[(g_death_timer/6)&3];
        u16 da=(u16)(PLAYER_BASE_WORD+(df-1)*PLAYER_FRAME_WORDS_LOCAL);
        u16 dc=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_1|player_pal);
        if (g_facing<0) dc|=VDC_SPR_X_INVERT;
        set_sprite_entry(satb,n++,px,py,da,dc);
        set_sprite_entry(satb,n++,px,py+16,(u16)(da+PLAYER_STRIP_WORDS),dc);
        set_sprite_entry(satb,n++,px,py+32,(u16)(da+PLAYER_STRIP_WORDS*2),dc);
    } else if (g_player_mode!=PMODE_FADE_OUT && player_visible) {
        if (g_throw_timer>0) {
            /* State 8 / gfx frame 13 is a true 48x32 metasprite. r4 used it
               for death and therefore lost the right-hand 16x32 block when
               throwing. Keep the whole composition visible for 30 frames. */
            u16 tc=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_Y_HEIGHT_2|player_pal);
            if (g_facing>0) {
                set_sprite_entry(satb,n++,px,py+16,THROW_BASE_WORD,(u16)(tc|VDC_SPR_X_WIDTH_2));
                set_sprite_entry(satb,n++,px+32,py+16,(u16)(THROW_BASE_WORD+256),tc);
            } else {
                tc|=VDC_SPR_X_INVERT;
                set_sprite_entry(satb,n++,px+16,py+16,THROW_BASE_WORD,(u16)(tc|VDC_SPR_X_WIDTH_2));
                set_sprite_entry(satb,n++,px,py+16,(u16)(THROW_BASE_WORD+256),tc);
            }
        } else {
            if (g_facing < 0) pctrl |= VDC_SPR_X_INVERT;
            set_sprite_entry(satb,n++,px,py,paddr,pctrl);
            set_sprite_entry(satb,n++,px,py+16,(u16)(paddr+PLAYER_STRIP_WORDS),pctrl);
            set_sprite_entry(satb,n++,px,py+32,(u16)(paddr+PLAYER_STRIP_WORDS*2),pctrl);
        }
    }

    /* Gems use their original 97/98/99 three-frame timing: 15,7,15. */
    {
        int t = g_state_frame % 37;
        int gf = (t < 15) ? 0 : (t < 22 ? 1 : 2);
        for (i = 0; i < MAX_GEMS && n < 50; ++i) {
            int sx, sy;
            if (!g_gems[i].active) continue;
            sx = g_gems[i].x - g_camera_x; sy = g_gems[i].y - g_camera_y;
            if (sx < -16 || sx > 255 || sy < -16 || sy > 239) continue;
            set_sprite_entry(satb,n++,sx,sy,(u16)(GEM_BASE_WORD + gf*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
        }
    }

    /* Freed crate contents.  Metasprites 191..197 are single 16x16 parts on
       the same sprite palette line the crate and gem use. */
    for (i = 0; i < MAX_ITEMS && n < 54; ++i) {
        int sx, sy;
        if (!g_items[i].active) continue;
        sx = (int)(g_items[i].x >> FIX_SHIFT) - g_camera_x;
        sy = (int)(g_items[i].y >> FIX_SHIFT) - g_camera_y;
        if (sx < -16 || sx > 255 || sy < -16 || sy > 239) continue;
        set_sprite_entry(satb,n++,sx,sy,
            (u16)(ITEM_BASE_WORD + (g_items[i].kind-1)*ITEM_CELL_WORDS),
            (u16)(VDC_SPR_PRIO_SP|player_pal));
    }

    for (i = 0; i < MAX_CRATES && n < 54; ++i) {
        int sx, sy;
        if (!g_crates[i].active || g_crates[i].carried) continue;
        sx = (int)(g_crates[i].x >> FIX_SHIFT) - g_camera_x;
        sy = (int)(g_crates[i].y >> FIX_SHIFT) - g_camera_y + bowser_room_object_y_offset();
        if (sx < -32 || sx > 255 || sy < -32 || sy > 239) continue;
        {u16 addr=CRATE_BASE_WORD,pal=player_pal; int width=2,height=2;
        if((!g_in_boss&&g_current_level>=6&&g_current_level<=8)||
           (g_in_boss&&g_boss_class==1)){
            addr=BOSS_POT_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;
        }else if(g_in_boss&&g_boss_class==2){
            addr=BOSS_CRATE4_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;width=1;
        }else if(g_in_boss&&g_boss_class==3){
            addr=(u16)(BOSS_BOMB_BASE_WORD+((g_state_frame>>3)&1)*256);pal=BOSS_ITEM_SPR_PAL;
        }else if(g_in_boss&&g_boss_class==4){
            addr=BOSS_BELL_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;
        }else if(g_in_boss&&g_boss_class>0){
            addr=BOSS_HAMMER_BASE_WORD;pal=BOSS_AUX_SPR_PAL;
        }
        set_sprite_entry(satb,n++,sx,sy,addr,(u16)(VDC_SPR_PRIO_SP | (width==2?VDC_SPR_X_WIDTH_2:0) | (height==2?VDC_SPR_Y_HEIGHT_2:0) | pal));}
    }

    /* Traversal-critical Level-2 moving platforms get SATB priority over
       enemies so a busy screen cannot make the floor disappear. */
    for (i=0;i<g_moving_platform_count && n<58;++i) {
        MovingPlatform *mp=&g_moving_platforms[i];
        int sx=platform_draw_x(mp)-g_camera_x;
        int sy=platform_draw_y(mp)-g_camera_y;
        if (sx<-platform_width(mp) || sx>255 || sy<-platform_height(mp) || sy>239) continue;
        if(PLATFORM_KIND_IS_STAGE_OBJECT(mp->kind)){
            /* Cells are stored row-major with an even pitch, so each pair is
               128-word aligned and can go out as one 32-pixel-wide sprite. */
            const unsigned short *sh=platform_shape(mp);
            u16 base=(u16)(STAGE_OBJECT_BASE_WORD+sh[0]);
            int cols=sh[1],rows=sh[2],cy2,cx2;
            for(cy2=0;cy2<rows&&n<58;++cy2)for(cx2=0;cx2<cols&&n<58;cx2+=2){
                u16 addr=(u16)(base+(cy2*cols+cx2)*64);
                set_sprite_entry(satb,n++,sx+cx2*16,sy+cy2*16,addr,
                    (u16)(VDC_SPR_PRIO_SP|((cols-cx2>=2)?VDC_SPR_X_WIDTH_2:0)|LEVEL8_OBJ_SPR_PAL));
            }
        }else if(mp->kind==PLATFORM_KIND_BRICK){
            set_sprite_entry(satb,n++,sx,sy,PLATFORM_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
            if(n<58)set_sprite_entry(satb,n++,sx+32,sy,(u16)(PLATFORM_BASE_WORD+128),
                (u16)(VDC_SPR_PRIO_SP|LEVEL8_OBJ_SPR_PAL));
        }else if(mp->kind==PLATFORM_KIND_WEDGE_R){
            /* 48x64: four 32x16 strips plus the 16-wide right column. */
            int s;
            for(s=0;s<4&&n<58;++s){
                set_sprite_entry(satb,n++,sx,sy+s*16,(u16)(LEVEL8_WEDGE_R_BASE_WORD+s*128),
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
                if(n<58)set_sprite_entry(satb,n++,sx+32,sy+s*16,(u16)(LEVEL8_WEDGE_R_RIGHT_WORD+s*64),
                    (u16)(VDC_SPR_PRIO_SP|LEVEL8_OBJ_SPR_PAL));
            }
        }else if(mp->kind==PLATFORM_KIND_WEDGE_L){
            int s;
            for(s=0;s<3&&n<58;++s)
                set_sprite_entry(satb,n++,sx,sy+s*16,(u16)(LEVEL8_WEDGE_L_BASE_WORD+s*128),
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
        }else if(g_in_boss&&g_boss_class==3){
            set_sprite_entry(satb,n++,sx,sy,BOSS13_PLATFORM_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS13_PLATFORM_SPR_PAL));
            if(n<58)set_sprite_entry(satb,n++,sx+32,sy,(u16)(BOSS13_PLATFORM_BASE_WORD+256),(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS13_PLATFORM_SPR_PAL));
        }else{
            set_sprite_entry(satb,n++,sx,sy,PLATFORM_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2));
            if(n<58)set_sprite_entry(satb,n++,sx+32,sy,(u16)(PLATFORM_BASE_WORD+128),VDC_SPR_PRIO_SP);
        }
    }

    /* Projectiles are emitted ahead of the enemies: a screen full of stage
       objects can exhaust the 64-entry SATB, and dropping the thing that
       hurts the player is worse than dropping one enemy cell. */
    for(i=0;i<4&&n<62;++i)if(g_enemy_shots[i].active){
        int sx=(int)(g_enemy_shots[i].x>>FIX_SHIFT)-g_camera_x,sy=(int)(g_enemy_shots[i].y>>FIX_SHIFT)-g_camera_y;
        int c;
        for(c=0;c<g_enemy_shots[i].cols&&n<62;++c)
            set_sprite_entry(satb,n++,sx+c*16,sy,(u16)(g_enemy_shots[i].base+c*64),
                (u16)(VDC_SPR_PRIO_SP|g_enemy_shots[i].palette));
    }

    for (i=0;i<MAX_ENEMIES && n<58;++i) {
        int sx,sy,anim,li,gi,cx,cy,cols,rows,grid_cols,special_snake,special_monkey,special_crate,special_frame,use_hit_frame,use_alt=0,use_atk=0,use_atk2=0; unsigned mask; u16 flip,base;
        Enemy *e=&g_enemies[i]; const EnemyVariantData *v;
        if(!e->active||!e->spawned)continue;
        sx=(int)(e->x>>FIX_SHIFT)-g_camera_x;sy=(int)(e->y>>FIX_SHIFT)-g_camera_y;
        /* The mimic's authored Y is the crate baseline.  Its revealed body
           grows upward from that footprint rather than down into the floor. */
        if(e->kind==ENEMY_KIND_CRATE_MIMIC){
            if(e->behavior_phase==1)sy-=8;       /* 24px crate -> 32px rise */
            else if(e->behavior_phase==2)sy-=14;/* walking art's visible baseline */
        }
        if(sx<-56||sx>255||sy<-56||sy>239)continue;
        li=e->variant;if(li>=g_level->variant_count)continue;gi=g_level->variant_ids[li];v=&g_enemy_variants[gi];
        anim=(int)((g_state_frame/8)&1);
        /* Frame selector 2 is a valid live wing-flap cel for MATTHEW, so
           keep the defeated state separate from the frame number. */
        use_hit_frame=e->defeated&&v->has_hit_frame;
        if(e->defeated)anim=use_hit_frame?2:1;
        else if(e->kind!=ENEMY_KIND_GENERIC&&e->kind!=ENEMY_KIND_CHIP){
            if(e->kind==ENEMY_KIND_MONKEY)anim=(e->behavior_phase==2)?((g_state_frame/12)&1):0;
            else if(e->kind==ENEMY_KIND_PTERODACTYL)
                /* Selector 0 hangs; selectors 1 and 2 are the two wide flight
                   cels.  Normal enemy storage holds selectors 0/1, so the
                   second flight cel lives in the alt bank. */
                anim=e->behavior_phase?(1+((e->behavior_timer/8)&1)):0;
            else if(e->kind==ENEMY_KIND_STRIKE_SNAKE)anim=e->behavior_phase?((e->behavior_timer<28)?1:2):0;
            else if(e->kind==ENEMY_KIND_CRATE_MIMIC)anim=(e->behavior_phase==0)?0:((e->behavior_phase==1)?1:(1+((g_state_frame/12)&1)));
            /* Every fish script is two cels.  Selector 0 is the fish lying
               flat in the water and selector 1 is it half risen out of it, so
               state 0 surfaces by running 0 for four ticks and then 1, which
               $D0B8 holds until the leap: 0 is the pose to wait on and 1 the
               pose it comes out of the water on.  State 1 flaps selectors 2
               and 3 seven ticks each and loops on its $8888, so the whole jump
               is that pair -- selector 2 in the attack bank, selector 3 in the
               alt bank.  State 3 sinks on selectors 4 and 5, whose art the
               port does not keep; running the surfacing pair backwards is the
               closest the four cels it has can come. */
            else if(e->kind==ENEMY_KIND_FLOOR_SNAKE)
            {
                int early=e->behavior_timer<FISH_IDLE_CEL0_TICKS;
                anim=(e->behavior_phase==1)?(((e->behavior_timer/7)&1)?2:3):
                     (e->behavior_phase==0)?(early?0:1):(early?1:0);
            }
            /* The rock's cycle is the three poses the family stores, and the
               third repeats the first: only two of the ball's images are
               distinct, which is the pair the port keeps in VRAM.  (The walk
               script also names metasprites 447 and 453, but those re-place
               the same four quadrant tiles and resolve to torn halves
               against this art rather than to a further rotation.) */
            else if(e->kind==ENEMY_KIND_BOULDER)anim=(((int)(g_state_frame/6)%3)==1)?1:0;
            /* The throwers write 1 into their animation-state word before the
               projectile leaves, so both stay on the attack cel until it is
               gone (and $C0C6 through its recovery count as well).

               Both attack scripts run two cels: $D244 is $E1 (15) / $F9 (7) /
               $E1, and $D3F8 is $F9 (7) / $F9-selector-4 (7) / $F9.  Anim 4 is
               that second cel, held for the ROM's seven ticks starting at the
               frame the projectile leaves. */
            else if(e->kind==ENEMY_KIND_THROWER)
                anim=(e->behavior_phase==0)?0:
                     (e->behavior_phase==2&&e->behavior_timer<THROWER_RELEASE_TICKS)?4:3;
            else if(e->kind==ENEMY_KIND_SPEARMAN)
                anim=e->behavior_phase?
                     ((e->behavior_timer>=THROWER_RELEASE_TICKS&&
                       e->behavior_timer<THROWER_RELEASE_TICKS*2)?4:3):
                     (int)((g_state_frame/8)&1);
        }
#ifdef FORCE_ENEMY_HURT
        if(v->has_hit_frame){use_hit_frame=1;anim=2;}
#endif
#ifdef FORCE_SNAKE_FRAME
        if(e->kind==ENEMY_KIND_STRIKE_SNAKE)anim=FORCE_SNAKE_FRAME;
#endif
        special_snake=(e->kind==ENEMY_KIND_STRIKE_SNAKE);
        special_monkey=(e->kind==ENEMY_KIND_MONKEY&&(e->defeated||e->behavior_phase==3));
        special_crate=(e->kind==ENEMY_KIND_CRATE_MIMIC&&e->behavior_phase==2&&!e->defeated);
        special_frame=e->defeated?1:0;
#ifdef FORCE_MONKEY_HURT
        if(e->kind==ENEMY_KIND_MONKEY){special_monkey=1;special_frame=1;}
#endif
        if(special_snake){
            /* The long strike is 64x40 versus the 32x56 standing pose.
               Both use the same ground baseline in the Mega Drive SAT, so
               compensate for the strike canvas being sixteen pixels shorter. */
            if(anim==2){sx-=32;sy+=16;}
            mask=g_level8_snake_masks[anim];cols=rows=grid_cols=4;
        }
        else if(special_monkey){mask=g_monkey_special_masks[special_frame];cols=rows=grid_cols=3;}
        else if(special_crate){special_frame=(g_state_frame/10)&1;mask=g_crate_walk_masks[special_frame];cols=rows=grid_cols=3;}
        else{
            /* Every pose of a variant is packed bottom-aligned in its cell grid
               so the short ones stop floating, which shifts the whole grid down
               by the walk cel's share of that alignment.  Lift it back so walk
               cel 0 -- the pose that was always placed correctly -- does not
               move.  The special banks above carry their own art and their own
               alignment, so they must not be shifted. */
            sy-=(int)v->baseline;
            /* Live third cels come from the alt bank; anim 2 otherwise means
               the hit pose, which can be a taller metasprite than the walk. */
            use_alt=(anim==2&&!use_hit_frame&&v->alt_words!=0&&g_enemy_alt_vram[li]!=0);
            use_atk2=(anim==4&&v->atk2_words!=0&&g_enemy_atk2_vram[li]!=0);
            if(anim==4&&!use_atk2)anim=3;  /* family has one attack cel only */
            use_atk=(anim==3&&v->atk_words!=0&&g_enemy_atk_vram[li]!=0);
            if(anim==3&&!use_atk)anim=0;   /* no captured attack pose: stand */
            mask=use_atk2?v->atk2_mask:use_atk?v->atk_mask:use_alt?v->alt_mask:(anim==2)?v->mask2:(anim?v->mask1:v->mask0);
            cols=grid_cols=v->cols;rows=v->rows;
        }
        if(special_snake)base=(u16)(STRIKE_SNAKE_BASE_WORD+anim*1024);
        else if(special_monkey)base=(u16)(MONKEY_SPECIAL_BASE_WORD+special_frame*ENEMY_FRAME_WORDS);
        else if(special_crate)base=(u16)(CRATE_MIMIC_WALK_BASE_WORD+special_frame*ENEMY_FRAME_WORDS);
        else if(use_atk2)base=g_enemy_atk2_vram[li];
        else if(use_atk)base=g_enemy_atk_vram[li];
        else if(use_alt)base=g_enemy_alt_vram[li];
        else if(use_hit_frame)base=g_enemy_hit_vram[li];
        else base=(u16)(g_enemy_vram[li]+(anim?enemy_frame_words(v):0));
        flip=((e->kind==ENEMY_KIND_PTERODACTYL||e->kind==ENEMY_KIND_THROWER||
               (e->kind==ENEMY_KIND_SPEARMAN&&e->behavior_phase))?e->face_right:(e->vx>0))?VDC_SPR_X_INVERT:0;
        for(cy=0;cy<rows && n<61;++cy)for(cx=0;cx<cols && n<61;++cx){
            int cell=cy*grid_cols+cx,dx;
            if(!(mask&(1u<<cell)))continue;dx=flip?(cols-1-cx)*16:cx*16;
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(base+cell*64),(u16)(VDC_SPR_PRIO_SP|flip|g_enemy_palette_line[li]));
        }
    }

    if(g_in_boss && (g_boss_hp>0||g_boss_defeated) && n<58){
        /* Hurt includes an impact puff four pixels above the body, so its
           capture origin needs +4 relative to the old value to keep Bowser's
           feet aligned with the neighboring walk poses. */
        int bx=(int)(g_boss_x>>FIX_SHIFT),by=(int)(g_boss_y>>FIX_SHIFT),cx,cy;
        unsigned long long mask;
        u16 flip=(g_boss_face<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            const BossVisualData *v=&g_later_boss_visuals[g_boss_class-1];
            int frame=later_boss_frame(v),frame_words=v->cols*v->rows*64;
            if(frame>=0){
                mask=v->sprite_masks[frame];
                for(cy=0;cy<v->rows && n<62;++cy)for(cx=0;cx<v->cols && n<62;++cx){
                    int cell=cy*v->cols+cx,dx,dy=by+cy*16,palette;if(!(mask&(1ULL<<cell)))continue;
                    dx=(g_boss_face<0)?(v->cols-1-cx)*16:cx*16;
                    palette=v->sprite_cell_palettes[frame*v->cols*v->rows+cell];
                    set_sprite_entry(satb,n++,bx+dx,dy,(u16)(BOSS_BASE_WORD+frame*frame_words+cell*64),
                        (u16)(VDC_SPR_PRIO_SP|(palette?BOSS_SPR_PAL_AUX:BOSS_SPR_PAL)|flip));
                }
            }
        }else{
        /* The Genesis animation table at $71D2 selects two run frames at
           seven ticks each, two fire frames at seven ticks each, and the
           three teleport frames at three ticks each. Fire loops while its
           projectile state is active; teleport is a single 9-tick sequence. */
        int special,t=g_boss_action_timer;
        int action=g_bowser_action_script[g_boss_action];
        if(g_boss_invuln>0){
            special=BOWSER_SPECIAL_HURT;
        }else if(action==5){
            if(t<1)t=1;
            special=BOWSER_SPECIAL_FIRE+(((t-1)/BOWSER_FIRE_FRAME_TICKS)&1);
        }else if(action==6&&t>=1&&t<9){
            special=BOWSER_SPECIAL_TELEPORT+(t-1)/BOWSER_TELEPORT_FRAME_TICKS;
            if(special>BOWSER_SPECIAL_TELEPORT+2)special=BOWSER_SPECIAL_TELEPORT+2;
        }else if(action==3||action==4){
            if(t<1)t=1;
            special=BOWSER_SPECIAL_RUN+(((t-1)/BOWSER_RUN_FRAME_TICKS)&1);
        }else{
            if(t<1)t=1;
            special=BOWSER_SPECIAL_NORMAL+(((t-1)/BOWSER_RUN_FRAME_TICKS)&1);
        }
        by-=15;
        mask=g_bowser_special_masks[special];
        for(cy=0;cy<6 && n<62;++cy)for(cx=0;cx<7 && n<62;++cx){int cell=cy*7+cx,dx;if(!(mask&(1ULL<<cell)))continue;dx=(g_boss_face<0)?(6-cx)*16:cx*16;set_sprite_entry(satb,n++,bx+dx,by+cy*16,(u16)(BOWSER_ANIM_BASE_WORD+special*BOWSER_SPECIAL_FRAME_WORDS+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));}
        }
    }

    if(g_in_boss && g_boss_shot_active && n<63){
        int sx=(int)(g_boss_shot_x>>FIX_SHIFT),sy=(int)(g_boss_shot_y>>FIX_SHIFT),cx,cy;
        u16 flip=(g_boss_shot_vx<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            if(g_boss_class==1)set_sprite_entry(satb,n++,sx,sy,BOSS_WATER_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|BOSS_WATER_SPR_PAL));
            else if(g_boss_class==4||g_boss_class==5){
                int count=(g_boss_class==4)?3:4,k;
                for(k=0;k<count&&n<64;++k)set_sprite_entry(satb,n++,sx-(g_boss_shot_vx<0?-k*18:k*18),sy-k*22,BOSS_RING_BASE_WORD,
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS_AUX_SPR_PAL|flip));
            }else set_sprite_entry(satb,n++,sx,sy,FIREBALL_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|PLAYER_SPR_PAL|flip));
        }else{
        /* The breath object's coordinates precede its visible metasprite by
           (+32,+13) when travelling right; mirror that offset to the left. */
        sx+=(g_boss_shot_vx<0)?-32:32;sy-=3;
        for(cy=0;cy<2&&n<64;++cy)for(cx=0;cx<3&&n<64;++cx){
            int cell=cy*3+cx,dx;if(!(g_bowser_flame_mask&(1u<<cell)))continue;
            dx=(g_boss_shot_vx<0)?(2-cx)*16:cx*16;
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(BOWSER_FLAME_BASE_WORD+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));
        }
        }
    }

    for (i=0;i<MAX_FIREBALLS && n<64;++i) {
        int sx,sy;
        u16 ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|player_pal);
        if (!g_fireballs[i].active) continue;
        sx=(int)(g_fireballs[i].x>>FIX_SHIFT)-g_camera_x;
        sy=(int)(g_fireballs[i].y>>FIX_SHIFT)-g_camera_y;
        if (sx<-32 || sx>255 || sy<-32 || sy>239) continue;
        if (g_fireballs[i].vx<0) ctrl|=VDC_SPR_X_INVERT;
        set_sprite_entry(satb,n++,sx,sy,FIREBALL_BASE_WORD,ctrl);
    }

    if (level_front_scenery_over_sprites())
        for (i=hud_entries;i<64;++i) satb[i*4+3]&=(u16)~VDC_SPR_PRIO_SP;

    vdc_set_vram_write(FRONT_CHIP, base);
    for (i = 0; i < 64 * 4; ++i) vdc_vram_write(FRONT_CHIP, satb[i]);
    vdc_set_satb_address(FRONT_CHIP, base);
    g_satb_page ^= 1;
}

static void set_continue_palette_fade(int level)
{
    int i;
    for (i=0;i<256;++i)
        tetsu_set_palette((u16)i,fade_yuv(g_continue_base_asset.palette[i],level));
    for (i=0;i<16;++i)
        tetsu_set_palette((u16)(CONTINUE_PLAYER_SPR_PAL*16+i),fade_yuv(g_continue_player_palette[i],level));
    /* Large countdown digit uses dedicated sprite palette line 15. */
    for (i=0;i<16;++i)
        tetsu_set_palette((u16)(240+i),fade_yuv(g_continue_digit_palette[i],level));
}

static void draw_continue_digit(void)
{
    static u16 satb[64*4];
    int i,d=g_continue_digit;
    int frame=(int)((g_state_frame/CONTINUE_PLAYER_FRAME_TICKS)%CONTINUE_PLAYER_FRAME_COUNT);
    for (i=0;i<64*4;++i) satb[i]=0;
    if (d<0) d=0; if (d>9) d=9;
    for (i=0;i<3;++i) {
        u16 ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_1|CONTINUE_PLAYER_SPR_PAL);
        set_sprite_entry(satb,i,72,48+i*16,(u16)(CONTINUE_PLAYER_BASE_WORD+frame*PLAYER_FRAME_WORDS_LOCAL+i*PLAYER_STRIP_WORDS),ctrl);
        set_sprite_entry(satb,3+i,152,48+i*16,(u16)(CONTINUE_LUIGI_BASE_WORD+frame*PLAYER_FRAME_WORDS_LOCAL+i*PLAYER_STRIP_WORDS),ctrl);
    }
    set_sprite_entry(satb,6,116,152,(u16)(CONTINUE_DIGIT_BASE_WORD+d*256),
        (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|15));
    submit_front_satb(satb);
}

static void load_continue_hidden(void)
{
    int x,y,i;
    music_transition_to_continue();
    /* Never expose a half-loaded BAT/CHR set. Both VDC outputs remain disabled
       until the following TETSU frame boundary. */
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_bat(BACK_CHIP,BG_BAT_WORDS);
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    set_map_height(BACK_CHIP,0); set_map_height(FRONT_CHIP,0);
    upload_tiles(BACK_CHIP,CONTINUE_TILE_BASE_WORD,&g_continue_base_asset);
    for (y=0;y<30;++y) {
        vdc_set_vram_write(BACK_CHIP,(u16)(y*BG_BAT_W));
        for (x=0;x<32;++x)
            vdc_vram_write(BACK_CHIP,relocate_bat(g_continue_base_asset.bat[y*32+x],CONTINUE_TILE_BASE_INDEX));
    }
    vdc_set_vram_write(FRONT_CHIP,CONTINUE_DIGIT_BASE_WORD);
    for (i=0;i<2560;++i) vdc_vram_write(FRONT_CHIP,g_continue_digit_words[i]);
    vdc_set_scroll(BACK_CHIP,0,0); vdc_set_scroll(FRONT_CHIP,0,0);
    g_continue_digit=9;
    set_continue_palette_fade(0);
    music_start_at_black();
    draw_continue_digit();
    g_state=STATE_CONTINUE_LOAD;
    g_state_frame=0;
}

static int boss_class_for_level(int level_index)
{
    /* Exact 68000 handler dispatch recovered with BlastEm's disassembler:
       slots 3/6->$7526, 8->$806A, 10->$8764, 13->$8CEC,
       16->$99BE, 19->$940E. */
    switch(level_index+1){
    case 3: case 6: return 0;
    case 8: return 1;
    case 10:return 2;
    case 13:return 3;
    case 16:return 4;
    case 19:return 5;
    default:return 0;
    }
}

static void start_boss(int fade_in)
{
    int i,x,y;
    const unsigned short *bp;
    const BossVisualData *bv;
    const BossRoomData *br;
    g_in_boss=1; g_paused=0; g_level=&g_game_levels[g_current_level]; g_level_width_tiles=32; g_level_rows=1;
    music_transition_to_boss();
    g_boss_class=boss_class_for_level(g_current_level);bv=(g_boss_class>0)?&g_later_boss_visuals[g_boss_class-1]:0;br=bv?0:&g_bowser_rooms[(g_current_level==5)?1:0];
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_bat(BACK_CHIP,BG_BAT_WORDS);clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    set_map_height(BACK_CHIP,0); set_map_height(FRONT_CHIP,0);
    vdc_set_vram_write(BACK_CHIP,BOSS_BG_TILE_BASE_WORD);
    for(i=0;i<(bv?bv->bg_tile_count:br->tile_count)*TILE_WORDS;++i)vdc_vram_write(BACK_CHIP,bv?bv->bg_tile_words[i]:br->tile_words[i]);
    /* The final wall scrolls through a 224-line (28-row) source. Populate two
       complete repetitions in the 64-row VDC0 BAT; a 240-line viewport can
       then cross the source seam without ever exposing VDP scratch rows. */
    for(y=0;y<((g_boss_class==5)?64:32);++y){int sy=(g_boss_class==5)?(y%28):y;vdc_set_vram_write(BACK_CHIP,(u16)(y*BG_BAT_W));for(x=0;x<32;++x)vdc_vram_write(BACK_CHIP,relocate_bat(bv?bv->bg_bat[sy*32+x]:br->bg_bat[sy*32+x],BOSS_BG_TILE_BASE_INDEX));}
    if(bv){
        vdc_set_vram_write(FRONT_CHIP,FG_TILE_BASE_WORD);
        for(i=0;i<bv->fg_tile_count*TILE_WORDS;++i)vdc_vram_write(FRONT_CHIP,bv->fg_tile_words[i]);
        for(y=0;y<32;++y){vdc_set_vram_write(FRONT_CHIP,(u16)(y*FG_BAT_W));for(x=0;x<32;++x)vdc_vram_write(FRONT_CHIP,relocate_bat(bv->fg_bat[y*32+x],FG_TILE_BASE_INDEX));}
    }
    else{
        vdc_set_vram_write(FRONT_CHIP,FG_TILE_BASE_WORD);
        for(i=0;i<br->tile_count*TILE_WORDS;++i)vdc_vram_write(FRONT_CHIP,br->tile_words[i]);
        for(y=0;y<32;++y){vdc_set_vram_write(FRONT_CHIP,(u16)(y*FG_BAT_W));for(x=0;x<32;++x)vdc_vram_write(FRONT_CHIP,relocate_bat(br->fg_bat[y*32+x],FG_TILE_BASE_INDEX));}
    }
    upload_game_sprites();
    if(bv){
        vdc_set_vram_write(FRONT_CHIP,BOSS_BASE_WORD);
        for(i=0;i<bv->frame_count*bv->cols*bv->rows*64;++i)vdc_vram_write(FRONT_CHIP,bv->sprite_words[i]);
    }else{
        /* Every Bowser body pose comes from the canonical bowser/ bank. */
        vdc_set_vram_write(FRONT_CHIP,BOWSER_FLAME_BASE_WORD);
        for(i=0;i<384;++i)vdc_vram_write(FRONT_CHIP,g_bowser_flame_words[i]);
        vdc_set_vram_write(FRONT_CHIP,BOWSER_ANIM_BASE_WORD);
        for(i=0;i<BOWSER_SPECIAL_FRAME_WORDS*BOWSER_SPECIAL_FRAME_COUNT;++i)vdc_vram_write(FRONT_CHIP,g_bowser_special_words[i]);
    }
    if(bv){vdc_set_vram_write(FRONT_CHIP,BOSS_AUX_BASE_WORD);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_final_ring_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_final_hammer_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_pot_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_bell_words[i]);for(i=0;i<512;++i)vdc_vram_write(FRONT_CHIP,g_boss_bomb_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_crate4_words[i]);if(g_boss_class==3)for(i=0;i<512;++i)vdc_vram_write(FRONT_CHIP,g_boss13_platform_words[i]);if(g_boss_class==1){vdc_set_vram_write(FRONT_CHIP,BOSS_WATER_BASE_WORD);for(i=0;i<64;++i)vdc_vram_write(FRONT_CHIP,g_boss_water_words[i]);}}
    init_objects();

    /* Special-mode player spawn is the alternate table at ROM $1DC56.
       Like the ordinary spawn table, coordinates are object-space +$80. */
    x=(int)g_boss_alt_spawn[g_current_level*2]-128;
    y=(int)g_boss_alt_spawn[g_current_level*2+1]-128;
    if(x<-32||x>240)x=24;
    if(y<-32||y>208)y=144;
    g_player_x=(s32)x<<FIX_SHIFT;g_player_y=(s32)y<<FIX_SHIFT;
    g_vx=g_vy=0;g_grounded=(y>=144);g_facing=1;
    g_player_anim_clock=0;g_jump_active=0;g_bounce_active=0;g_camera_x=0;g_camera_tile=0;g_camera_hold=0;
    g_fireball_ammo=3;g_player_hearts=PLAYER_MAX_HEARTS;g_invuln=0;g_hurt_pending_death=0;g_throw_timer=0;g_death_timer=0;

    /* Loader $6CDE reads three words from $6DBA. The first two are the boss
       object position in the same +$80 coordinate system; the third is a
       graphics/metasprite selector retained in generated data/documentation. */
    bp=&g_boss_params[g_current_level*3];
    x=bv?bv->spawn_x:(int)bp[0]-128; y=bv?bv->spawn_y:(int)bp[1]-128;
    if(x<-96||x>256)x=172;
    if(y<-64||y>192)y=104;
    /* Boss 4's own loader record is (336,272), which the ostrich mapping
       turns into the right-hand rest position its script charges back to.
       The hand-picked spawn the placeholder sweep used is not on the axis
       the transcribed handler moves along. */
    if(g_boss_class==2){x=BOSS10_X((int)bp[0]);y=BOSS10_Y((int)bp[1]);}
    /* Boss 3's object X is drawn through a bottom-centre metasprite anchor
       (bbox x0 = -32).  Fold that into the actor so motion, art and hitbox
       all share one coordinate. */
    if(g_boss_class==1){x-=32;y-=32;}
    g_boss_x=(s32)x<<FIX_SHIFT;g_boss_y=(s32)y<<FIX_SHIFT;
    g_boss_ground_y=y;g_boss_vx=-2*FIX_ONE;g_boss_vy=0;
    g_boss_phase=0;g_boss_face=-1;
    g_boss_action=0;g_boss_action_timer=(g_boss_class==1||g_boss_class==2)?-1:0;g_boss_action_dir=0;g_boss_script_index=0;
    g_boss10_floor_y=y;
    g_boss8_crate=-1;

    /* Exact 1P HP table $6D6E: 8,8,10,10,12,12,12 for the seven bosses.
       $6D94 is also exported for the later two-player implementation. */
    g_boss_hp=(int)g_boss_hp_1p[g_current_level];
    if(g_boss_hp<=0)g_boss_hp=8;
    g_boss_timer=0;g_boss_invuln=0;g_boss_defeated=0;g_boss_shot_active=0;g_boss_shot_vy=0;g_boss_drop_rng=0x51F15EEDu;g_boss_random_drop_timer=72;g_after_boss=0;
    g_player_mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;set_game_palette_fade(fade_in?0:16);
    music_start_at_black();
    draw_game_sprites();
    g_state=STATE_STAGE_LOAD;g_state_frame=0;
}

static void boss_take_hit(void)
{
    if(g_boss_invuln||g_boss_defeated||g_boss_hp<=0)return;
    --g_boss_hp;Play_Sound_Sample(SFX_BOSS_HIT,0,0);
    if(g_boss_hp<=0){g_boss_defeated=1;g_boss_vx=0;g_boss_vy=-5*FIX_ONE;g_boss_shot_active=0;Play_Sound_Sample(SFX_BOSS_DEFEAT,0,0);}
    else g_boss_invuln=BOSS_HURT_FRAMES;
}

static void boss_fire_at_player(int speed)
{
    if(g_boss_shot_active)return;
    g_boss_shot_active=1;
    g_boss_shot_x=g_boss_x+(s32)32*FIX_ONE;
    g_boss_shot_y=g_boss_y+(s32)36*FIX_ONE;
    g_boss_shot_vx=((g_player_x<g_boss_x)?-speed:speed)*FIX_ONE;
    g_boss_shot_vy=0;
}

static unsigned boss_random(void)
{
    /* The MD handler samples the hardware RNG while selecting boss actions.
       Keep a deterministic per-room stream so recordings are reproducible
       while the bomb drops still vary instead of arriving on a fixed crate
       cadence. */
    g_boss_drop_rng^=g_boss_drop_rng<<13;
    g_boss_drop_rng^=g_boss_drop_rng>>17;
    g_boss_drop_rng^=g_boss_drop_rng<<5;
    return g_boss_drop_rng;
}

static int boss_bomb_active(void)
{
    int i;
    for(i=0;i<MAX_CRATES;++i)
        if(g_crates[i].active&&g_crates[i].extra==3)return 1;
    return 0;
}

static void boss_drop_bomb(void)
{
    int i,bx,by,x;
    if(boss_bomb_active())return;
    bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT);
    x=bx+8+(int)(boss_random()%49)-24;
    if(x<8)x=8;if(x>224)x=224;
    for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
        Crate *c=&g_crates[i];
        c->active=1;c->carried=0;c->thrown=0;c->extra=3;c->flight_timer=0;
        c->x=(s32)x<<FIX_SHIFT;c->y=(s32)(by+40)<<FIX_SHIFT;
        c->vx=0;c->vy=FIX_ONE;
        return;
    }
}

/* --- Boss 3 ($806A) crate and water-drop objects ------------------------ */

/* $8564 keeps its crate in one dedicated object slot.  Track the same way so
   the handler can walk it down and know when it has landed. */
static int boss8_crate_index(void)
{
    if(g_boss8_crate<0)return -1;
    if(!g_crates[g_boss8_crate].active||g_crates[g_boss8_crate].carried||
       g_crates[g_boss8_crate].thrown){g_boss8_crate=-1;return -1;}
    return g_boss8_crate;
}

static int boss8_drop_crate(void)
{
    int i;
    /* $8570 skips the action only when the room's crate slot is occupied.
       Once Mario picks a box up or throws it that slot is free again, so a
       carried or thrown crate must not hold the next drop off. */
    for(i=0;i<MAX_CRATES;++i)
        if(g_crates[i].active&&!g_crates[i].carried&&!g_crates[i].thrown)return 0;
    for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
        Crate *c=&g_crates[i];
        c->active=1;c->carried=0;c->thrown=0;c->extra=1;c->flight_timer=0;
        /* Crate metasprite $B3 is 24x24 and spawns on the pig's own origin.
           The port anchors the 64x80 pig art by its top-left, so centre the
           box on the body and start it just under the belly. */
        c->x=g_boss_x+(s32)20*FIX_ONE;
        c->y=g_boss_y+(s32)48*FIX_ONE;
        c->vx=0;c->vy=(s32)2*FIX_ONE-80;
        g_boss8_crate=i;
        return 1;
    }
    return 0;
}

/* $8ACE, move 11.  The ostrich spits a pot out behind itself: the object
   starts $32 ahead of its origin on the side it came from, walks one pixel a
   frame the way it is looking away from, and follows the $8C42 arc.  Only
   when the arc runs out does $8B58 copy it into the room's own object slot,
   which is the moment it becomes a box Mario can pick up.  $8ADA skips the
   whole move while that slot is still occupied. */
static int boss10_drop_pot(void)
{
    int i;
    for(i=0;i<MAX_CRATES;++i)
        if(g_crates[i].active&&!g_crates[i].carried&&!g_crates[i].thrown)return 0;
    for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
        Crate *c=&g_crates[i];
        c->active=1;c->carried=0;c->thrown=0;c->extra=1;c->flight_timer=0;
        /* Pot metasprite $B2's bbox starts at (0,8) from the object origin,
           so the two source offsets land these two room positions. */
        c->x=g_boss_x+(s32)(g_boss_action_dir?-2:82)*FIX_ONE;
        c->y=g_boss_y+(s32)56*FIX_ONE;
        c->vx=0;c->vy=-80;
        g_boss8_crate=i;
        return 1;
    }
    return 0;
}

/* $8270.  Every action falls through here.  With no drop live the source
   samples the VDP HV counter, masks $45, and spawns the small water drop
   below itself; otherwise it walks the existing drop down three pixels a
   frame until it splashes at $15E. */
static void boss8_common_tail(void)
{
    if(!g_boss_shot_active){
        if((int)(g_boss_y>>FIX_SHIFT)>BOSS8_DROP_MAX_Y)return;
        if(boss_random()&0x45u)return;
        g_boss_shot_active=1;
        /* $82AA offsets the drop +$14 or -$1C from the object origin; both
           land under the body once the port's top-left anchor is added. */
        g_boss_shot_x=g_boss_x+(s32)(g_boss_action_dir?52:4)*FIX_ONE;
        g_boss_shot_y=g_boss_y+(s32)64*FIX_ONE;
        g_boss_shot_vx=0;
        g_boss_shot_vy=3*FIX_ONE;
    }
}

static void update_boss_motion(void)
{
    int bx=(int)(g_boss_x>>FIX_SHIFT),by=(int)(g_boss_y>>FIX_SHIFT);
    int t=g_boss_timer;
    /* These six motion classes mirror the six 68000 handlers rather than
       forcing all seven encounters through one patrol. Constants are taken
       from the handler states (object coordinates minus the source $80 bias):
       $7526 bounds 12/152 and y 24/104; $806A uses 40/216, 72/176 and
       44/28/22-frame diagonals; $8764 sweeps from -88 to 332; $99BE uses
       48/64/100-frame state timers; $940E chooses a new action every 48. */
    switch(g_boss_class){
    default:
    case 0: { /* levels 3/6: literal action script used by handler $7526 */
        int a;
        if(g_boss_action_timer==0){
            a=g_bowser_action_script[g_boss_action];g_boss_action_dir=g_bowser_direction_script[g_boss_action];
            /* The supplied Bowser pixels are stored left-facing.  The MD
               direction bit is one for rightward actions, so those actions
               need the PC-FX horizontal-invert path. */
            g_boss_face=g_boss_action_dir?-1:1;
        }
        a=g_bowser_action_script[g_boss_action];++g_boss_action_timer;
        if(a==5&&g_boss_action_timer==8){
            /* Metasprite 10 owns the attached flame during wind-up. Create
               the detached object only when the clean breath pose begins. */
            g_boss_shot_active=1;
            g_boss_shot_x=g_boss_x+(s32)(g_boss_action_dir?72:0)*FIX_ONE;
            g_boss_shot_y=g_boss_y+(s32)32*FIX_ONE;
            g_boss_shot_vx=(s32)(g_boss_action_dir?4:-4)*FIX_ONE;
        }
        if(a==1){
            g_boss_x-=(s32)2*FIX_ONE;
            if((int)(g_boss_x>>FIX_SHIFT)<=12){g_boss_x=(s32)12<<FIX_SHIFT;g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;}
        }else if(a==2){
            g_boss_x+=(s32)2*FIX_ONE;
            if((int)(g_boss_x>>FIX_SHIFT)>=152){g_boss_x=(s32)152<<FIX_SHIFT;g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;}
        }else if(a==3){
            g_boss_x-=(s32)4*FIX_ONE;
            if((int)(g_boss_x>>FIX_SHIFT)<=-88){g_boss_x=(s32)-88*FIX_ONE;g_boss_y=(s32)(((int)(g_boss_y>>FIX_SHIFT)==104)?24:104)<<FIX_SHIFT;g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;}
        }else if(a==4){
            g_boss_x+=(s32)4*FIX_ONE;
            if((int)(g_boss_x>>FIX_SHIFT)>=256){g_boss_x=(s32)256*FIX_ONE;g_boss_y=(s32)(((int)(g_boss_y>>FIX_SHIFT)==104)?24:104)<<FIX_SHIFT;g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;}
        }else if(a==5&&g_boss_action_timer>8){
            if(!g_boss_shot_active){g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;}
        }else if(a==6&&g_boss_action_timer>=9){
            g_boss_x=(s32)(((int)(g_boss_x>>FIX_SHIFT)==12)?152:12)<<FIX_SHIFT;
            g_boss_y=(s32)(((int)(g_boss_y>>FIX_SHIFT)==24)?104:24)<<FIX_SHIFT;
            g_boss_action_timer=0;g_boss_action=(g_boss_action+1)%12;
        }
        g_boss_vx=0;g_boss_vy=0;
        break;
    }
    case 1: { /* level 8/$806A, transcribed from the 68000 handler */
        int state,dir,step,duration,vx,vy;
        const unsigned char *p;

        /* Object coordinates are the source's, shifted into room space.  The
           pig's metasprite (IDs 435-438) is anchored bottom-centre with bbox
           x0 = -32, so the drawn top-left is the object x minus 32 on top of
           the ordinary -$80 room offset: room_x = md_x - 160.  That is what
           puts the source's $A8..$158 patrol range at 8..184 here, which is
           exactly the 64-pixel-wide body fitting a 256-pixel room. */

        /* $806A: the source's init bit is represented by a negative timer.
           Keeping the previous state through the transition preserves its
           final pose for the frame in which the source resets that bit. */
        if(g_boss_action_timer<0){
            p=&g_boss8_script[g_boss_script_index*2];
            if(p[0]==0x88){g_boss_script_index=0;p=g_boss8_script;}
            g_boss_action=p[0];g_boss_action_dir=p[1];++g_boss_script_index;
            g_boss_action_timer=0;g_boss_phase=0;
            /* The cleaned strip is stored in the opposite base orientation
               from the MD direction bit: dir 0 moves left and is unflipped. */
            g_boss_face=g_boss_action_dir?-1:1;
        }
        state=g_boss_action;dir=g_boss_action_dir;
        g_boss_vx=0;g_boss_vy=0;
        if(state==1){ /* $8110 */
            g_boss_x-=(s32)2*FIX_ONE;g_boss_y-=(s32)FIX_ONE;
            g_boss_vx=-2*FIX_ONE;g_boss_vy=-FIX_ONE;
            ++g_boss_action_timer;
            if(g_boss_action_timer>=44){
                g_boss_y+=(s32)2*FIX_ONE;
                if((int)(g_boss_x>>FIX_SHIFT)<=BOSS8_LEFT_X){
                    g_boss_x=(s32)BOSS8_LEFT_X<<FIX_SHIFT;g_boss_action_timer=-1;
                }
            }
        }else if(state==2){ /* $814A */
            g_boss_x+=(s32)2*FIX_ONE;g_boss_y+=(s32)FIX_ONE;
            g_boss_vx=2*FIX_ONE;g_boss_vy=FIX_ONE;
            ++g_boss_action_timer;
            if(g_boss_action_timer>=44){
                g_boss_y-=(s32)2*FIX_ONE;
                if((int)(g_boss_x>>FIX_SHIFT)>=BOSS8_RIGHT_X){
                    g_boss_x=(s32)BOSS8_RIGHT_X<<FIX_SHIFT;g_boss_action_timer=-1;
                }
            }
        }else if(state==3||state==4){ /* $8184 / $81FA, arc table $868A */
            step=g_boss_action_timer;
            if(g_boss8_arc[step]==0x88){
                if(state==3){
                    if(dir){g_boss_x=(s32)BOSS8_RIGHT_X<<FIX_SHIFT;g_boss_y=(s32)BOSS8_TOP_Y<<FIX_SHIFT;}
                    else{g_boss_x=(s32)BOSS8_LEFT_X<<FIX_SHIFT;g_boss_y=(s32)BOSS8_BOTTOM_Y<<FIX_SHIFT;}
                }else{
                    if(dir){g_boss_x=(s32)BOSS8_RIGHT_X<<FIX_SHIFT;g_boss_y=(s32)BOSS8_BOTTOM_Y<<FIX_SHIFT;}
                    else{g_boss_x=(s32)BOSS8_LEFT_X<<FIX_SHIFT;g_boss_y=(s32)BOSS8_TOP_Y<<FIX_SHIFT;}
                }
                g_boss_action_timer=-1;
            }else{
                step=g_boss8_arc[step];
                if(state==3){
                    if(dir){g_boss_y-=(s32)step*FIX_ONE;g_boss_x+=(s32)4*FIX_ONE;g_boss_vx=4*FIX_ONE;g_boss_vy=-step*FIX_ONE;}
                    else{g_boss_y+=(s32)step*FIX_ONE;g_boss_x-=(s32)4*FIX_ONE;g_boss_vx=-4*FIX_ONE;g_boss_vy=step*FIX_ONE;}
                }else if(dir){
                    g_boss_y+=(s32)step*FIX_ONE;g_boss_x+=(s32)4*FIX_ONE;g_boss_vx=4*FIX_ONE;g_boss_vy=step*FIX_ONE;
                }else{
                    g_boss_y-=(s32)step*FIX_ONE;g_boss_x-=(s32)4*FIX_ONE;g_boss_vx=-4*FIX_ONE;g_boss_vy=-step*FIX_ONE;
                }
                ++g_boss_action_timer;
            }
        }else if(state==6||state==7){ /* $82F4 / $83E4 */
            /* Both spin handlers open with "if the drop object is live, fall
               through to the common tail" - the pig holds its spin pose and
               does not move while a water drop is still falling.  Skipping
               that check is what made the spin overshoot the room. */
            if(!g_boss_shot_active){
                static const signed char spin6[4][3]={{-4,4,28},{-4,-4,20},{4,-4,16},{4,4,34}};
                static const signed char spin7[7][3]={{4,4,22},{4,-4,24},{-4,-4,6},{-4,4,38},
                                                      {-4,-4,8},{4,-4,28},{4,2,22}};
                const signed char *ph=(state==6)?spin6[g_boss_phase&3]:spin7[(g_boss_phase>6)?6:g_boss_phase];
                vx=ph[0];vy=ph[1];duration=ph[2];
                g_boss_vx=(s32)vx*FIX_ONE;g_boss_vy=(s32)vy*FIX_ONE;
                g_boss_x+=g_boss_vx;g_boss_y+=g_boss_vy;
                ++g_boss_action_timer;
                if(g_boss_action_timer>=duration){
                    g_boss_action_timer=0;++g_boss_phase;
                    if((state==6&&g_boss_phase>=4)||(state==7&&g_boss_phase>=7)){
                        /* $83C2 lands on $158/$130, $8542 on $158/$C8. */
                        g_boss_x=(s32)BOSS8_RIGHT_X<<FIX_SHIFT;
                        g_boss_y=(s32)((state==6)?BOSS8_BOTTOM_Y:BOSS8_TOP_Y)<<FIX_SHIFT;
                        g_boss_action_timer=-1;g_boss_phase=0;
                    }
                }
            }
        }else if(state==8){ /* $8564: hold still and lower a crate */
            /* The source parks a crate object on its own origin, walks it
               down two pixels a frame, and only releases the action once the
               crate has come to rest.  A crate already in the room makes it
               skip the action outright.  Landing is left to the port's own
               crate physics so the box settles on this room's terrain rather
               than the source's hard-coded $120. */
            int ci;
            if(g_boss_action_timer==0){
                g_boss_action_timer=1;
                if(!boss8_drop_crate())g_boss_action_timer=-1;
            }else if((ci=boss8_crate_index())<0)g_boss_action_timer=-1;
            else if(g_crates[ci].vy==0&&g_boss_action_timer>2)g_boss_action_timer=-1;
            else{
                /* $859C walks it down a flat two pixels a frame.  update_crates()
                   adds one gravity step on top before it moves, so hold the
                   velocity here every frame rather than letting it accelerate. */
                g_crates[ci].vy=(s32)2*FIX_ONE-80;
                ++g_boss_action_timer;
            }
        }
        /* $8270: every state but the two spins branches into the common
           tail.  $82F4/$83E4 reach it only when a drop is already live, and
           then only to advance it - which is why a spin never starts one. */
        if(state!=6&&state!=7)boss8_common_tail();
        break;
    }
    case 2: { /* level 10/$8764, transcribed from the 68000 handler */
        int state,dir,step;
        const unsigned char *p;
        /* $8764's init byte is represented by a negative timer, as for the
           pig above: the previous move keeps its pose for the frame in which
           the source clears it. */
        if(g_boss_action_timer<0){
            p=&g_boss10_script[g_boss_script_index*2];
            if(p[0]==0x88){g_boss_script_index=0;p=g_boss10_script;}
            g_boss_action=p[0];g_boss_action_dir=p[1];++g_boss_script_index;
            g_boss_action_timer=0;g_boss_phase=0;
            /* The supplied ostrich strip is drawn facing left, which is the
               source's direction 0, so only rightward moves are flipped. */
            g_boss_face=g_boss_action_dir?-1:1;
        }
        state=g_boss_action;dir=g_boss_action_dir;
        bx=(int)(g_boss_x>>FIX_SHIFT);
        g_boss_vx=0;g_boss_vy=0;
        /* Every move but the hop keeps Y fixed, so the last such Y is the
           storey it is standing on and the one the hop comes back down to. */
        if(state!=8)g_boss10_floor_y=(int)(g_boss_y>>FIX_SHIFT);
        if(state==1||state==2){ /* $8806 / $883A: 80 frames of flat walking */
            step=(state==1)?1:2;
            g_boss_vx=(s32)(dir?step:-step)*FIX_ONE;
            g_boss_x+=g_boss_vx;
            if(++g_boss_action_timer>=0x50)g_boss_action_timer=-1;
        }else if(state==3||state==4){ /* $886E / $88A2: accelerating charge */
            step=g_boss10_ramp[(g_boss_action_timer<64)?g_boss_action_timer:63];
            g_boss_vx=(s32)(state==3?-step:step)*FIX_ONE;
            g_boss_x+=g_boss_vx;++g_boss_action_timer;
            bx=(int)(g_boss_x>>FIX_SHIFT);
            if(state==3?(bx<=BOSS10_MIN_X):(bx>=BOSS10_MAX_X))g_boss_action_timer=-1;
        }else if(state==5){ /* $88D6: re-enter from a corner and walk to centre */
            if(!g_boss_phase){
                g_boss_phase=1;
                g_boss_x=(s32)(dir?BOSS10_MIN_X:BOSS10_MAX_X)<<FIX_SHIFT;
                g_boss_y=(s32)(dir?BOSS10_AIR_Y:BOSS10_GROUND_Y)<<FIX_SHIFT;
            }else{
                g_boss_vx=(s32)(dir?1:-1)*FIX_ONE;
                g_boss_x+=g_boss_vx;
                if((int)(g_boss_x>>FIX_SHIFT)==BOSS10_MID_X)g_boss_action_timer=-1;
            }
        }else if(state==6||state==7){ /* $8946 / $89A0: corner charge, clamped */
            if(!g_boss_phase){
                /* Both handlers place themselves regardless of the script's
                   direction byte: 6 always comes in from the left. */
                g_boss_phase=1;
                g_boss_x=(s32)((state==6)?BOSS10_MIN_X:BOSS10_MAX_X)<<FIX_SHIFT;
                g_boss_y=(s32)((state==6)?BOSS10_AIR_Y:BOSS10_GROUND_Y)<<FIX_SHIFT;
            }else{
                step=g_boss10_ramp[(g_boss_action_timer<64)?g_boss_action_timer:63];
                g_boss_vx=(s32)((state==6)?step:-step)*FIX_ONE;
                g_boss_x+=g_boss_vx;++g_boss_action_timer;
                bx=(int)(g_boss_x>>FIX_SHIFT);
                if(state==6?(bx>=BOSS10_RIGHT_STOP):(bx<=BOSS10_LEFT_STOP)){
                    g_boss_x=(s32)((state==6)?BOSS10_RIGHT_STOP:BOSS10_LEFT_STOP)<<FIX_SHIFT;
                    g_boss_action_timer=-1;
                }
            }
        }else if(state==8){ /* $89F8: the 38-step hop */
            step=g_boss10_hop[g_boss_action_timer];
            if(step==0x88)g_boss_action_timer=-1;
            else{
                g_boss_vy=(s32)(signed char)step*FIX_ONE;
                g_boss_y+=g_boss_vy;++g_boss_action_timer;
            }
        }else if(state==11){ /* $8ACE: spit a pot out behind */
            Crate *c=(g_boss8_crate>=0)?&g_crates[g_boss8_crate]:0;
            if(!g_boss_phase){
                g_boss_phase=1;
                if(!boss10_drop_pot())g_boss_action_timer=-1;
            }else if(!c||!c->active||c->carried||c->thrown)g_boss_action_timer=-1;
            else{
                step=g_boss10_pot[g_boss_action_timer];
                if(step==0x88){
                    /* $8B58 releases it into the room, and $8B80 throws it
                       away again if the arc carried it off the playfield. */
                    bx=(int)(c->x>>FIX_SHIFT);
                    if(bx+CRATE_W<0||bx>256)c->active=0;
                    g_boss8_crate=-1;g_boss_action_timer=-1;
                }else{
                    /* update_crates() adds one gravity step before it moves,
                       so cancel it and carry the table's own displacement. */
                    c->x+=(s32)(dir?-1:1)*FIX_ONE;
                    c->vy=(s32)(signed char)step*FIX_ONE-80;
                    ++g_boss_action_timer;
                }
            }
        }else g_boss_action_timer=-1;   /* 9/10 are the death and win states */
        break;
    }
    case 3: { /* level 13/$8CEC: wait on either carrier, then cross by arc */
        int q=t%160,from=(t/160)&1,to=from^1,step;
        int fx=(int)(g_moving_platforms[from].x>>FIX_SHIFT),fy=(int)(g_moving_platforms[from].y>>FIX_SHIFT)-64;
        int tx=(int)(g_moving_platforms[to].x>>FIX_SHIFT),ty=(int)(g_moving_platforms[to].y>>FIX_SHIFT)-64;
        if(q<48){bx=fx;by=fy;}
        else if(q<112){step=q-48;bx=fx+(tx-fx)*step/64;by=fy+(ty-fy)*step/64-(64-(iabs_i(step-32)*2));}
        else{bx=tx;by=ty;if(q==120)boss_drop_bomb();}
        g_boss_x=(s32)bx<<FIX_SHIFT;g_boss_y=(s32)by<<FIX_SHIFT;
        g_boss_face=(g_player_x<g_boss_x)?-1:1;
        break;
    }
    case 4: /* level 16/$99BE: random 64/48-frame sweeps and falling bells */
        if((t%64)==0){g_boss_phase=(g_boss_phase+1)%5;
            if(g_boss_phase==0){
                static const short tele_x[4]={16,120,48,176};
                static const short tele_y[4]={24,72,112,40};int ti=(t/64)&3;
                g_boss_x=(s32)tele_x[ti]<<FIX_SHIFT;g_boss_y=(s32)tele_y[ti]<<FIX_SHIFT;
                g_boss_vx=-2*FIX_ONE;g_boss_vy=0;
            }
            else if(g_boss_phase==1){g_boss_vx=3*FIX_ONE;g_boss_vy=2*FIX_ONE;}
            else if(g_boss_phase==2){g_boss_vx=-3*FIX_ONE;g_boss_vy=-2*FIX_ONE;}
            else {g_boss_vx=0;g_boss_vy=0;}
            if(g_boss_phase==3&&!g_boss_shot_active){g_boss_shot_active=1;g_boss_shot_x=g_player_x;g_boss_shot_y=-16*FIX_ONE;g_boss_shot_vx=0;g_boss_shot_vy=3*FIX_ONE;}
            if(g_boss_phase==4)boss_fire_at_player(5);
        }
        g_boss_x+=g_boss_vx;g_boss_y+=g_boss_vy;bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT);
        if(bx<8){g_boss_x=(s32)8<<FIX_SHIFT;g_boss_vx=2*FIX_ONE;}if(bx>136){g_boss_x=(s32)136<<FIX_SHIFT;g_boss_vx=-2*FIX_ONE;}
        if(by<8){g_boss_y=(s32)8<<FIX_SHIFT;g_boss_vy=2*FIX_ONE;}if(by>112){g_boss_y=(s32)112<<FIX_SHIFT;g_boss_vy=-2*FIX_ONE;}
        g_boss_face=(g_player_x<g_boss_x)?-1:1;
        break;
    case 5: /* level 19/$940E: grounded elephant patrol/trunk attack */
        if((t%56)==0){g_boss_phase=(g_boss_phase+1)%5;
            if(g_boss_phase==0)g_boss_vx=-2*FIX_ONE;
            else if(g_boss_phase==2)g_boss_vx=2*FIX_ONE;
            else g_boss_vx=0;
            if(g_boss_phase==1||g_boss_phase==4)boss_fire_at_player(4);
        }
        g_boss_x+=g_boss_vx;bx=(int)(g_boss_x>>FIX_SHIFT);
        if(bx<8){g_boss_x=(s32)8<<FIX_SHIFT;g_boss_vx=2*FIX_ONE;}if(bx>136){g_boss_x=(s32)136<<FIX_SHIFT;g_boss_vx=-2*FIX_ONE;}
        /* The elephant art has a 144px canvas; its feet sit on the room's
           platform at y=16, not at the old 49px source-anchor position. */
        g_boss_y=(s32)16<<FIX_SHIFT;g_boss_face=(g_player_x<g_boss_x)?-1:1;
        break;
    }
}

static void update_boss_actor(void)
{
    int i,bx,by,hitx,hity,hitw,hith;
    if(g_boss_defeated){
        /* Source HP-zero path at $16398 changes the boss state instead of
           erasing it. Keep the sprite visible while it launches/falls away. */
        g_boss_vy+=72;if(g_boss_vy>7*FIX_ONE)g_boss_vy=7*FIX_ONE;g_boss_y+=g_boss_vy;
        if((int)(g_boss_y>>FIX_SHIFT)>256){g_after_boss=1;music_stop_for_transition();g_state=STATE_LEVEL_FADE;g_state_frame=0;}
        return;
    }
    if(g_boss_invuln>0)--g_boss_invuln;
    else{
        ++g_boss_timer;
#ifdef FORCE_BOSS_HURT
        if(g_boss_timer==FORCE_BOSS_HURT)boss_take_hit();
#endif
        if(g_boss_invuln==0)update_boss_motion();
    }
    /* Boss 5's $907E object path creates a bomb during its action cycle and
       the handler also samples the source RNG for additional drops. */
    if(g_boss_class==3&&g_boss_invuln==0){
        if(g_boss_random_drop_timer>0)--g_boss_random_drop_timer;
        if(g_boss_random_drop_timer<=0){
            boss_drop_bomb();
            g_boss_random_drop_timer=72+(int)(boss_random()&95);
        }
    }
    /* The source dispenser ($7958) waits while its previous crate object is
       live.  Do the same so unused ammunition cannot carpet the catwalk.
       Bosses whose own handler supplies the throwable are excluded: the pig
       lowers crates from $8564, boss 5 drops bombs, and the ostrich spits a
       pot out of move 11.  Leaving the generic dispenser on for those parks
       a box in the room permanently, and their handlers then skip their own
       drop for ever because the source's occupancy check sees it. */
    if(g_boss_class!=3&&g_boss_class!=1&&g_boss_class!=2&&g_boss_invuln==0&&(g_boss_timer%120)==1){
        int any_crate=0;
        for(i=0;i<MAX_CRATES;++i)if(g_crates[i].active){any_crate=1;break;}
        if(!any_crate)for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
                g_crates[i].active=1;g_crates[i].carried=0;g_crates[i].thrown=0;
                /* Later bosses always dispense their own throwable.  Zero is
                   reserved for Bowser's ordinary wooden crate. */
                g_crates[i].extra=(unsigned char)g_boss_class;
                g_crates[i].x=(s32)(32+((g_boss_timer/120)%6)*32)<<FIX_SHIFT;
                g_crates[i].y=(s32)-16*FIX_ONE;g_crates[i].vx=0;g_crates[i].vy=FIX_ONE;break;
        }
    }
    if(g_boss_shot_active){
        int sx,sy,py;
        g_boss_shot_x+=g_boss_shot_vx;g_boss_shot_y+=g_boss_shot_vy;
        sx=(int)(g_boss_shot_x>>FIX_SHIFT);sy=(int)(g_boss_shot_y>>FIX_SHIFT);
        py=player_body_y()+bowser_room_object_y_offset();
        if(g_boss_class==1){
            if(sx<-16||sx>272||sy>=BOSS8_DROP_END_Y)g_boss_shot_active=0;
            else if(g_invuln==0&&aabb(player_body_x()+4,py+5,8,PLAYER_BODY_H-10,sx,sy,7,16)){hurt_player();g_boss_shot_active=0;}
        }else{
            sy-=16;
            if(sx<-40||sx>272||sy>240)g_boss_shot_active=0;
            else if(g_invuln==0&&aabb(player_body_x()+4,py+5,8,PLAYER_BODY_H-10,sx+4,sy+4,24,24)){hurt_player();g_boss_shot_active=0;}
        }
    }
    bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT);
    /* $15D20 is used by the source boss collision path. Values vary sharply by
       boss class, so use them to tune the broad collision body rather than the
       old universal 56x68 rectangle. */
    if(g_boss_class>0){
        /* Art banks use full 112x96 source canvases so changing pose never
           moves the origin.  Collision remains the recovered actor body,
           not that transparent canvas. */
        static const unsigned char hit_w[5]={48,64,80,112,96};
        static const unsigned char hit_h[5]={64,80,56,64,80};
        hitx=bx+8;hity=by+8;hitw=hit_w[g_boss_class-1];hith=hit_h[g_boss_class-1];
        /* The ostrich art fills the canvas from y+12 down to its feet at
           y+108, so the generic box sat 36 pixels too high and poked through
           the one-way platform its arena hangs 72 pixels above the floor.
           Mario standing up there was hurt by the neck, and his fireballs and
           crates could hit it from a floor away.  Keep the feet as the bottom
           edge and stop the body just below whichever surface it stands on. */
        if(g_boss_class==2){
            hitx=bx+6;hitw=80;hity=by+44;hith=64;
            /* $89F8's hop lifts it 65 pixels without leaving the floor it
               started on, so the body sails up past the one-way platform and
               back down.  Nothing on the far side of that platform may take
               part: while its home floor is the lower one, clip the box at
               the platform's underside; while it is up on the platform, clip
               it at the platform's surface. */
            if(g_boss10_floor_y>=BOSS10_GROUND_Y){if(hity<BOSS10_PLATFORM_BOTTOM)hity=BOSS10_PLATFORM_BOTTOM;}
            else{int bottom=hity+hith;if(bottom>BOSS10_PLATFORM_TOP)bottom=BOSS10_PLATFORM_TOP;hith=bottom-hity;}
            if(hith<=0){hitw=0;hith=0;}
        }
    }
    else{hitx=bx+12;hity=by-8;hitw=72;hith=72;
        if(g_boss_hit_offsets[g_current_level*2]>0)hitx=bx+(g_boss_hit_offsets[g_current_level*2]>>1);
        if(g_boss_hit_offsets[g_current_level*2+1]>0)hith=g_boss_hit_offsets[g_current_level*2+1];
        if(hith<48)hith=64;if(hith>88)hith=88;
        /* Bowser is perched on a one-way ledge.  Trim the upper 16 pixels of
           the recovered body so a crate thrown on the platform above cannot
           hit him while he is below it.  Keep the bottom edge unchanged. */
        hity+=16;hith-=16;}
    if(g_boss_invuln==0&&hitw>0&&hith>0){
        for(i=0;i<MAX_FIREBALLS&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_fireballs[i].active){int fx=(int)(g_fireballs[i].x>>FIX_SHIFT),fy=(int)(g_fireballs[i].y>>FIX_SHIFT);if(aabb(fx+4,fy+4,16,16,hitx,hity,hitw,hith)){boss_take_hit();g_fireballs[i].active=0;}}
        for(i=0;i<MAX_CRATES&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_crates[i].active&&g_crates[i].thrown){int cx=(int)(g_crates[i].x>>FIX_SHIFT),cy=(int)(g_crates[i].y>>FIX_SHIFT)+bowser_room_object_y_offset();if(aabb(cx,cy,24,24,hitx,hity,hitw,hith)){boss_take_hit();g_crates[i].active=0;}}
        if(!g_boss_defeated&&g_invuln==0&&aabb(player_body_x()+4,player_body_y()+bowser_room_object_y_offset()+5,8,PLAYER_BODY_H-10,hitx+4,hity+4,hitw-8,hith-8))hurt_player();
    }
}

static void start_gameplay(int fade_in)
{
    int ycrop;
    /* Every normal level is selected through the 19-entry reconstruction table. */
    g_in_boss=0; g_paused=0;
    music_transition_to_level(g_current_level);
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    init_level_ring();
    init_objects();
    ycrop=(g_current_level==2)?8:0;
    g_player_x=(s32)world_x_from_flat(g_level->spawn_x)<<FIX_SHIFT;
    g_player_y=(s32)world_y_from_flat(g_level->spawn_x,g_level->spawn_y-ycrop)<<FIX_SHIFT;
#ifdef FORCE_PLAYER_X
    g_player_x=(s32)FORCE_PLAYER_X<<FIX_SHIFT;
#endif
#ifdef FORCE_PLAYER_Y
    g_player_y=(s32)FORCE_PLAYER_Y<<FIX_SHIFT;
#endif
    g_vx=g_vy=0;
    g_grounded=1;
    g_facing=1;
    g_player_anim_clock=0;
    g_player_down=0;
    g_jump_active=0; g_jump_index=0;
    g_bounce_active=0; g_bounce_index=0;
    g_camera_hold=0;
    g_fireball_ammo=3;
    g_player_hearts=PLAYER_MAX_HEARTS;
    g_invuln=0;
    g_hurt_pending_death=0;
    g_throw_timer=0;
    g_death_timer=0;
    g_player_mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;
    set_game_palette_fade(fade_in?0:16);
    /* Both VDC outputs are disabled above and all palettes are now black. */
    music_start_at_black();
    update_camera();
    draw_game_sprites();
    g_state=STATE_STAGE_LOAD;
    g_state_frame=0;
}

static void update_play(u16 btn, u16 pressed)
{
    const s32 run_speed=2*FIX_ONE;

    if (pressed&JOY_MODE1) {
        g_debug_mode^=1;
        if (g_debug_mode) {
            g_player_hearts=PLAYER_MAX_HEARTS;g_fireball_ammo=DEBUG_AMMO_DISPLAY;
            g_invuln=0;g_hurt_pending_death=0;
            if(g_player_mode==PMODE_DYING||g_player_mode==PMODE_FADE_OUT)g_player_mode=PMODE_NORMAL;
        }
    }
    if (g_debug_mode) {g_player_hearts=PLAYER_MAX_HEARTS;g_fireball_ammo=DEBUG_AMMO_DISPLAY;}
    /* START pauses active play only.  music_update() remains in the outer
       frame loop, so the Red Book track continues without a fade or seek. */
    if (g_player_mode==PMODE_NORMAL && (pressed&JOY_RUN)) {
        g_paused^=1;
        return;
    }
    if (g_paused) return;
    g_crate_hide=(g_carry_index>=0&&g_grounded&&(btn&JOY_DOWN))?1:0;
    /* DOWN selects the dedicated 32x40 crouch pose only while grounded. The
       jump input below clears it immediately when both buttons are pressed. */
    g_player_down=(g_carry_index<0&&g_grounded&&g_lift_timer==0&&g_throw_timer==0&&(btn&JOY_DOWN))?1:0;
    if (g_drop_platform_timer>0)--g_drop_platform_timer;
    if (g_player_mode==PMODE_DYING) {
        g_player_y+=g_vy;
        g_vy+=64;
        if (g_vy>7*FIX_ONE) g_vy=7*FIX_ONE;
        ++g_death_timer;
        draw_game_sprites();
        if ((int)(g_player_y>>FIX_SHIFT)>256) {
            g_player_mode=PMODE_FADE_OUT;
            g_death_timer=0;
        }
        return;
    }
    if (g_player_mode==PMODE_FADE_OUT) {
        int level;
        ++g_death_timer;
        level=16-(g_death_timer*16/48);
        if (level<0) level=0;
        set_game_palette_fade(level);
        draw_game_sprites();
        if (g_death_timer>=48) {
            Stop_Sound_Sample(SFX_PLAYER_HURT,0);
            /* A normal death consumes one reserve life.  Do not enter the
               CONTINUE room while reserves remain: reload the current level
               from its source spawn instead.  This also deliberately exits a
               boss arena so a death resumes from the level, not mid-boss. */
            if (g_lives>0) --g_lives;
            if (g_lives>0) start_gameplay(1);
            else if (g_continues>0) load_continue_hidden();
            else enter_boot();
        }
        return;
    }
    if (g_player_mode==PMODE_FADE_IN) {
        int level;
        ++g_death_timer;
        level=g_death_timer*16/48;
        if (level>16) level=16;
        set_game_palette_fade(level);
        draw_game_sprites();
        if (g_death_timer>=48) {
            set_game_palette_fade(16);
            g_player_mode=PMODE_NORMAL;
            g_death_timer=0;
        }
        return;
    }

    if (g_invuln>0) {
        --g_invuln;
        if (g_invuln==0) {
            Stop_Sound_Sample(SFX_PLAYER_HURT,0);
            if (g_hurt_pending_death) {
                begin_player_death();
                draw_game_sprites();
                return;
            }
        }
    }

    update_moving_platforms();

    /* Actions are latched before horizontal input so the very first throw
       frame cannot inherit the previous walking velocity/camera movement. */
    if ((pressed&JOY_II) && !g_crate_hide && g_lift_timer==0 && g_throw_timer==0) do_crate_action();
    if ((pressed&JOY_III) && g_lift_timer==0 && g_throw_timer==0) fire_fireball();

    /* Grounded DOWN is a stationary crouch/disguise state. Exclude the jump
       press so DOWN+JUMP can retain horizontal control as it leaves the floor;
       the jump transition below clears the crouch/disguise state. */
    g_camera_hold=0;
    if (g_grounded && (btn&JOY_DOWN) && !(pressed&JOY_I) &&
        g_lift_timer==0 && g_throw_timer==0) {
        g_vx=0;
        g_camera_hold=1;
    } else if (g_lift_timer>0) {
        --g_lift_timer;
        g_vx=0;
    } else if (g_throw_timer>0 && g_grounded) {
        /* On the ground the Mega Drive throw/fire pose is a stationary action.
           In the air, horizontal control remains live throughout the same
           30-frame animation. */
        g_vx=0;
        g_camera_hold=1;
    } else if (btn&JOY_LEFT) {
        g_vx=-run_speed; g_facing=-1;
    } else if (btn&JOY_RIGHT) {
        g_vx=run_speed; g_facing=1;
    } else g_vx=0;

    if (g_grounded && (pressed&JOY_I) && (btn&JOY_DOWN) && g_on_moving_platform!=-1) {
        g_drop_platform_timer=12;g_grounded=0;g_on_moving_platform=-1;g_vy=FIX_ONE;g_player_down=0;g_crate_hide=0;g_camera_hold=0;
    } else if (g_grounded && (pressed&JOY_I) && g_lift_timer==0 && g_throw_timer==0) {
        g_jump_active=1; g_jump_index=0; g_bounce_active=0; g_grounded=0;g_player_down=0;g_crate_hide=0;g_camera_hold=0;
        Play_Sound_Sample(SFX_JUMP,0,0);
    }

    g_prev_body_y=player_body_y();
    resolve_player_horizontal(g_player_x+g_vx);
    /* Camera progression is intentionally one-way, but Mario is not allowed
       to walk behind the already-scrolled viewport. Clamp the object anchor
       to the current left screen edge, as the Mega Drive stage does. */
    if ((int)(g_player_x>>FIX_SHIFT)<g_camera_x) {
        g_player_x=(s32)g_camera_x<<FIX_SHIFT;
        if (g_vx<0) g_vx=0;
    }

    if (g_bounce_active) {
        int dy=(int)g_stomp_bounce_dy[g_bounce_index++];
        g_vy=(s32)dy*FIX_ONE;
        resolve_player_vertical(g_player_y+g_vy);
        if (g_bounce_index>=18) g_bounce_active=0;
    } else if (g_jump_active) {
        int dy;
        if (g_jump_index<=19 && !(btn&JOY_I))
            g_jump_index=44-g_jump_index;
        if (g_jump_index<0) g_jump_index=0;
        if (g_jump_index>44) g_jump_index=44;
        dy=(int)g_md_jump_dy[g_jump_index++];
        g_vy=(s32)dy*FIX_ONE;
        resolve_player_vertical(g_player_y+g_vy);
        if (g_jump_index>=45) g_jump_active=0;
    } else {
        g_vy+=88;
        if (g_vy>5*FIX_ONE) g_vy=5*FIX_ONE;
        resolve_player_vertical(g_player_y+g_vy);
    }

    if (g_throw_timer>0) {
        --g_throw_timer;
        if (!g_grounded || g_throw_timer==0) g_camera_hold=0;
    }

    /* Falling through an open pit is fatal once Mario leaves the 224-line
       source playfield. The visual extension rows remain non-colliding.
       In a stacked level only the bottom screen's floor is the pit: falling
       out of an upper screen simply lands him on the one below. */
    if ((int)(g_player_y>>FIX_SHIFT)>g_level_rows*LEVEL_SCREEN_H) {
        if(g_debug_mode){
            g_player_y=(s32)(g_camera_y+96)<<FIX_SHIFT;g_vy=0;g_jump_active=0;g_bounce_active=0;
            draw_game_sprites();return;
        }
        begin_player_death();
        draw_game_sprites();
        return;
    }

    update_crates();
    update_fireballs();
    if(g_in_boss) update_boss_actor();
    else { update_gems(); update_items(); update_enemies(); }
    if(!g_in_boss)update_enemy_shots();
    if (g_state==STATE_LEVEL_FADE) { draw_game_sprites(); return; }
    if (g_player_mode!=PMODE_NORMAL) { draw_game_sprites(); return; }
    update_camera();
    ++g_player_anim_clock;
    draw_game_sprites();

    /* $16928-$1699A in the 68000 program advances ordinary stages, but for
       level slots 3,6,8,10,13,16,19 it sets FF00A8=1 and re-enters the level
       loader in boss mode. The PC-FX port keeps exactly that boss-bearing list. */
    if(!g_in_boss && (int)(g_player_x>>FIX_SHIFT)>=g_level_width_tiles*8-48){
        int next_level=g_current_level+1;
        g_after_boss=0;
        if(g_boss_level_flags[g_current_level]) music_stop_for_transition();
        else music_prepare_level_transition(next_level);
        g_state=STATE_LEVEL_FADE; g_state_frame=0; return;
    }

    if (pressed&JOY_SELECT) {
        /* This shortcut enters the menu directly rather than the title
           screen, so leave its cue stopped until a later black load selects
           a real destination track. */
        music_stop_for_transition();
        show_black();
        load_menu_asset();
        enter_menu();
    }
}

static void submit_front_satb(u16 *satb)
{
    int i;
    u16 base=(u16)(g_satb_page?SATB_BASE1:SATB_BASE0);
    vdc_set_vram_write(FRONT_CHIP,base);
    for (i=0;i<64*4;++i) vdc_vram_write(FRONT_CHIP,satb[i]);
    vdc_set_satb_address(FRONT_CHIP,base);
    g_satb_page^=1;
}

static void clear_front_sprites(void)
{
    static u16 satb[64*4];
    int i; for (i=0;i<64*4;++i) satb[i]=0;
    submit_front_satb(satb);
}

static void set_front_sprites_enabled(int enabled)
{
    /* SATB replacement is deferred by the VDC, so clearing it alone can leave
       the old, fully lit sprite list on one output frame.  Gate the sprite
       plane itself for every screen transition; the background plane remains
       available for its normal palette fade. */
    vdc_setreg(FRONT_CHIP, VDC_REG_CR,
               enabled ? (u16)(VDC_CR_BB | VDC_CR_SB) : VDC_CR_BB);
}

static void add_front_player(u16 *satb, int *n, int x, int y, int frame, u16 baseword, int facing)
{
    /* Exact-title background conversion uses palette lines 0..13.  Reserve
       line 15 for the clean opening player sprites. */
    u16 ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_1|15);
    u16 a=(u16)(baseword+(frame-1)*PLAYER_FRAME_WORDS_LOCAL);
    if (facing<0) ctrl|=VDC_SPR_X_INVERT;
    set_sprite_entry(satb,(*n)++,x,y,a,ctrl);
    set_sprite_entry(satb,(*n)++,x,y+16,(u16)(a+PLAYER_STRIP_WORDS),ctrl);
    set_sprite_entry(satb,(*n)++,x,y+32,(u16)(a+PLAYER_STRIP_WORDS*2),ctrl);
}

static void set_front_sprite_fade(int level)
{
    int i;
    /* Keep opening sprites completely separate from the exact-title BG
       palettes: line 15 = Mario/Luigi, line 14 = moon/coin. */
    for (i=0;i<16;++i) {
        tetsu_set_palette((u16)(240+i),fade_yuv(g_player_palette[i],level));
        tetsu_set_palette((u16)(224+i),fade_yuv(g_coin_palette[i],level));
    }
}


static void draw_boot_motion(unsigned f)
{
    static u16 satb[64*4];
    int i,n=0,mx,lx,py,walk,cf,fade,cx,cy;
    unsigned sf;
    for (i=0;i<64*4;++i) satb[i]=0;

    /* Sprites cover the moving part of the opening only.  From source frame
       348 the pair stands still and the captured frames carry it themselves,
       so the wall can close over and behind it exactly as on the Mega Drive
       instead of leaving a reconstructed hole around the sprites. */
    if (f<60 || f>=348) {
        submit_front_satb(satb);
        return;
    }

    /* The source animation advances on every other captured frame. */
    sf=f&~1u;

    /* Keep VDC1 background disabled; use only its sprite plane. */
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,VDC_CR_SB);

    /* Positions and pose changes below are taken directly from the captured
       Mega Drive sequence.  The pair stands at the edges, walks inward at
       0.5 px/frame, jumps together, then settles before the title reveal. */
    if (sf<136) {
        mx=40; lx=188; py=153; walk=1;
    } else if (sf<246) {
        int step=(int)(sf-134)/2;
        mx=40+step; lx=188-step; py=153;
        if      (sf<142) walk=1;
        else if (sf<158) walk=2;
        else if (sf<174) walk=1;
        else if (sf<190) walk=3;
        else if (sf<206) walk=1;
        else if (sf<222) walk=2;
        else if (sf<238) walk=1;
        else            walk=3;
    } else if (sf<342) {
        mx=96; lx=132; walk=4;
        if (sf<=294) py=153-(int)(sf-246);
        else         py=105+(int)(sf-294);
    } else {
        mx=96; lx=132; py=153; walk=1;
    }

    fade=(sf<108)?(int)((sf-60)*16/48):16;
    if (fade<0) fade=0; if (fade>16) fade=16;
    set_front_sprite_fade(fade);

    add_front_player(satb,&n,mx,py,walk,BOOT_MARIO_BASE_WORD,1);
    add_front_player(satb,&n,lx,py,walk,BOOT_LUIGI_BASE_WORD,-1);

    /* Coin/moon stays fixed until frame 276, then exits up-left at the exact
       captured -1x/-2y cadence.  Palette line 14 is reserved for it. */
    if (sf<345) {
        if (sf<276) { cx=96; cy=73; }
        else { cx=96-(int)(sf-276); cy=73-(int)(sf-276)*2; }
        cf=(int)((sf/12)&3);
        if (cy>-64) {
            set_sprite_entry(satb,n++,cx,cy,(u16)(COIN_BASE_WORD+cf*1024),
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_4|14));
            set_sprite_entry(satb,n++,cx+32,cy,(u16)(COIN_BASE_WORD+cf*1024+512),
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_4|14));
        }
    }
    submit_front_satb(satb);
}

#define MENU_OVER_TILE_WORD 0x2000
#define MENU_OVER_TILE_INDEX (MENU_OVER_TILE_WORD/TILE_WORDS)

static void show_menu_back(int choice)
{
    int y,x;
    int panel=(choice==MENU_2P)?1:0;
    u16 baseidx=(u16)(FRONT_TILE_BASE_WORD/TILE_WORDS);
    for (y=0;y<30;++y) {
        vdc_set_vram_write(BACK_CHIP,(u16)((32+y)*BG_BAT_W));
        for (x=0;x<32;++x) {
            u16 e=g_menu_combo_asset.bat[y*g_menu_combo_asset.width_tiles+panel*32+x];
            vdc_vram_write(BACK_CHIP,relocate_bat(e,baseidx));
        }
    }
    vdc_set_scroll(BACK_CHIP,0,256);
}

static void menu_overlay_prepare(void)
{
    /* The complete 256x240 menu is on VDC0. Keep both outputs hidden while
       changing the plane arrangement; publish VDC0 on a later frame. */
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    vdc_set_scroll(FRONT_CHIP,0,0);
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_front_sprites();
}

static void menu_overlay_enable(void)
{
    vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,VDC_CR_SB);
}

static void add_menu_32x48(u16 *satb,int *n,int x,int y,u16 addr,int facing,u16 pal)
{
    u16 c=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_1|pal);
    if (facing<0) c|=VDC_SPR_X_INVERT;
    set_sprite_entry(satb,(*n)++,x,y,addr,c);
    set_sprite_entry(satb,(*n)++,x,y+16,(u16)(addr+PLAYER_STRIP_WORDS),c);
    set_sprite_entry(satb,(*n)++,x,y+32,(u16)(addr+PLAYER_STRIP_WORDS*2),c);
}

static void draw_menu_people(int walking)
{
    static u16 satb[64*4];
    int i,n=0;
    int frame=walking?(int)((g_state_frame/5)&3):0;
    int mx=walking?(int)(g_menu_mario_x>>FIX_SHIFT):38;
    for (i=0;i<64*4;++i) satb[i]=0;

    /* Palette line 15 is no longer needed by PUSH START once the menu is up. */
    for (i=0;i<16;++i) tetsu_set_palette((u16)(240+i),fade_yuv(g_menu_mario_palette[i],g_menu_sprite_fade));

    add_menu_32x48(satb,&n,mx,160,
        (u16)(MENU_MARIO_BASE_WORD+frame*PLAYER_FRAME_WORDS_LOCAL),1,15);
    if (g_menu_choice==MENU_2P)
        add_menu_32x48(satb,&n,68,160,MENU_LUIGI_BASE_WORD,1,15);

    /* Never scroll VDC1 during the menu. r5's scroll-based walk caused the
       brief centered/shifted frame during transitions. */
    vdc_set_scroll(FRONT_CHIP,0,0);
    submit_front_satb(satb);
}

static void enter_boot(void)
{
    music_transition_to_title();
    show_black();
    music_start_at_black();
    upload_front_sprites();
    boot_exact_setup();
    g_boot_dual_load_pos=0;
    g_state=STATE_BOOT;
    g_state_frame=0;
    g_prompt_visible=-1;
    g_menu_choice=MENU_1P;
    g_lives=4; g_continues=5; g_current_level=0; g_in_boss=0;
    { int i; for (i=0;i<256;++i) g_title_exact_palette[i]=0x0088; }
}

static void enter_title_fade(void)
{
    /* Freeze the completed exact frame in the same VDC/BAT quadrant before
       touching palettes.  r7 faded using the separately converted static
       title palette, whose palette-line assignment does not match the exact
       frame stream and produced the black/checker corruption on RUN. */
    set_boot_visible(BACK_CHIP);
    vdc_set_scroll(BACK_CHIP,0,256);
    vdc_set_scroll(FRONT_CHIP,0,0);
    clear_front_sprites();
    set_front_sprites_enabled(0);
    g_state=STATE_TITLE_FADE;
    g_state_frame=0;
}

static void enter_menu(void)
{
    g_menu_choice=MENU_1P;
    g_menu_sprite_fade=16;
    show_menu_back(g_menu_choice);
    menu_overlay_prepare();
    set_asset_palette_fade(&g_menu_combo_asset,16);
    menu_overlay_enable();
    g_state=STATE_MENU;
    g_state_frame=0;
    draw_menu_people(0);
}

static void update_frontend(u16 pressed)
{
    if (g_state==STATE_BOOT) {
        if (g_state_frame<BOOT_EXACT_FRAME_COUNT) {
            /* One PC-FX frame for every captured Mega Drive frame 12..600. */
            boot_exact_apply(g_state_frame);
        } else {
            int vis=(int)(((g_state_frame-BOOT_EXACT_FRAME_COUNT)/60)&1)!=0;
            if (g_state_frame==BOOT_EXACT_FRAME_COUNT) {
                /* Frame 600 is already the completed 256x240 no-PUSH title.
                   Do not reload/recenter it: that one-frame replacement was
                   the idle title glitch in r6. */
                g_prompt_visible=0;
                set_boot_visible(BACK_CHIP);
            }
            set_baked_title_prompt(vis);
            if (pressed&(JOY_RUN|JOY_I|JOY_II)) {
                enter_title_fade(); return;
            }
        }
    } else if (g_state==STATE_TITLE_FADE) {
        /* Captured MD timing from START at frame 650: black by ~740, menu
           begins returning after ~760 and is fully bright at ~840. */
        if (g_state_frame<90) {
            int level=16-(int)(g_state_frame*16/90);
            set_title_exact_palette_fade(level);
            clear_front_sprites();
            set_front_sprites_enabled(0);
        } else if (g_state_frame<110) {
            if (g_state_frame==90) {
                /* Give the output-disable write a complete frame to latch.
                   Starting the long upload here can otherwise retain one
                   bright scanline from the old screen at the frame boundary. */
                vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
                vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
                set_title_exact_palette_fade(0);
            } else if (g_state_frame==91) {
                load_menu_asset();
                g_menu_choice=MENU_1P;
                g_menu_sprite_fade=0;
                vdc_set_scroll(BACK_CHIP,0,256);
                menu_overlay_prepare();
                set_asset_palette_fade(&g_menu_combo_asset,0);
            } else if (g_state_frame==92) {
                menu_overlay_enable();
            }
            clear_front_sprites();
        } else if (g_state_frame<190) {
            int level=(int)((g_state_frame-110)*16/80);
            if (level>16) level=16;
            g_menu_sprite_fade=level;
            set_asset_palette_fade(&g_menu_combo_asset,level);
            draw_menu_people(0);
        } else {
            g_menu_choice=MENU_1P;
            g_menu_sprite_fade=16;
            set_asset_palette_fade(&g_menu_combo_asset,16);
            g_state=STATE_MENU;
            g_state_frame=0;
            draw_menu_people(0);
            return;
        }
    } else if (g_state==STATE_MENU) {
        if (pressed&(JOY_UP|JOY_DOWN|JOY_LEFT|JOY_RIGHT)) {
            g_menu_choice^=1;
            show_menu_back(g_menu_choice);
        }
        draw_menu_people(0);
        if (pressed&(JOY_RUN|JOY_I|JOY_II)) {
            Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
            /* The HUD reserve begins at x4.  Continue credits are a separate
               pool; do not consume a reserve merely for entering gameplay. */
            g_lives=4; g_continues=5; g_current_level=0; g_in_boss=0;
#ifdef FORCE_LEVEL
            g_current_level=FORCE_LEVEL;
#endif
            music_stop_for_transition();
            g_state=STATE_MENU_WALK;
            g_state_frame=0;
            g_menu_mario_x=(s32)38*FIX_ONE;
            return;
        }
    } else if (g_state==STATE_MENU_WALK) {
        /* Confirmation walk is deliberately brisk and sprite-based: Mario must
           clear the full 256-pixel raster before the hold/fade begins. */
        if ((int)(g_menu_mario_x>>FIX_SHIFT)<320) {
            g_menu_mario_x+=3*FIX_ONE;
            draw_menu_people(1);
        } else if (g_state_frame<156) {
            clear_bat(FRONT_CHIP,FG_BAT_WORDS);
            clear_front_sprites();
            set_front_sprites_enabled(0);
        } else if (g_state_frame<204) {
            /* Include both endpoints: frame 156 is fully bright and frame
               203 is fully black. The old /48 interpolation stopped at
               level 1, leaving a dim menu frame during the stage handoff. */
            int level=16-(int)((g_state_frame-156)*16/47);
            if (level<0) level=0;
            set_asset_palette_fade(&g_menu_combo_asset,level);
            clear_front_sprites();
            set_front_sprites_enabled(0);
        } else {
            /* Let both disabled outputs persist for one full frame before the
               multi-frame stage upload begins. */
            vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
            vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
            g_state=STATE_STAGE_PREP;
            g_state_frame=0;
            return;
        }
    } else if (g_state==STATE_STAGE_PREP) {
        if (g_state_frame>=1) {
#ifdef FORCE_BOSS
            start_boss(1);
#else
            start_gameplay(1);
#endif
            return;
        }
    } else if (g_state==STATE_STAGE_LOAD) {
        /* CHR/BAT/SATB uploads can span a frame. Keep one additional complete
           black frame after they return before publishing both VDC planes. */
        if (g_state_frame>=2) {
            vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
            vdc_setreg(FRONT_CHIP,VDC_REG_CR,(u16)(VDC_CR_BB|VDC_CR_SB));
            g_state=STATE_PLAY; g_state_frame=0;
            return;
        }
    } else if (g_state==STATE_CONTINUE_LOAD) {
        if (g_state_frame>=1) {
            vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
            vdc_setreg(FRONT_CHIP,VDC_REG_CR,VDC_CR_SB);
            g_state=STATE_CONTINUE; g_state_frame=0;
            return;
        }
    } else if (g_state==STATE_CONTINUE) {
        unsigned elapsed=g_state_frame;
        int level=(elapsed<24)?(int)(elapsed*16/24):16;
        int d=9-(int)((elapsed>24?(elapsed-24):0)/64);
        if (d<0) d=0;
        if (d!=g_continue_digit) g_continue_digit=d;
        set_continue_palette_fade(level);
        draw_continue_digit();
        if ((pressed&(JOY_RUN|JOY_I|JOY_II)) && elapsed>=24 && g_continues>0) {
            --g_continues;
            g_lives=4;
            Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
            music_stop_for_transition();
            g_continue_resume=1; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
        if (elapsed>=24+10*64) {
            music_stop_for_transition();
            g_continue_resume=0; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
    } else if (g_state==STATE_CONTINUE_FADE) {
        int level=16-(int)(g_state_frame*16/32); if (level<0) level=0;
        set_continue_palette_fade(level); clear_front_sprites(); set_front_sprites_enabled(0);
        if (g_state_frame>=32) {
            Stop_Sound_Sample(SFX_MENU_CONFIRM,0);
            if (g_continue_resume) {
                /* Continue resumes the current level from its normal spawn. */
                start_gameplay(1);
            } else enter_boot();
            return;
        }
    } else if (g_state==STATE_LEVEL_FADE) {
        int level=16-(int)(g_state_frame*16/32); if(level<0)level=0;
        set_game_palette_fade(level); draw_game_sprites();
        if(g_state_frame>=32){
            /* Keep the live SATB visible through the fade.  Once both the
               background and sprite palettes have reached black, blank the
               sprite plane before replacing the scene underneath it. */
            clear_front_sprites(); set_front_sprites_enabled(0);
            if(g_after_boss){
                ++g_current_level; g_in_boss=0; g_after_boss=0;
                if(g_current_level>=19){enter_boot();return;}
                start_gameplay(1); return;
            }
            if(g_boss_level_flags[g_current_level]){start_boss(1);return;}
            ++g_current_level;
            if(g_current_level>=19){enter_boot();return;}
            start_gameplay(1); return;
        }
    }
}

int main(void)
{
    contrlr_pad_init(0);
    init_video();
    audio_init();
    music_init();
    enter_boot();

    for (;;) {
        u16 btn, pressed;
        wait_frame();
        if (g_state==STATE_PLAY || g_state==STATE_LEVEL_FADE)
            commit_stage_camera();
        btn = read_buttons();
        pressed = (u16)(btn & (u16)~g_prev_buttons);
        g_prev_buttons = btn;

        if (g_state == STATE_PLAY) update_play(btn, pressed);
        else update_frontend(pressed);
        music_update();
        ++g_state_frame;
    }
    return 0;
}
