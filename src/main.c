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
#include <pcfx/king.h>
#include <pcfx/contrlr.h>
#include <pcfx/v810.h>
#include "assets_generated.h"
#include "boot_exact.h"
#include "boot_dual.h"
#include "audio_stub.h"
#include "music.h"
#include "game_levels_generated.h"
#include "staff_roll.h"
/* Generated VRAM layout: see tools/vram_map.py, which owns every address
   below and rejects banks that would overlap while both are resident. */
#include "vram_map.h"

extern void pcfx_tetsu_wait_frame(void);
/* Present in libpcfx, but missing from the installed vdc.h.  The GMaker 7UP
   notes require saving this selector when an IRQ can interrupt VDC accesses. */
extern u16 vdc_get_last_regnum(int chip);

/* Defined with the rest of the World 3 cellophane layer, below. */
static void overlay_disable(void);

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
/* The fireball's four poses are banked in the stage-object bank but authored
   against a different source sprite line: its pixels are indices 10..15, which
   on the level's own line 2 are the white/yellow/orange/red fire ramp ($AECE's
   art shares those indices in slots 9 and 10).  The bank as a whole sits on
   line 3 (STAGE_OBJECT_PAL_LINE), whose browns are the ledges, so the fireball
   is drawn on line 2 -- already loaded as part of the level palette -- instead. */
#define FIREBALL_SPR_PAL 2
#define CONTINUE_TILE_BASE_INDEX (CONTINUE_TILE_BASE_WORD / TILE_WORDS)
#define CONTINUE_LUIGI_BASE_WORD (CONTINUE_PLAYER_BASE_WORD + 2 * PLAYER_FRAME_WORDS_LOCAL)
#define CONTINUE_PLAYER_SPR_PAL 14
#define CONTINUE_PLAYER_FRAME_TICKS 8

#define PLAYER_FRAME_WORDS_LOCAL 384
#define CONTINUE_PLAYER_FRAME_COUNT 2
#define BACK_PLAYER_FIRST_FRAME_COUNT 6
#define BACK_PLAYER_CARRY_FIRST_FRAME 10
#define BACK_PLAYER_CARRY_FRAME_COUNT 6
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
#define MARIO_BOSS_SPR_PAL 6
#define LUIGI_SPR_PAL 13
#define BOSS_SPR_PAL 14
#define BOSS_HURT_FRAMES 30
#define SPR_ORIGIN_X 0x20
#define SPR_ORIGIN_Y 0x40
#define VDC_SPRITES_PER_LINE 16
#define VDC_SPRITE_PIXELS_PER_LINE 256
#define GAME_SPRITE_CAPACITY 128

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
/* $F78E's shuttle runs 70 pixels left of its record and back. */
#define SHUTTLE_RUN      70
/* These shafts ask the player to cross between shuttles that can retreat from
   him, which leaves too little time to make the jump.  Carry their run past
   the record so each carrier comes farther toward him.  The adjustment is
   confined to the two requested late stages and the earlier shaft that
   already had this accessibility fix.

   Slot 12's shuttles carry that overrun a whole run again, which takes them
   so far right of their record that a rider is walked past the ledge he is
   aiming for and has to jump just to stay aboard.  Give that shaft a short
   overrun instead: enough that the carrier still comes out to meet the
   player, not so much that it runs away with him. */
#define SHUTTLE_OVERRUN_LEVEL_12 11
#define SHUTTLE_OVERRUN_LEVEL_13 12
#define SHUTTLE_OVERRUN_LEVEL_19 18
#define SHUTTLE_OVERRUN       70
#define SHUTTLE_OVERRUN_SHORT 24
#define PLATFORM_KIND_FALLER    6   /* handler 4  */
#define PLATFORM_KIND_DESCEND   7   /* handler 5  */
#define PLATFORM_KIND_ASCEND    8   /* handler 6  */
#define PLATFORM_KIND_PATH      9   /* handler 7  */
#define PLATFORM_KIND_DIAMOND  10   /* handler 11 */
#define PLATFORM_KIND_SINKER   11   /* handler 1: the level 2/6 lift elsewhere */
#define PLATFORM_KIND_BOULDER  12   /* $B086, from the $B31E table */
#define PLATFORM_KIND_PHASE    13   /* handler 8:  $FAF8 */
#define PLATFORM_KIND_DRIFT_R  14   /* handler 9:  $FC1E */
#define PLATFORM_KIND_DRIFT_L  15   /* handler 10: $FDAC */
#define PLATFORM_KIND_LEVER    16   /* handler 17: $10836 */
#define PLATFORM_KIND_RISER    17   /* handler 18: $10892 */
#define PLATFORM_KIND_RETURN   18   /* handler 19: $1097C */
#define PLATFORM_KIND_SPRING   19   /* handler 12: $102B6 */
/* Slot 15's shaft ledges alone: a stationary platform that just cycles
   through four metasprites, never checking for a rider.  Dropping it left
   the shafts of level 14 with no platforms at all -- just a spring and a
   few static ledges, none of the objects the source actually paints in
   between them. */
#define PLATFORM_KIND_PULSE    20   /* handler 13: $104AA */
/* $AECE, the other state of the $B31E table.  Authored under the floor and
   thrown 218 pixels up the $B01E arc, so it is a hazard rather than a carrier;
   only slots 9 and 10 have any.  Its four poses are banked behind its record's
   own shape in phase order, the way the spring's and the pulse's are. */
#define PLATFORM_KIND_FIREBALL 21   /* $AECE, from the $B31E table */
#define PLATFORM_KIND_IS_STAGE_OBJECT(k) ((k)>=PLATFORM_KIND_STATIC)
/* $F934 wraps a descending carrier from y 0x1C2 back to 0x6E and $F980 wraps a
   climbing one from 0x64 back to 0x17C, in the object space the records store,
   which sits 128 lines below the playfield. */
#define PLATFORM_WRAP_BOTTOM (0x1C2 - 128)
#define PLATFORM_WRAP_TOP    (0x6E - 128)
#define PLATFORM_WRAP_CEIL   (0x64 - 128)
#define PLATFORM_WRAP_REBASE (0x17C - 128)
/* Slot 13's shaft lifts run between the same two ends of their shaft, two
   pixels a frame and ungated: $10892 lifts from 0x128 to 0x98 and drops back,
   $1097C drops from 0x98 to 0x128 and climbs back to its own 0x94. */
#define STAGE_LIFT_TOP    (0x98 - 128)
#define STAGE_LIFT_BOTTOM (0x128 - 128)
#define STAGE_LIFT_HOME   (0x94 - 128)
/* $102B6 winds up for $25 frames, then walks the byte table at $10464 -- 69
   entries falling 8,7,6,5,4,3,2,1 -- one entry a frame.  They add up to 252
   pixels, which is the height of the shaft each of the two springs sits at the
   foot of. */
#define SPRING_WIND_FRAMES 0x25
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

/* Fade levels are 0..FADE_FULL rather than 0..16.  The Mega Drive could only
   step its CRAM through eight brightnesses, so its fades visibly staircase;
   the HuC6261 carries a full 8-bit Y, and a 16-step ramp spread over an
   80-frame fade holds each step for five or six frames, which reads as judder
   rather than as a fade.  At 256 levels every frame of every fade lands on a
   distinct luma. */
#define FADE_FULL 256

/* Title -> menu handoff, in frames from the START press.  See the comment in
   update_frontend() for the Mega Drive capture these come from. */
#define TITLE_FADE_OUT      80
#define TITLE_FADE_IN_START 115
#define TITLE_FADE_DONE     178

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
#define STATE_STAFF_LOAD 11
#define STATE_STAFF      12
#define STATE_STAFF_OUT  13

#define PMODE_NORMAL     0
#define PMODE_DYING      1
#define PMODE_FADE_OUT   2
#define PMODE_FADE_IN    3
#define PMODE_REENTRY    4
#define PMODE_OUT        5

#define MULTI_HOVER_FRAMES 60
#define MULTI_INVULN_FRAMES 120

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
    /* Set when a ledge turned the walker round without it moving, cleared the
       moment it does move.  See update_enemies(). */
    unsigned char ledge_pinned;
    /* Jump-over bonus state: 0 idle, 1 airborne player is passing overhead,
       2 the pass already sounded.  See update_enemies(). */
    unsigned char jumped_over;
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
   $BB98's spear, which only ever changes X.  Mode 3 is the bomb $C7CE hangs
   under the flier: $C766 walks it straight down two pixels a frame. */
#define SHOT_MODE_ARC   0
#define SHOT_MODE_ROCK  1
#define SHOT_MODE_SPEAR 2
#define SHOT_MODE_DROP  3
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
    /* $FAF8/$FC1E/$FDAC park their object by writing metasprite 0 for part of
       the cycle; a hidden carrier is neither drawn nor stood on. */
    unsigned char hidden;
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
/* Vertical camera held still for the length of a loop-carrier ride. */
static int g_ride_camera_y;
static int g_ride_camera_hold;
static int g_camera_tile;
/* Route steps whose mode is 2 or 3 stack the next screen above or below the
   current one instead of beside it ($145E2 drives VSRAM instead of HSCROLL).
   Levels 11..12, 15, 16 and 19 of the source use them, so the stage is a path
   through a 2-D grid of screens, not a single horizontal ribbon. */
static int g_camera_y;
static int g_camera_target_y;
static int g_camera_row;   /* world tile row at the top of the streamed window */
static int g_pending_level_rows[FG_BAT_H];
static int g_pending_level_row_count;
static int g_level_rows=1; /* stacked screens; 1 for a purely horizontal level */
static int g_level_cols;
static int g_room;      /* route index of the live room, the port's FF0046 */
static int g_satb_page;
static int g_back_satb_page;
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
/* Pixels a spring launch moved the picture this frame.  $146BC scrolls the
   room by the table step and leaves the player object alone, so the launched
   player holds one screen position for the whole rise while the room slides
   past him.  The port raises the player instead, so the camera has to take the
   identical step in the same frame or the two come apart -- which is what the
   storey camera does on its own: it seeks the live room's home row at 8 pixels
   a frame, and only once the player has already crossed into the room above.
   Set by the launch, consumed and cleared by update_camera(). */
static unsigned char g_spring_camera_step;
static int g_death_timer;
static int g_game_fade_level;
static s32 g_menu_mario_x;
static int g_menu_sprite_fade=FADE_FULL;
static int g_throw_timer;
static int g_camera_hold;
static int g_prev_body_y;
static unsigned g_boot_dual_load_pos;
static u16 g_title_exact_palette[256];

static int g_fireball_ammo;
/* Fireball pickups are the game's power-ups and they follow the player from
   one stage to the next: only a fresh start or a death resets the count. */
static int g_keep_ammo=0;
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
/* $BD54 subtracts $3C from the duck's X before comparing it with the player,
   so it spits as soon as they are within sixty pixels of reaching it. */
#define SPARK_TRIGGER_DX 60
/* $C9D8/$C9E0 carry the thrown ball two pixels a frame. */
#define BALL_THROW_SPEED 2
/* A short patrol gap after each ball: less than the former 0..127-frame wait,
   without reproducing the ROM's near-immediate retry. */
#define GOLFER_RETRY_TICKS 48
/* Size of the ENEMY_ALT and ENEMY_SHOT banks in tools/vram_map.py. */
#define ENEMY_ALT_WORDS 2880
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
/* The later handlers keep more of the source object than the earlier two.
   +$1E is the "a move is running" bit, +$1C the group/cel cursor, +$B the
   pose the handler last selected and +$14 the teleport blank. */
static int g_boss_init,g_boss_group,g_boss_cel,g_boss_anim_step,g_boss_anim_div;
static int g_boss_hidden;
/* Set for the mage's whole $9B88 vanish-and-blink window ($40..$A0), not
   just the frames $g_boss_hidden currently blanks the draw for.  The source
   box test reads the box off the currently drawn metasprite, but a
   flickering actor is still the one that vanished a moment ago and has not
   properly landed yet -- treating only the blank half of the blink as safe
   left the crate/fireball/player tests able to connect on every other
   frame of the reappearance, which is the "still sometimes hits while
   invisible" report. */
static int g_boss_intangible;
/* $9CE6's bell drop (action 8) never appears in any of $9E38's four group
   tables, so the source's own move picker can never land on it; the mage
   would otherwise never drop the crate its bell art exists for.  Force it
   into rotation on a fixed cadence instead of leaving it dead code. */
static int g_boss16_pick_count;
/* Bosses 6 and 7 both put three or four objects in the air at once, which
   the port's single g_boss_shot_* slot could not represent. */
#define MAX_BOSS_SHOTS 4
#define BOSS_SHOT_RING   1   /* $1A1 volley and $1A7 shockwave */
/* $1A3 is a 24x24 three-by-three of tile 1216 -- the same bubble art the
   mage's $1A1 volley takes its top-left quarter from.  The mallet the final
   room lies around as ammunition is $1A4 (tile 1225) and belongs to the
   room's own dispenser, not to anything the elephant spouts. */
#define BOSS_SHOT_BUBBLE 2   /* $1A3, spouted by $965E */
#define BOSS_SHOT_BOMB   3   /* $1A8 on the $9262 arc */
#define BOSS_SHOT_BELL   4   /* $B4 lowered by $9CE6 */
typedef struct { s32 x,y,vx,vy; short timer; unsigned char active,kind,phase; } BossShot;
static BossShot g_boss_shots[MAX_BOSS_SHOTS];
/* $9596 jolts the plane when the elephant lands. */
static int g_screen_shake;
/* $7D04's own two bytes: the countdown to the next mallet and the drop column
   it will use.  The final room hands the player one mallet at a time. */
static int g_boss19_ammo_delay,g_boss19_ammo_slot;
/* $FF002A, which $95CE tests for its own half-speed walk.  The source
   counts passes of the main loop at $6B4C, and in the final room that loop
   turns over on every other field -- so every count in $940E is really a
   thirtieth of a second, and the elephant on hardware hops, spouts and idles
   at half the rate this port gives it.  Running it that slowly was tried and
   is not wanted: at thirty hertz one spout holds the room for eleven seconds
   and a run of them left a minute between jumps.  The port keeps the whole
   shape of the handler and runs it a pass a field. */
static int g_boss19_tick;

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

/* --- Bosses 5, 6 and 7 -------------------------------------------------
   The three handlers after the ostrich were the last placeholders in the
   port: a sinusoid between two lifts, a random sweep and a flat patrol.  All
   three are recovered here from the 68000 the same way $806A and $8764 were.

   They share one coordinate convention.  Source object coordinates are the
   room's plus $80 on both axes, and the drawn corner is the metasprite's
   bbox origin away from that.  $1D22A ($28C, the frog's dragon, 72x64 at
   -32/-64), $1C75A ($25C, the mage, 104x96 at -56/-88) and $1CB42 ($270, the
   elephant, 104x96 at -48/-96) supply those offsets; folding the port's own
   canvas padding in gives the two tables below, so a handler can work in the
   source's actor space and the renderer keeps its top-left anchor. */
#define BOSS_ROOM_BIAS 128
/* $7B74/$7BE0 spawn the two carriers at $68 and run them to $15E. */
#define BOSS13_LIFT_TOP    (0x68-BOSS_ROOM_BIAS)
/* The space matters: "0x15E-" is a single pp-number to the preprocessor. */
#define BOSS13_LIFT_BOTTOM (0x15E - BOSS_ROOM_BIAS)
/* $7BC0 retires the descending carrier at $15E, which is room 222.  The port
   draws boss-room objects seven lines up, so that still leaves the carrier's
   23 rows on screen at 215..237 and it blinks out in full view.  Run it on to
   the first row where the whole sprite has left the 240-line window instead:
   room 247 puts its top edge at screen 240.  The rising carrier needs no such
   extension - it retires at room -24, already clear of the top edge. */
#define BOSS13_ROOM_DY     (-7)
#define BOSS13_LIFT_EXIT   (240 - BOSS13_ROOM_DY)
/* $940E's own storey: the elephant's actor origin while it is not mid-hop. */
#define BOSS19_FLOOR_Y (0x120-BOSS_ROOM_BIAS)
/* $9742's pop line for the spouted bubbles, and $7D04's own two constants:
   the room's mallet is dropped from $68 and comes to rest at $108. */
#define BOSS19_BUBBLE_END_Y (0x15E - BOSS_ROOM_BIAS)
#define BOSS19_AMMO_TOP_Y   (0x68 - BOSS_ROOM_BIAS)
/* Slot 13's actor point is the tank's, not the driver's: $8D72 drives the
   vehicle with metasprites $1A5/$1A6, 80x32 anchored at -40/-32..39/-1, so
   the actor X is 40 pixels into the tank and the actor Y is the tread line
   the storey surfaces are quoted against ($C0 on top, $120 below).  The port
   packs driver and tank into one 112x96 pose with the tank pinned at canvas
   x=16, y=64..95, which puts the actor at (56,96) inside that canvas. */
static const short g_boss_origin_dx[3]={56,56,56};
static const short g_boss_origin_dy[3]={96,88,144};

/* $92F8, walked two bytes at a time by $8CFC: the movement state and the
   source direction flag.  $93CE names sixteen handlers for it. */
static const unsigned char g_boss13_script[]={
    16,0,  1,0,  2,0,  3,1,  4,1, 16,1,  5,1, 12,1, 15,0, 11,0,
     6,1,  7,0,  8,0, 16,0,  1,0, 11,0,  5,1, 12,1,  1,0, 15,0,
    15,0, 11,0,  5,1, 12,1,  1,0, 15,0, 15,0,  2,0,  3,1,  4,1,
    15,1, 15,1,  5,1, 15,1, 15,1, 12,1,  1,0, 11,0,  5,1, 12,1,
    15,0, 11,0, 15,0,  6,1,  7,0,  8,0, 14,0, 14,0, 16,0,  1,0,
    11,0,  5,1, 12,1, 15,0,  1,0, 15,0,  2,0,  3,1,  4,1, 15,1,
    15,1, 13,1, 15,1, 15,1,  5,1, 12,1, 16,0,  1,0, 14,0, 15,0,
    15,0, 11,0,  5,1,  6,1,  7,0,  8,0,  1,0,  2,0,  3,1,  4,1,
    15,1,  5,1,  6,1,  7,0,  8,0, 16,0,  1,0, 11,0, 16,1, 16,1,
    12,1,  2,0,  3,1,  4,1, 15,1, 13,1, 15,1,  5,1, 12,1, 16,0,
    16,0, 11,0, 15,1,  6,1,  7,0,  8,0, 0x88
};
/* $922A/$9235/$9240/$9251: states 11-14 are pure cel scripts, one entry
   every third frame, each entry a (pose, direction) pair. */
static const unsigned char g_boss13_anim11[]={1,0, 2,1, 2,0, 1,1, 1,1, 0x88};
static const unsigned char g_boss13_anim12[]={1,1, 5,1, 5,0, 1,0, 1,0, 0x88};
static const unsigned char g_boss13_anim13[]={1,1, 2,0, 2,1, 1,0, 5,0, 5,1, 1,1, 1,1, 0x88};
static const unsigned char g_boss13_anim14[]={1,0, 2,1, 2,0, 1,1, 5,1, 5,0, 1,0, 1,0, 0x88};
/* $9262: the bomb's flight, one (dy,dx) word pair a frame.  dx is applied on
   the side the throw was aimed at; dy is signed and always downward-positive
   in room space.  The 0x8888 terminator hands the object to the room. */
static const signed char g_boss13_bomb_arc[]={
    -4,2, -4,2, -3,2, -3,2, -3,2, -2,2, -2,2, -1,1, -1,1,  1,1,
     1,1,  1,2,  2,2,  2,2,  2,3,  2,2,  3,2,  3,2,  3,2,  4,2,
     4,2,  4,2, -3,1, -3,1, -2,1, -2,1, -2,1, -1,1, -1,1,  0,0,
     1,1,  1,1,  2,1,  2,1,  2,1,  3,1,  3,1, -128
};

/* $99BE picks its next move from the VDP counter instead of a fixed script.
   $9E20 is the modulus for the group it is currently in and $9E38 points at
   that group's records: (state, next group, direction).  The group is also
   the corner $9E28 teleports it to, which is why the four of them are the
   arena's four quadrants. */
static const unsigned char g_boss16_group_len[4]={11,11,9,9};
static const unsigned char g_boss16_groups[4][11][3]={
  {{1,1,0},{3,2,0},{7,3,0},{5,1,1},{5,2,1},{5,3,0},{6,0,0},{6,0,0},{1,1,0},{3,2,0},{7,3,0}},
  {{1,0,1},{7,2,1},{4,3,1},{5,0,1},{5,2,1},{5,3,0},{6,1,1},{6,1,1},{1,0,1},{7,2,1},{4,3,1}},
  {{3,0,1},{2,1,1},{5,0,0},{5,1,1},{5,3,0},{6,2,1},{6,2,1},{3,0,1},{2,1,1},{0,0,0},{0,0,0}},
  {{2,0,0},{4,1,0},{5,0,0},{5,1,0},{5,2,1},{6,3,0},{6,3,0},{2,0,0},{4,1,0},{0,0,0},{0,0,0}},
};
/* $9E28, in source object coordinates. */
static const short g_boss16_tele[4][2]={{320,224},{192,224},{192,320},{320,320}};

/* $940E is the same machine with six groups and its own X anchors. */
static const unsigned char g_boss19_group_len[6]={7,7,6,7,7,6};
static const unsigned char g_boss19_groups[6][7][3]={
  {{1,1,0},{3,4,0},{5,0,0},{2,5,0},{4,3,0},{6,0,0},{6,0,0}},
  {{1,0,1},{1,2,0},{3,3,0},{5,1,0},{2,4,0},{6,1,0},{6,1,1}},
  {{1,1,1},{2,3,0},{5,2,0},{5,2,1},{6,2,0},{6,2,1},{0,0,0}},
  {{4,0,1},{3,1,1},{2,2,1},{1,4,1},{5,3,1},{6,3,1},{6,3,1}},
  {{1,3,0},{1,5,1},{3,0,1},{5,4,1},{2,1,1},{6,4,0},{6,4,1}},
  {{2,0,1},{1,4,0},{5,5,0},{5,5,1},{6,5,0},{6,5,1},{0,0,0}},
};
/* $9830: state 5 snaps to its group's X before it leaps. */
static const short g_boss19_anchor_x[6]={320,272,224,192,240,288};
/* $983C/$9855/$9876/$988B/$98B4.  Each hop is read one byte a frame: the
   rise subtracts the entry from Y, then $9540 turns the table around and the
   fall adds it, so one table is a complete arc. */
static const unsigned char g_boss19_hop1[]={8,8,8,8,8,7,7,7,7,6,6,6,6,5,5,5,5,4,4,4,4,3,2,1,0x88};
static const unsigned char g_boss19_hop2[]={9,9,9,8,8,8,8,8,8,7,7,7,7,7,7,6,6,6,6,5,5,5,5,4,4,4,4,3,3,2,2,1,0x88};
static const unsigned char g_boss19_hop3[]={9,9,9,9,8,8,8,8,8,8,7,7,7,7,7,7,7,7,7,6,0x88};
static const unsigned char g_boss19_hop4[]={8,8,8,8,8,7,7,7,7,7,7,7,6,6,6,6,6,6,6,5,5,5,5,5,4,4,4,4,4,4,3,3,3,3,3,2,2,2,1,1,0x88};
static const unsigned char g_boss19_hop5[]={9,9,9,9,9,8,8,8,8,8,7,7,7,7,7,6,6,6,6,5,5,4,4,3,3,0x88};
/* $98CE: the rise half of each spouted bubble's flight. */
static const unsigned char g_boss19_bubble_arc[]={9,9,9,8,8,8,8,7,7,7,7,6,6,6,6,5,5,5,4,4,4,3,2,1,0x88};

/* $B844's three bounce arcs, read one signed word a frame while the rock
   rolls two pixels right.  The first is the same 45-step table $17948 gives
   the player's jump; the port's copy of that one carries a deliberate change
   for Mario's clearance, so the rock keeps its own.

   $BA7A is also the hop of the state-14 walker, which reads it a word a frame
   until the $8888 that follows its forty-fourth entry -- hence the separate
   step count below rather than the whole array. */
#define HOPPER_JUMP_STEPS 44
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

/* $B01E: the arc $AECE's red fireball climbs, fifty-two unsigned words that sum
   to 218 -- the leap out of the floor -- and that it walks backwards to fall
   back down.  Their handler is the other state of the $B31E table (state 3,
   where $B086 is state 4), thrown straight up with an X that never changes. */
#define FIREBALL_ARC_LEN 52
static const signed char g_fireball_arc[FIREBALL_ARC_LEN]={
    8,8,8,7,7,7,7,7,6,6,6,6,6,6,5,5,5,5,5,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,2,2,2,2,2,2,1,1,1,1,1
};
/* $AF34/$AF7C drift ten more pixels up and ten back down at the apex. */
#define FIREBALL_APEX_TICKS 10

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

/* Luigi reuses Mario's gameplay pattern bank and selects a green palette.
   This costs no additional gameplay VRAM, which is already tightly packed. */
typedef struct {
    s32 x,y,vx,vy;
    int grounded,facing,anim_clock,jump_active,jump_index,down;
    int mode,death_timer,invuln,hearts,lives,out;
    int carry_index,lift_timer,throw_timer,crate_hide,fireball_ammo;
    u16 prev_buttons;
} SecondPlayer;
static SecondPlayer g_luigi;

/* Bank 1 changes Mario's red clothing indices 1/8/12 to Luigi's green
   ramp 3/2/4.  Applying that index correspondence to Mario's shared patterns
   keeps Luigi green without a second 7,680-word gameplay bank. */
static const u16 g_luigi_reuse_palette[16]={
    0x0088,0x5D63,0x7072,0x5D63,0x4574,0x93A6,0x5CB5,0x4696,
    0x7072,0xD34A,0x784C,0xC75B,0x4574,0x957E,0x2488,0xFF88
};

static int multiplayer_active(void) { return g_menu_choice==MENU_2P; }

static void enter_menu(void);
static void enter_boot(void);
static void start_boss(int fade_in);
static void load_continue_hidden(void);

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

/* ---------------------------------------------------------------------------
   Colour palette publishing.

   HuC6261 doc 2.2.2(5) and 2.2.2 note: "writing data to the colour palette RAM
   while the picture is being displayed shows noise on the screen ... therefore
   read and write the colour palette RAM during the blanking interval".  The
   fades rewrite the whole 256-entry palette every frame, which is far more
   work than one blanking interval could ever be asked to compute, so the game
   never wrote it there: the burst ran wherever the state machine happened to
   be, i.e. mostly over the visible picture.  Emulators do not model the
   conflict, but real hardware scatters dark specks over the screen for as long
   as a fade is running -- exactly the "black dots that clear up" reported for
   the transitions.

   So the writes go to a RAM shadow instead, at whatever point in the frame the
   fade wants to compute them, and only the publish is held back to blanking.
   The shadow also collapses redundant work: a fade step that lands on the same
   YUV value as the last one costs nothing, and the dirty range keeps a
   four-entry overlay tweak from republishing all 256 entries.

   The burst itself follows the documented fast path: CPA (R01) auto-increments
   on every CPW (R02) access, so the address is set once, and a 32-bit I/O
   write "accesses the same register twice, low half then high half, which can
   be used to write two palette addresses in one instruction" (doc 2.1.1), so
   256 entries cost 128 stores and comfortably fit in the 22-line interval. */
#define VCE_REG_PORT   0x300
#define VCE_DATA_PORT  0x304
#define VCE_REG_CPA    1   /* colour palette address */
#define VCE_REG_CPW    2   /* colour palette data, write */

/* The status word at the address port carries the raster in bits 13..5.  The
   262-line mode this port programs blanks lines 0..21; stop short of the end
   of that window so a full 256-entry burst still lands inside it. */
#define VCE_RASTER_MASK   0x3FE0
#define VCE_LAST_BLANK_RASTER 16

static u16 g_pal_shadow[256];
static int g_pal_dirty_lo=256, g_pal_dirty_hi=-1;

/* Deliberately not inlined: it is called from three dozen sites, several of
   them 256-entry loops, and the image has only a few kilobytes of headroom
   under the 2004992-byte ceiling. */
static __attribute__((noinline)) void pal_set(u16 index, u16 color)
{
    if (g_pal_shadow[index]==color) return;
    g_pal_shadow[index]=color;
    if ((int)index<g_pal_dirty_lo) g_pal_dirty_lo=(int)index;
    if ((int)index>g_pal_dirty_hi) g_pal_dirty_hi=(int)index;
}

/* Wait until the raster is inside the vertical blanking interval.  A single
   read of the status port can return a transitional raster (the same HuC6261
   erratum pcfx_tetsu_wait_frame() works around), so two reads must agree. */
static void pal_wait_blank(void)
{
    for (;;) {
        u16 a=in16(VCE_REG_PORT), b=in16(VCE_REG_PORT);
        if (((a^b)&VCE_RASTER_MASK)!=0) continue;
        if (((a&VCE_RASTER_MASK)>>5)<VCE_LAST_BLANK_RASTER) return;
    }
}

/* Publish everything pal_set() has changed since the last call.  Safe to call
   from anywhere: it parks on blanking first, and costs nothing when the caller
   has just returned from wait_frame(). */
static void pal_publish(void)
{
    int lo=g_pal_dirty_lo, hi=g_pal_dirty_hi, i;
    if (hi<lo) return;
    g_pal_dirty_lo=256; g_pal_dirty_hi=-1;
    pal_wait_blank();
    out16(VCE_REG_PORT,VCE_REG_CPA);
    out16(VCE_DATA_PORT,(u16)lo);
    out16(VCE_REG_PORT,VCE_REG_CPW);
    /* The raster IRQ can land inside this burst, but it saves and restores the
       address port, and CPA is left alone by it, so the run continues. */
    for (i=lo;i+1<=hi;i+=2)
        out32(VCE_DATA_PORT,
              (u32)g_pal_shadow[i]|((u32)g_pal_shadow[i+1]<<16));
    if (i==hi) out16(VCE_DATA_PORT,g_pal_shadow[hi]);
}

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

static u16 read_buttons_2(void)
{
    return (u16)(contrlr_pad_read(1) & 0xFFFFu);
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
    for (i = 0; i < 0x100; ++i) vdc_vram_write(FRONT_CHIP, 0);
    vdc_set_vram_write(FRONT_CHIP, SATB_BASE1);
    for (i = 0; i < 0x100; ++i) vdc_vram_write(FRONT_CHIP, 0);
    vdc_set_satb_address(FRONT_CHIP, SATB_BASE0);
    g_satb_page = 0;
    vdc_set_vram_write(BACK_CHIP, BACK_SATB_BASE0);
    for (i = 0; i < 0x100; ++i) vdc_vram_write(BACK_CHIP, 0);
    vdc_set_vram_write(BACK_CHIP, BACK_SATB_BASE1);
    for (i = 0; i < 0x100; ++i) vdc_vram_write(BACK_CHIP, 0);
    vdc_set_satb_address(BACK_CHIP, BACK_SATB_BASE0);
    g_back_satb_page = 0;
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
    for (i = 0; i < 256; ++i) pal_set((u16)i, 0x0088);
    /* This is the "the screen is black now" primitive, and its callers go on
       to load for many frames without returning to the main loop, so the
       blackout cannot wait for the loop's own publish. */
    pal_publish();
    vdc_set_scroll(BACK_CHIP, 0, 0);
    vdc_set_scroll(FRONT_CHIP, 0, 0);
    clear_satb();
    overlay_disable();
}

/* The HuC6261 picture-mode register also carries the KING BG0 display bit, so
   turning the cellophane layer on and off means rewriting the whole mode. */
static void set_display_layers(int king_bg0, int king_bg1)
{
    tetsu_set_video_mode(TETSU_LINES_262, 0, TETSU_DOTCLOCK_5MHz,
                         TETSU_COLORS_16, TETSU_COLORS_16,
                         1, 1, king_bg0, king_bg1, 0, 0, 0);
}

static void init_video(void)
{
    int i;
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
    set_display_layers(0,0);
    clear_satb();
    /* The shadow has to start out matching nothing the hardware holds, or the
       first pal_set() of a value it already believes is live would be dropped.
       Seed it with the black the first show_black() wants and mark the whole
       range dirty so that publish is unconditional. */
    for (i = 0; i < 256; ++i) g_pal_shadow[i]=0x0088;
    g_pal_dirty_lo=0; g_pal_dirty_hi=255;
}

/* ---- World 3's forest glare ------------------------------------------
   Levels 6 and 7 are the only screens in the game that pair a full-intensity
   white sky -- nearly a quarter of the picture -- with a near-black canopy
   lattice drawn over it, and the port scrolls that pair at 2 pixels a frame
   where the Mega Drive's 30 Hz main loop moved it about 1.15.  A high-contrast
   grating in fast motion is exactly the pattern the eye cannot settle on, and
   it is why these two levels read as harsh when nothing else in the port does.

   The layers themselves are innocent: measured across 95 consecutive scrolling
   frames, every 8-line band of the picture -- both VDC planes and the KING BG0
   band -- moves by the identical step, so there is no split and no one-frame
   lag between KING and the VDCs to fix.

   What is left is the contrast itself.  Pull the two extremes of the level's
   own palette towards the middle with a cubic shoulder: a colour already in
   the mid range is left where the artist put it (Y=170 moves by one step),
   while pure white and pure black each give up FOREST_GLARE_TENTHS/10 of their
   distance from mid grey.  The sprite lines are republished after this, so
   Mario and the cast keep their full range and stay legible against the
   softened scenery. */
#define FOREST_GLARE_TENTHS 3      /* 0 disables; 3 takes white 255 -> 217 */

static int level_softens_glare(void)
{
    return !g_in_boss && (g_current_level==6 || g_current_level==7);
}

static u16 soften_yuv(u16 c)
{
    int y=(c>>8)&255, d=y-128;
    y=128+d-(FOREST_GLARE_TENTHS*d*d*d)/(10*128*128);
    if(y<0)y=0; if(y>255)y=255;
    return (u16)((y<<8)|(c&0x00FF));
}

static u16 fade_yuv(u16 c, int level)
{
    int y=(c>>8)&255, u=(c>>4)&15, v=c&15;
    if (level < 0) level=0;
    if (level > FADE_FULL) level=FADE_FULL;
    y=(y*level)/FADE_FULL;
    u=8+((u-8)*level)/FADE_FULL;
    v=8+((v-8)*level)/FADE_FULL;
    return (u16)((y<<8)|((u&15)<<4)|(v&15));
}


/* Sixteen entries of one palette line at one fade level.  Kept out of line and
   shared by every caller: the fade paths would otherwise expand this loop two
   dozen times, and the image has very little room under its size ceiling. */
static __attribute__((noinline)) void pal_set_line(int line,const unsigned short *src,int level)
{
    int i;
    for (i=0;i<16;++i) pal_set((u16)(line*16+i),fade_yuv(src[i],level));
}

static void set_asset_palette_fade(const BgAsset *a, int level)
{
    int i;
    for (i=0;i<256;++i) pal_set((u16)i,fade_yuv(a->palette[i],level));
}

static void set_title_exact_palette_fade(int level)
{
    int i;
    for (i=0;i<256;++i)
        pal_set((u16)i,fade_yuv(g_title_exact_palette[i],level));
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
    int panel,x,y;
    /* By the time this runs the title has faded to black, so the title-only
       CHR region can be reused without exposing a transient tile mismatch.
       CHR and the matching BAT rows 32..61 must be written together: uploading
       one without the other leaves the menu interpreting stale title cells.

       The background BAT is 64 cells wide, exactly enough to keep the 1P and
       2P panels next to each other.  Populate both while output is hidden so
       menu navigation can select one with a scroll change instead of rewriting
       the visible BAT during active display. */
    upload_tiles(BACK_CHIP,FRONT_TILE_BASE_WORD,&g_menu_combo_asset);
    for(y=0;y<30;++y){
        vdc_set_vram_write(BACK_CHIP,(u16)((32+y)*BG_BAT_W));
        for(panel=0;panel<2;++panel)
            for(x=0;x<32;++x){
                u16 e=g_menu_combo_asset.bat[y*g_menu_combo_asset.width_tiles+panel*32+x];
                /* The captured 2P panel is a sparse overlay on the complete
                   1P/common scene; zero cells mean "keep the base". */
                if(panel && !e)
                    e=g_menu_combo_asset.bat[y*g_menu_combo_asset.width_tiles+x];
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
        pal_set(pi,value);
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
    if (index < 0 || index >= GAME_SPRITE_CAPACITY) return;
    e = satb + index * 4;
    e[0] = (u16)(y + SPR_ORIGIN_Y);
    e[1] = (u16)(x + SPR_ORIGIN_X);
    e[2] = VDC_SPR_PATTERN(addr);
    e[3] = ctrl;
}

static int sprite_height(u16 ctrl)
{
    return ((ctrl&VDC_SPR_Y_HEIGHT_4)==VDC_SPR_Y_HEIGHT_4)?64:
           (ctrl&VDC_SPR_Y_HEIGHT_2)?32:16;
}

/* VDC0's background patterns occupy the normal player addresses.  Translate
   only player resources into their explicit auxiliary mirrors when the SATB
   allocator moves an entry to that chip. */
static u16 back_sprite_address(u16 address)
{
    u16 carry_front=(u16)(PLAYER_BASE_WORD+
        (BACK_PLAYER_CARRY_FIRST_FRAME-1)*PLAYER_FRAME_WORDS_LOCAL);
    if(address>=PLAYER_BASE_WORD&&
       address<PLAYER_BASE_WORD+BACK_PLAYER_FIRST_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL)
        return (u16)(BACK_MIRROR_PLAYER_WORD+address-PLAYER_BASE_WORD);
    if(address>=carry_front&&
       address<carry_front+BACK_PLAYER_CARRY_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL)
        return (u16)(BACK_MIRROR_PLAYER_CARRY_WORD+address-carry_front);
    if(address>=THROW_BASE_WORD&&address<THROW_BASE_WORD+448)
        return (u16)(BACK_MIRROR_THROW_WORD+address-THROW_BASE_WORD);
    if(address>=GAMEPLAY_DOWN_MARIO_BASE_WORD&&
       address<GAMEPLAY_DOWN_MARIO_BASE_WORD+PLAYER_FRAME_WORDS_LOCAL)
        return (u16)(BACK_MIRROR_DOWN_MARIO_WORD+address-GAMEPLAY_DOWN_MARIO_BASE_WORD);
    if(address>=GAMEPLAY_DOWN_LUIGI_BASE_WORD&&
       address<GAMEPLAY_DOWN_LUIGI_BASE_WORD+PLAYER_FRAME_WORDS_LOCAL)
        return (u16)(BACK_MIRROR_DOWN_LUIGI_WORD+address-GAMEPLAY_DOWN_LUIGI_BASE_WORD);
    return address;
}

/* Split one priority-ordered logical list across both HuC6270s.  VDC1 wins
   the PC-FX VDC pre-mixer whenever both chips produce a non-transparent
   pixel, so a later (lower-priority) entry may use VDC1 only when no earlier
   VDC0 entry covers any of the same scanlines.  That conservative barrier,
   plus retaining source order inside each SATB, preserves sprite overlap
   priority while doubling the 16-sprite/256-pixel scanline budget. */
static void submit_game_satbs(const u16 *logical,const unsigned char *group_len,
                              int logical_count)
{
    static u16 front[64*4],back[64*4];
    /* Pack sprite count in the low byte and fetched 16-pixel units in the
       high byte.  Both hardware limits are 16, so this keeps all allocator
       scratch state below one 2 KiB DRAM page. */
    u16 front_line[240]={0},back_line[240]={0};
    int i,j,nf=0,nb=0;
    u16 front_base=(u16)(g_satb_page?SATB_BASE1:SATB_BASE0);
    u16 back_base=(u16)(g_back_satb_page?BACK_SATB_BASE1:BACK_SATB_BASE0);
    for(i=0;i<64*4;++i)front[i]=back[i]=0;

    for(i=0;i<logical_count;){
        int k,len=group_len[i]?group_len[i]:1;
        int use_front=(nf+len<=64),use_back=(nb+len<=64);
        if(i+len>logical_count)len=logical_count-i;

        /* Almost every logical object is one SATB entry.  Handle it directly
           instead of building and clearing a 240-line temporary group map. */
        if(len==1){
            const u16 *e=logical+i*4;
            int y=(int)e[0]-SPR_ORIGIN_Y,h=sprite_height(e[3]);
            int first=y<0?0:y,last=y+h;
            u16 add=(u16)(1+(((e[3]&VDC_SPR_X_WIDTH_2)?2:1)<<8));
            if(last>240)last=240;
            for(j=first;j<last;++j){
                u16 f=front_line[j],b=back_line[j];
                if((b&0xff)||(f&0xff)>=VDC_SPRITES_PER_LINE||
                   (f>>8)+(add>>8)>VDC_SPRITE_PIXELS_PER_LINE/16)use_front=0;
                if((b&0xff)>=VDC_SPRITES_PER_LINE||
                   (b>>8)+(add>>8)>VDC_SPRITE_PIXELS_PER_LINE/16)use_back=0;
            }
            if(use_front){
                for(j=0;j<4;++j)front[nf*4+j]=e[j];
                ++nf;
                for(j=first;j<last;++j)front_line[j]=(u16)(front_line[j]+add);
            }else if(use_back){
                for(j=0;j<4;++j)back[nb*4+j]=e[j];
                back[nb*4+2]=back_sprite_address(e[2]);
                ++nb;
                for(j=first;j<last;++j)back_line[j]=(u16)(back_line[j]+add);
            }
            ++i;
            continue;
        }

        {
            u16 add_line[240];
            int first=240,last=0;
            /* A moving platform is one visual object even when it occupies
               eight SATB entries.  Measure the complete object before
               selecting a VDC; assigning its strips independently lets them
               migrate across the fixed VDC1-over-VDC0 mixer as scanline
               pressure changes. */
            for(k=0;k<len;++k){
                const u16 *e=logical+(i+k)*4;
                int y=(int)e[0]-SPR_ORIGIN_Y,h=sprite_height(e[3]);
                int sprite_first=y<0?0:y,sprite_last=y+h;
                if(sprite_last>240)sprite_last=240;
                if(sprite_first<sprite_last){
                    if(sprite_first<first)first=sprite_first;
                    if(sprite_last>last)last=sprite_last;
                }
            }
            for(j=first;j<last;++j)add_line[j]=0;
            for(k=0;k<len;++k){
                const u16 *e=logical+(i+k)*4;
                int y=(int)e[0]-SPR_ORIGIN_Y,h=sprite_height(e[3]);
                u16 add=(u16)(1+(((e[3]&VDC_SPR_X_WIDTH_2)?2:1)<<8));
                int sprite_first=y<0?0:y,sprite_last=y+h;
                if(sprite_last>240)sprite_last=240;
                for(j=sprite_first;j<sprite_last;++j)
                    add_line[j]=(u16)(add_line[j]+add);
            }
            for(j=first;j<last;++j){
                u16 add=add_line[j],f=front_line[j],b=back_line[j];
                if((f&0xff)+(add&0xff)>VDC_SPRITES_PER_LINE||
                   (f>>8)+(add>>8)>VDC_SPRITE_PIXELS_PER_LINE/16)use_front=0;
                if((b&0xff)+(add&0xff)>VDC_SPRITES_PER_LINE||
                   (b>>8)+(add>>8)>VDC_SPRITE_PIXELS_PER_LINE/16)use_back=0;
                if(b&0xff)use_front=0;
            }
            if(use_front){
                for(k=0;k<len;++k)for(j=0;j<4;++j)front[(nf+k)*4+j]=logical[(i+k)*4+j];
                nf+=len;
                for(j=first;j<last;++j)front_line[j]=(u16)(front_line[j]+add_line[j]);
            }else if(use_back){
                for(k=0;k<len;++k)for(j=0;j<4;++j)back[(nb+k)*4+j]=logical[(i+k)*4+j];
                for(k=0;k<len;++k)back[(nb+k)*4+2]=
                    back_sprite_address(logical[(i+k)*4+2]);
                nb+=len;
                for(j=first;j<last;++j)back_line[j]=(u16)(back_line[j]+add_line[j]);
            }
        }
        i+=len;
    }

    /* Prepare both hidden pages completely, then queue both SATB DMAs.  They
       become visible together at the following VDW->VCR boundary. */
    vdc_set_vram_write(FRONT_CHIP,front_base);
    for(i=0;i<64*4;++i)vdc_vram_write(FRONT_CHIP,front[i]);
    vdc_set_vram_write(BACK_CHIP,back_base);
    for(i=0;i<64*4;++i)vdc_vram_write(BACK_CHIP,back[i]);
    vdc_set_satb_address(FRONT_CHIP,front_base);
    vdc_set_satb_address(BACK_CHIP,back_base);
    g_satb_page^=1;g_back_satb_page^=1;
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

/* VDC0's gameplay pattern space above the background banks is otherwise idle.
   Mirror every world-object bank there while both outputs are hidden so an
   entry can move between VDC sprite engines without changing its pattern
   number or exposing a partially-uploaded frame. */
static void upload_sprite_words(int chip,u16 address,const u16 *words,unsigned count)
{
    unsigned i;
    vdc_set_vram_write(chip,address);
    for(i=0;i<count;++i)vdc_vram_write(chip,words[i]);
}

static void upload_world_sprite_words(u16 address,const u16 *words,unsigned count)
{
    upload_sprite_words(FRONT_CHIP,address,words,count);
    upload_sprite_words(BACK_CHIP,address,words,count);
}

static void upload_throw_sprite_words(int chip,u16 address)
{
    upload_sprite_words(chip,address,g_player_death_words,320);
    /* The final 16x16 cell follows the 32x32 cell's 128-word alignment gap. */
    upload_sprite_words(chip,(u16)(address+384),g_player_death_words+320,64);
}

static void upload_hud_heart_strip(void)
{
    static u16 words[192];
    int i;
    /* The source heart is an 8x8 image in the upper half of a 16x16 cell.
       Duplicate those eight bits into the right half. Two paired cells make
       four adjacent hearts in one 32-pixel sprite; the original cell at the
       tail supplies odd counts. */
    for(i=0;i<64;++i){
        u16 pair=(u16)(g_hud_heart_words[i]|(g_hud_heart_words[i]>>8));
        words[i]=pair;words[64+i]=pair;words[128+i]=g_hud_heart_words[i];
    }
    upload_world_sprite_words(HUD_HEART_STRIP_BASE_WORD,words,192);
}

static void upload_game_sprites(void)
{
    int i,j,k,uniq=0,walk_top=0,hit_top=0,alt_top=0,shot_top=0;
    const unsigned short *seen[12];
    /* Keep a dedicated copy of the player palette for boss rooms. Normal
       stages use source palette line 0 directly; every generated level carries
       the same Mario/object colors there. */
    for (i=0;i<16;++i) pal_set((u16)(MARIO_BOSS_SPR_PAL*16+i),g_player_palette[i]);
    for (i=0;i<16;++i) pal_set((u16)(LUIGI_SPR_PAL*16+i),g_luigi_reuse_palette[i]);
    for (i=0;i<16;++i) pal_set((u16)(CONTINUE_PLAYER_SPR_PAL*16+i),g_continue_player_palette[i]);
    if(g_in_boss)for(i=0;i<16;++i)pal_set((u16)(BOSS_CROUCH_SPR_PAL*16+i),g_continue_player_palette[i]);

    upload_sprite_words(FRONT_CHIP,PLAYER_BASE_WORD,g_player_frame_words,
                        PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL);
    upload_sprite_words(BACK_CHIP,BACK_MIRROR_PLAYER_WORD,g_player_frame_words,
                        BACK_PLAYER_FIRST_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL);
    upload_sprite_words(BACK_CHIP,BACK_MIRROR_PLAYER_CARRY_WORD,
                        g_player_frame_words+
                        (BACK_PLAYER_CARRY_FIRST_FRAME-1)*PLAYER_FRAME_WORDS_LOCAL,
                        BACK_PLAYER_CARRY_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL);
    upload_throw_sprite_words(FRONT_CHIP,THROW_BASE_WORD);
    upload_throw_sprite_words(BACK_CHIP,BACK_MIRROR_THROW_WORD);
    vdc_set_vram_write(FRONT_CHIP,CONTINUE_PLAYER_BASE_WORD);
    for (i=0;i<768;++i) vdc_vram_write(FRONT_CHIP,g_continue_mario_words[i]);
    vdc_set_vram_write(FRONT_CHIP,CONTINUE_LUIGI_BASE_WORD);
    for (i=0;i<768;++i) vdc_vram_write(FRONT_CHIP,g_continue_luigi_words[i]);
    upload_sprite_words(FRONT_CHIP,GAMEPLAY_DOWN_MARIO_BASE_WORD,g_player_down_words,384);
    upload_sprite_words(BACK_CHIP,BACK_MIRROR_DOWN_MARIO_WORD,g_player_down_words,384);
    upload_sprite_words(FRONT_CHIP,GAMEPLAY_DOWN_LUIGI_BASE_WORD,g_luigi_down_words,384);
    upload_sprite_words(BACK_CHIP,BACK_MIRROR_DOWN_LUIGI_WORD,g_luigi_down_words,384);
    /* A full two-player HUD is 18 entries / 288 sprite pixels on its busiest
       scanline, so its tail legitimately spills to VDC0.  Mirror all HUD art,
       not only world objects; otherwise Luigi's last hearts name unrelated
       background VRAM on the auxiliary chip. */
    upload_world_sprite_words(HUD_LIFE_BASE_WORD,g_hud_life_words,128);
    upload_world_sprite_words(HUD_FIRE_BASE_WORD,g_hud_fire_words,64);
    upload_world_sprite_words(CRATE_BASE_WORD,g_crate_sprite_words,256);
    /* Boss 3 uses the supplied pot object; upload it with the shared gameplay
       bank so entering that room does not need a mid-frame pattern transfer. */
    upload_world_sprite_words(BOSS_POT_BASE_WORD,g_boss_pot_words,256);
    upload_world_sprite_words(GEM_BASE_WORD,g_gem_sprite_words,192);
    upload_world_sprite_words(FIREBALL_BASE_WORD,g_fireball_sprite_words,256);
    if(!g_in_boss)upload_world_sprite_words(ITEM_BASE_WORD,g_item_sprite_words,ITEM_KIND_COUNT*ITEM_CELL_WORDS);
    upload_world_sprite_words(HUD_DIGIT_BASE_WORD,g_hud_digit_words,640);
    upload_hud_heart_strip();
    if (!g_in_boss && (g_current_level==1 || g_current_level==5 || g_current_level==7)) {
        const u16 *platform_words=(g_current_level==5)?g_level6_platform_words:
                                  (g_current_level==7)?g_level8_platform_words:
                                  g_level2_platform_words;
        upload_world_sprite_words(PLATFORM_BASE_WORD,platform_words,192);
    }
    /* Every other level's stage objects share one bank and one palette line. */
    if (!g_in_boss) {
        const StageObjectData *so=stage_objects();
        if (so->word_count) {
            upload_world_sprite_words(STAGE_OBJECT_BASE_WORD,so->words,so->word_count);
            for(i=0;i<16;++i)pal_set((u16)(LEVEL8_OBJ_SPR_PAL*16+i),so->palette[i]);
        }
    }

    /* Level 8's two sliding wedges (metasprites 168/169). */
    if (!g_in_boss && g_current_level==7) {
        upload_world_sprite_words(LEVEL8_WEDGE_R_BASE_WORD,g_level8_wedge_right_words,768);
        upload_world_sprite_words(LEVEL8_WEDGE_L_BASE_WORD,g_level8_wedge_left_words,384);
    }

    /* F3AE/F3E4 in the 68000 disassembly load an enemy palette from F426 ->
       72338 and a 0x180-byte graphics chunk selected through 7246C. The
       generated table reproduces those combinations. Only variants referenced
       by the current level are copied to VDC1. */
    if (!g_in_boss && g_level) {
        if(level_has_enemy_kind(ENEMY_KIND_MONKEY))upload_world_sprite_words(ENEMY_BANANA_BASE_WORD,g_monkey_banana_words,64);
        /* One copy of the flier's bombing pose and its bomb serves every
           variant of the family: their DMA buffers hold the same images. */
        if(level_has_enemy_kind(ENEMY_KIND_CHIP)){
            upload_world_sprite_words(CHIP_BOMB_BASE_WORD,g_chip_bomb_words,64);
            upload_world_sprite_words(CHIP_ATTACK_BASE_WORD,g_chip_attack_words,256);
        }
        walk_top=ENEMY_BASE_WORD; hit_top=ENEMY_HIT_BASE_WORD; alt_top=ENEMY_ALT_BASE_WORD;
        shot_top=ENEMY_SHOT_BASE_WORD;
        for(i=0;i<g_level->variant_count;++i) {
            int gi=g_level->variant_ids[i]; const EnemyVariantData *v=&g_enemy_variants[gi]; int line=-1;
            int fw=enemy_frame_words(v);
            for(j=0;j<uniq;++j) if(palettes_equal(seen[j],v->palette)){ line=4+j; break; }
            if(line<0){ seen[uniq]=v->palette; line=4+uniq; for(j=0;j<16;++j)pal_set((u16)(line*16+j),v->palette[j]); ++uniq; }
            g_enemy_palette_line[i]=(unsigned char)line;
            g_enemy_vram[i]=(u16)walk_top;
            upload_world_sprite_words((u16)walk_top,v->words,(unsigned)(fw*2));
            walk_top+=fw*2;
            g_enemy_hit_vram[i]=(u16)hit_top;
            if(v->has_hit_frame){
                upload_world_sprite_words((u16)hit_top,v->words+fw*2,(unsigned)fw);
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
                upload_world_sprite_words((u16)alt_top,src,(unsigned)fw);
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
                    upload_world_sprite_words((u16)shot_top,v->shot_words,(unsigned)(v->shot_cols*64));
                    shot_top+=v->shot_cols*64;
                }
            }
        }
        /* The snake strike is a genuine 64x40 metasprite and cannot share the
           legacy 3x3-cell enemy frame. Its authored variant set leaves room
           at $9800 for the three 4x4 poses. */
        if(level_has_enemy_kind(ENEMY_KIND_STRIKE_SNAKE)){
            upload_world_sprite_words(STRIKE_SNAKE_BASE_WORD,g_level8_snake_words,3072);
        }
        if(level_has_enemy_kind(ENEMY_KIND_MONKEY)){
            upload_world_sprite_words(MONKEY_SPECIAL_BASE_WORD,g_monkey_special_words,1152);
        }
        if(level_has_enemy_kind(ENEMY_KIND_CRATE_MIMIC)){
            upload_world_sprite_words(CRATE_MIMIC_WALK_BASE_WORD,g_crate_walk_words,1152);
        }
    }
}

static void overlay_set_palette(int level);

static void set_game_palette_fade(int level)
{
    int i,j,uniq=0; const unsigned short *seen[12];
    const BossVisualData *bv=(g_in_boss&&g_boss_class>0)?&g_later_boss_visuals[g_boss_class-1]:0;
    const BossRoomData *br=(g_in_boss&&g_boss_class==0)?&g_bowser_rooms[(g_current_level==5)?1:0]:0;
    const unsigned short *pal = g_in_boss ? (bv?bv->bg_palette:br->palette) : g_level->palette;
    if(level_softens_glare())
        for(i=0;i<256;++i)pal_set((u16)i,fade_yuv(soften_yuv(pal[i]),level));
    else
        for(i=0;i<256;++i)pal_set((u16)i,fade_yuv(pal[i],level));
    pal_set_line(MARIO_BOSS_SPR_PAL,g_player_palette,level);
    if(multiplayer_active())pal_set_line(LUIGI_SPR_PAL,g_luigi_reuse_palette,level);
    pal_set_line(CONTINUE_PLAYER_SPR_PAL,g_continue_player_palette,level);
    if(g_in_boss){
        if(bv){
            for(j=0;j<bv->palette_count;++j){
                int line=j?BOSS_SPR_PAL_AUX:BOSS_SPR_PAL;
                pal_set_line(line,bv->sprite_palettes+j*16,level);
            }
        }else pal_set_line(BOSS_SPR_PAL,g_bowser_palette,level);
    }
    if(g_in_boss&&g_boss_class>0)pal_set_line(BOSS_AUX_SPR_PAL,g_final_aux_palette,level);
    if(g_in_boss&&g_boss_class>0){
        const unsigned short *item_pal=g_boss_pot_palette;
        if(g_boss_class==3)item_pal=g_boss_bomb_palette;
        else if(g_boss_class==2)item_pal=g_boss_crate4_palette;
        else if(g_boss_class==4)item_pal=g_boss_bell_palette;
        pal_set_line(BOSS_ITEM_SPR_PAL,item_pal,level);
    }
    if(g_in_boss&&g_boss_class==1)pal_set_line(BOSS_WATER_SPR_PAL,g_boss_water_palette,level);
    if(g_in_boss&&g_boss_class==3)pal_set_line(BOSS13_PLATFORM_SPR_PAL,g_boss13_platform_palette,level);
    else if(!g_in_boss&&g_level){
        for(i=0;i<g_level->variant_count;++i){
            const EnemyVariantData *v=&g_enemy_variants[g_level->variant_ids[i]]; int line=-1;
            for(j=0;j<uniq;++j)if(palettes_equal(seen[j],v->palette)){line=4+j;break;}
            if(line<0){seen[uniq]=v->palette;line=4+uniq;++uniq;}
            g_enemy_palette_line[i]=(unsigned char)line;
            pal_set_line(line,v->palette,level);
        }
    }
    /* Boss 3's stage object is the supplied pot.  Normal stages, including
       World 3 and all later worlds, keep the ordinary crate palette. */
    if(g_in_boss&&g_boss_class==1)
        pal_set_line(BOSS_ITEM_SPR_PAL,g_boss_pot_palette,level);
    /* Level 8's carriers and wedges are all on source palette line 2. */
    if(!g_in_boss&&g_current_level==7)
        pal_set_line(LEVEL8_OBJ_SPR_PAL,g_level8_object_palette,level);
    else if(stage_objects()->palette)
        pal_set_line(LEVEL8_OBJ_SPR_PAL,stage_objects()->palette,level);
    /* Boss bodies own palette line 14, which the continue room normally uses
       for the separately indexed crouch art. Keep an independent boss-room
       copy after every boss/background palette update. */
    if(g_in_boss)pal_set_line(BOSS_CROUCH_SPR_PAL,g_continue_player_palette,level);
    overlay_set_palette(level);
    g_game_fade_level=level;
}

/* Scenery that is drawn once and animated in CRAM.  The source's palette task
   chain has exactly two of these, and both work the same way: a counter is
   stepped every call, and when it reaches the task's period three consecutive
   CRAM words are rewritten from a nine-word table whose three phases are the
   level's own three colours rotated one place.  So each is a rotation of three
   palette entries and needs no colour the level does not already own.

   $ABBC gives level slots 17, 18 and 19 the conveyor gears: period seven,
   CRAM $6E (palette line 3's colours 7..9, the machine's three purples) from
   the table at $AC44.

   $AAA4 gives slots 11, 12 and 13 -- the waterfall stages -- two rotations at
   once: period seven at CRAM $5A (line 2's colours 13..15) from $AC20, and
   period $11 at CRAM $78 (line 3's colours 12..14) from $AC32.  Both are the
   falling water, which is why it stood perfectly still without them.

   Every source task runs at half the frame rate, so each period doubles. */
typedef struct { unsigned char level_lo,level_hi,first,period; } PaletteCycle;
static const PaletteCycle g_palette_cycles[3]={
    {16,18,55,14},   /* $ABBC gears,     CRAM $6E, period 7  */
    {10,12,45,14},   /* $AAA4 waterfall, CRAM $5A, period 7  */
    {10,12,60,34},   /* $AAA4 waterfall, CRAM $78, period $11 */
};
#define PALETTE_CYCLE_COUNT 3
static unsigned char g_palette_cycle_shift[PALETTE_CYCLE_COUNT];

static void update_gear_palette(void)
{
    int c,i;
    if (g_in_boss||!g_level||g_game_fade_level!=FADE_FULL) return;
    for (c=0;c<PALETTE_CYCLE_COUNT;++c) {
        const PaletteCycle *p=&g_palette_cycles[c];
        if (g_current_level<p->level_lo||g_current_level>p->level_hi) continue;
        if ((g_state_frame%p->period)!=0) continue;
        g_palette_cycle_shift[c]=(unsigned char)((g_palette_cycle_shift[c]+2)%3);
        for (i=0;i<3;++i)
            pal_set((u16)(p->first+i),
                g_level->palette[p->first+(i+g_palette_cycle_shift[c])%3]);
    }
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

/* --- Screen grid -------------------------------------------------------
   $14370 walks the route one record at a time; the record's mode selects
   where the incoming screen is pasted relative to the outgoing one (table
   $144B6: mode 0 right, 1 left, 2 above, 3 below).  Reconstruct that walk
   once at level load so every world coordinate can be resolved to a route
   index, and so the camera knows how tall the stage really is.

   The first route entry is always the stage entrance, so after normalising
   the upward steps it sits on the bottom row of the grid. */
/* A stacked storey is pasted one whole collision quadrant away, not one
   picture: the quadrants of the 64x64 buffer are 32 cells tall, $14370 only
   loads the next record once the scroll accumulator has completed a multiple
   of 256, and $10A8E takes the vertical trigger phase modulo 256 as well.
   The V28 picture shows the top 224 lines of each 256-pixel storey; the 32
   lines below carry the segment's four unused map rows, which is where the
   climb between two storeys passes. */
#define LEVEL_SCREEN_H     256
#define LEVEL_SCREEN_ROWS  32
#define LEVEL_PICTURE_ROWS 28           /* MD V28 picture inside the storey */
#define SEGMENT_BAT_ROWS   34           /* 30 picture rows + the 4 band rows */
#define SEGMENT_BAND_ROW   30           /* source map row 28 lands here */
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

/* The Mega Drive holds only the live room and the one the route pastes next
   to it, so the viewport never leaves that pair.  $17238 lets Mario walk right
   past screen x $F5 only when FF0050 == 0 (the next room is pasted to the
   right); $170F8 mirrors it for mode 1, and $14804's whole horizontal scroll
   handler is skipped when FF0050 >= 2, where the room hands over to the
   vertical scroll at $17818 instead.  FF0050 is the mode of the record that
   was loaded last, i.e. the mode of the *next* room, so route_modes[si+1]
   governs while Mario stands in room si.

   Reproduce that window here: the camera may span Mario's own screen plus a
   route neighbour, and only along the axis that neighbour is pasted on.  A
   room whose exit is upwards therefore has no horizontal travel at all, and a
   room in the middle of a horizontal run has no vertical travel. */
static void room_window(int si,int *clo,int *chi,int *rlo,int *rhi)
{
    int k,sc,gr;
    *clo=*chi=*rlo=*rhi=0;
    if(si<0||si>=g_screen_count)return;
    sc=g_screen_col[si]; gr=g_screen_row[si];
    *clo=*chi=sc; *rlo=*rhi=gr;
    for(k=si-1;k<=si+1;k+=2){
        int nc,nr;
        if(k<0||k>=g_screen_count)continue;
        nc=g_screen_col[k]; nr=g_screen_row[k];
        if(nr==gr){ if(nc<*clo)*clo=nc; if(nc>*chi)*chi=nc; }
        else if(nc==sc){ if(nr<*rlo)*rlo=nr; if(nr>*rhi)*rhi=nr; }
    }
}

/* $14370 walks the route one record at a time, so the live room can only ever
   become the neighbour the walk has just pasted.  Deriving the room from
   Mario's coordinates instead let a jump that crossed a storey boundary hand
   the camera a screen the route had not reached -- jumping on the first
   screen of a stacked level threw the view a whole storey up. */
static void update_current_room(int px,int py)
{
    int lr,gr=split_world_row((py+16)>>3,&lr),sc=(px+8)>>8,k;
    if(g_screen_count<=0)return;
    if(g_room<0||g_room>=g_screen_count)g_room=0;
    if(g_screen_col[g_room]==sc&&g_screen_row[g_room]==gr)return;
    for(k=g_room-1;k<=g_room+1;k+=2)
        if(k>=0&&k<g_screen_count&&g_screen_col[k]==sc&&g_screen_row[k]==gr){g_room=k;return;}
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

/* Row of a segment's bank to read for a storey-local tile row.  The bank holds
   the 28 picture rows, the two rows the PC-FX's taller raster needs under them
   -- the bottom storey's HUD band -- and then the source's own map rows 28..31,
   which are the wall behind the 32-line gap between two stacked storeys. */
static int segment_bat_row(int lr,int gr)
{
    if(lr<LEVEL_PICTURE_ROWS)return lr;
    if(g_level_rows>1&&gr!=g_level_rows-1)return SEGMENT_BAND_ROW+(lr-LEVEL_PICTURE_ROWS);
    return (lr<30)?lr:-1;
}

static u16 level_fg_bat_entry(int world_col,int tile_row)
{
    int seg,lx,si,lr,row;
    if(!g_level||!g_stage_set||world_col<0||world_col>=g_level_width_tiles||tile_row<0)
        return FG_TILE_BASE_INDEX;
    {
        int gr=split_world_row(tile_row,&lr);
        si=screen_index_at(world_col,gr);
        row=segment_bat_row(lr,gr);
    }
    if(si<0||lr<0||row<0)return FG_TILE_BASE_INDEX;
    lx=world_col&31;
    seg=g_level->route[si]; if(seg==255||seg>=g_stage_set->segment_count)return FG_TILE_BASE_INDEX;
    return relocate_bat(g_stage_set->segment_bat[(seg*SEGMENT_BAT_ROWS+row)*32+lx],FG_TILE_BASE_INDEX);
}

static void stream_level_column(int world_col, int display_enabled)
{
    int row,chip=level_stage_chip(),map_col=world_col&(FG_BAT_W-1);
    /* Gameplay keeps a complete auxiliary SATB on VDC0.  Preserve its sprite
       enable while a stage-plane column temporarily changes the increment
       mode; both the table and every pattern it can reference are resident. */
    u16 planes=display_enabled
        ? (u16)(VDC_CR_BB|VDC_CR_SB) : 0;
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

/* Vertical BAT rows are selected during simulation, after the leading VBlank
   interval may already have expired.  Queue their world coordinates and write
   them at the next frame wrap, before the scroll that makes them reachable.
   Camera motion is normally at most one tile per frame, but retain a complete
   window's worth so a future larger step cannot silently drop an entering row. */
static void queue_level_row(int world_row)
{
    int i;
    if(world_row<0)return;
    for(i=0;i<g_pending_level_row_count;++i)
        if(g_pending_level_rows[i]==world_row)return;
    if(g_pending_level_row_count<FG_BAT_H)
        g_pending_level_rows[g_pending_level_row_count++]=world_row;
}

static void flush_level_rows(void)
{
    int i;
    for(i=0;i<g_pending_level_row_count;++i)
        stream_level_row(g_pending_level_rows[i]);
    g_pending_level_row_count=0;
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
    vdc_set_vram_write(chip,FG_TILE_BASE_WORD);
    for(x=0;x<g_stage_set->tile_count*TILE_WORDS;++x)vdc_vram_write(chip,g_stage_set->tile_words[x]);
    for(y=0;y<30;++y){
        vdc_set_vram_write(chip,(u16)(y*BG_BAT_W));
        /* The background plane is the Mega Drive's full 64-cell width; it
           wraps every 512 pixels, not every 256.  All 30 rows are real art:
           rows 28 and 29 sit below the Mega Drive's 224-line picture, and the
           world 3 sheets paint them (see tools/level_sheet.py) rather than
           leaving the row-27 texture repeating behind the HUD. */
        for(x=0;x<64;++x)
            vdc_vram_write(chip,relocate_bat(g_level->bg_bat[y*BG_BAT_W+x],FG_TILE_BASE_INDEX));
    }
}

/* ---- World 3's cellophane layer ---------------------------------------
   The Mega Drive fakes translucency in this stage family by dithering the
   foreground plane against the stage plane behind it: the canopy shadow and
   level 8's water are 1-pixel checkerboards of a single colour.  The PC-FX
   blends for real, so tools/level_overlay.py lifts those regions out of the
   VDC plane and stores them as a solid 4-colour image, which KING BG0 draws
   here.  The HuC6261 mixes BG0 with the layer behind it using coefficient 1,
   set to 4/8 on both sides for an even 50% blend.  On the Mega Drive the
   dithered plane is in front of the sprites, so BG0 outranks the VDC sprites
   too and the band tints the player and the enemies under it -- except across
   the HUD's own scanlines, where a raster split drops BG0 back between the two
   VDC layers so the score row reads at full contrast.

   The overlay shares the backdrop plane's geometry: 512 pixels wide, wrapping
   with it, and is stored as 8x8 cells that the whole stage family pools.
   Palette bank 15 is free during gameplay (only the boss rooms use it), so
   BG0's sixteen colours live there -- world 3-1's band alone needs six, which
   is why the layer is 4bpp; 4bpp is also the smallest KING mode that carries
   them, at half the KRAM of the 256-colour one. */
#define OVERLAY_PAL_BASE 240
#define OVERLAY_KRAM_LINES 256

/* The band's own VCE priority, with the VDC background on 1 and the VDC
   sprites on 3: 4 puts it in front of everything the VDCs draw, which is
   where the Mega Drive's dithered plane sits.  BG1 carries the HUD above it;
   the two remaining KING planes are not displayed and stay zero. */
#define OVERLAY_PRIO_OVER_SPRITES  4
#define HUD_KING_PRIO              5

static int g_overlay_active;

/* ---- The HUD on KING BG1 ----------------------------------------------
   The HuC6261 blends the band with the picture behind it wherever the band
   wins, so everything it covers is tinted -- and the HUD, sitting in the
   bottom sixteen lines of a stage whose band reaches the floor, was tinted
   with it.  A raster split used to drop the band behind the sprites for
   those lines, but the sprites down there are not only the HUD: the carrier
   logs, the player and anything else standing at the foot of the picture
   lost their tint for the same sixteen lines, which is the seam that shows
   up as the band "switching off" around the score row.

   So the HUD leaves the VDC sprites altogether and becomes KING BG1.  KING
   resolves its own four planes into a single pixel before the VCE sees a
   KING layer at all, so an opaque HUD dot simply replaces the band's dot and
   arrives at the VCE tagged BG1 -- a layer with no cellophane coefficient --
   while every other dot is still the band, in front of the VDC sprites for
   every line of the frame.  Nothing needs a raster split any more.

   BG1 is a 256x256 BAT layer: a BAT cell picks its own sixteen-colour
   palette line, so both players' HUDs keep the palettes their sprites used.
   Only the two cell rows the HUD occupies are ever filled, and BAT entry 0
   selects a blank tile, so the rest of the layer is transparent. */
#define HUD_KING_COLS       32
#define HUD_KING_ROWS       2          /* the HUD is sixteen lines tall */
#define HUD_KING_CELLS      (HUD_KING_COLS*HUD_KING_ROWS)
#define HUD_KING_CELL_WORDS 16         /* 8x8 at 4bpp, two words per row */
#define HUD_KING_BAT_ROW    26         /* layer line 208 */
#define HUD_KING_TOP        209        /* first screen line of the HUD */
#define HUD_KING_LUIGI_COL  15         /* x 120, where player two's HUD starts */
/* KRAM granules of 1024 words.  BG0's 512x256 4bpp band is granules 0..31,
   so the HUD's 32x32 BAT and its 65 tiles follow it. */
#define HUD_KING_BAT_ADDR   32
#define HUD_KING_CG_ADDR    33

static u16 g_hud_cg[HUD_KING_CELLS*HUD_KING_CELL_WORDS];
static u32 g_hud_king_sig;

/* Both HUD rows sit sixteen lines higher in the boss rooms and in level 3. */
static int hud_y_offset(void)
{
    return (g_in_boss||g_current_level==2)?-16:0;
}

/* Publish the layer registers and the parts of KRAM that never change: the
   blank tile every unused BAT entry points at, and a cleared BAT. */
static void hud_king_setup(void)
{
    int i;
    king_set_kram_write(HUD_KING_CG_ADDR*1024,1);
    for(i=0;i<HUD_KING_CELL_WORDS;++i)king_kram_write(0);
    king_set_kram_write(HUD_KING_BAT_ADDR*1024,1);
    for(i=0;i<HUD_KING_COLS*32;++i)king_kram_write(0);
    king_set_bg_size(KING_BG1,KING_BGSIZE_256,KING_BGSIZE_256,
                     KING_BGSIZE_256,KING_BGSIZE_256);
    king_set_bat_cg_addr(KING_BG1,HUD_KING_BAT_ADDR,HUD_KING_CG_ADDR);
    /* BG1's scroll registers are ten bits, so the one-line lift that puts
       layer line 208 on screen line 209 is written as its 10-bit residue. */
    king_set_scroll(KING_BG1,0,
        (s16)((HUD_KING_BAT_ROW*8-HUD_KING_TOP-hud_y_offset())&0x03FF));
    g_hud_king_sig=0;
}

static const LevelOverlayData *level_overlay(void)
{
    const LevelOverlayData *ov;
    if(g_in_boss||!g_level||g_current_level<0||g_current_level>=19)return 0;
    ov=&g_level_overlays[g_current_level];
    return ov->rows?ov:0;
}

static void overlay_set_palette(int level)
{
    const LevelOverlayData *ov=level_overlay();
    int i;
    if(!ov)return;
    /* The band is mixed with the softened scenery underneath it, so it takes
       the same shoulder or its tint would pull away from the plane it belongs
       to as the level fades in. */
    for(i=0;i<LEVEL_OVERLAY_COLORS;++i)
        pal_set((u16)(OVERLAY_PAL_BASE+i),
                fade_yuv(level_softens_glare()?soften_yuv(ov->palette[i])
                                              :ov->palette[i],level));
}

static void overlay_disable(void)
{
    if(!g_overlay_active)return;
    g_overlay_active=0;
    king_disable_microprogram();
    king_set_bg_prio(KING_BGPRIO_HIDE,KING_BGPRIO_HIDE,KING_BGPRIO_HIDE,
                     KING_BGPRIO_HIDE,0);
    tetsu_set_cellophane_control(0,0,TETSU_NO_COEFF,TETSU_NO_COEFF,TETSU_NO_COEFF,
                                 TETSU_NO_COEFF,TETSU_NO_COEFF,TETSU_NO_COEFF,
                                 TETSU_NO_COEFF);
    tetsu_set_priorities(1,2,0,0,0,0,0);
    set_display_layers(0,0);
}

static void overlay_upload(void)
{
    const LevelOverlayData *ov=level_overlay();
    signed char rowmap[LEVEL_OVERLAY_ROWS];
    u16 mprog[16];
    int y,x,i;

    if(!ov){overlay_disable();return;}
    for(i=0;i<LEVEL_OVERLAY_ROWS;++i)rowmap[i]=-1;
    for(i=0;i<ov->row_count;++i)rowmap[ov->rows[i]]=(signed char)i;

    /* The ADPCM bank owns KRAM page 1; the overlay image is the only user of
       page 0, so it starts at word zero and is written as one linear run. */
    king_set_kram_pages(0,0,0,1);
    king_set_kram_write(0,1);
    for(y=0;y<OVERLAY_KRAM_LINES;++y){
        const unsigned char *cells=0;
        if(y<LEVEL_OVERLAY_ROWS*8){
            int band=rowmap[y>>3];
            if(band>=0)cells=ov->cells+band*LEVEL_OVERLAY_CELLS;
        }
        if(!cells){
            for(x=0;x<LEVEL_OVERLAY_CELLS*2;++x)king_kram_write(0);
            continue;
        }
        for(x=0;x<LEVEL_OVERLAY_CELLS;++x){
            int tile=cells[x];
            const unsigned short *w=tile?ov->tiles+(tile-1)*LEVEL_OVERLAY_CELL_WORDS+(y&7)*2:0;
            king_kram_write(w?w[0]:0);
            king_kram_write(w?w[1]:0);
        }
    }

    king_set_bg_mode(KING_BGMODE_16_PAL,
                     (king_bgmode)(KING_BGMODE_16_PAL|KING_BGMODE_BAT),0,0);
    king_set_bg_size(KING_BG0,KING_BGSIZE_256,KING_BGSIZE_512,
                     KING_BGSIZE_256,KING_BGSIZE_512);
    king_set_bat_cg_addr(KING_BG0,0,0);
    king_set_bat_cg_addr(KING_BG0SUB,0,0);
    king_set_scroll(KING_BG0,0,0);
    hud_king_setup();
    /* KING resolves its own planes in this order, so the HUD lands on top of
       the band before either reaches the HuC6261. */
    king_set_bg_prio(KING_BGPRIO_0,KING_BGPRIO_1,KING_BGPRIO_HIDE,
                     KING_BGPRIO_HIDE,0);
    /* Two CG fetches cover eight 16-colour pixels -- the doc (3.6.7.4) fixes
       the count at the number of words the eight dots occupy and requires the
       offsets to ascend within the eight-cycle bank.  BG1 is a BAT layer, so
       it also needs the cell's BAT word fetched before the two CG words that
       word addresses.  Five of the bank's eight cycles; the rest idle. */
    mprog[0]=KING_CODE_BG0_CG_0;
    mprog[1]=KING_CODE_BG0_CG_1;
    mprog[2]=KING_CODE_BG1_BAT_0;
    mprog[3]=KING_CODE_BG1_BATCG_0;
    mprog[4]=KING_CODE_BG1_BATCG_1;
    for(i=5;i<16;++i)mprog[i]=KING_CODE_NOP;
    king_disable_microprogram();
    king_write_microprogram(mprog,0,16);
    king_enable_microprogram();

    /* BG1's palette base is zero, so a BAT cell's four-bit bank number is
       the same sprite palette line the HUD used as sprites. */
    tetsu_set_king_palette(OVERLAY_PAL_BASE,0,0,0);
    /* KING BG0 is published by the HuC6261 picture mode, not by either VDC's
       display bit, so blanking both VDC outputs for a level load does not
       blank the band.  Bring it up black and let the caller's fade raise it,
       or the freshly uploaded band flashes at full colour over the black
       screen for the frames the rest of the load takes. */
    overlay_set_palette(0);
    tetsu_set_priorities(1,3,OVERLAY_PRIO_OVER_SPRITES,HUD_KING_PRIO,0,0,0);
    tetsu_set_cellophane_coeffs(1,4,4,4,4,4,4);
    /* Neither front nor back cellophane: those replace the hindmost layer with
       a constant colour, while plain per-layer cellophane blends BG0 with the
       VDC picture underneath, which is what the dither stood in for. */
    tetsu_set_cellophane_control(0,0,TETSU_NO_COEFF,TETSU_NO_COEFF,TETSU_COEFF_1,
                                 TETSU_NO_COEFF,TETSU_NO_COEFF,TETSU_NO_COEFF,
                                 TETSU_NO_COEFF);
    set_display_layers(1,1);
    g_overlay_active=1;
}

static void init_level_ring(void)
{
    int col,i,chip;
    g_pending_level_row_count=0;
    g_level=&g_game_levels[g_current_level]; g_stage_set=&g_stage_sets[g_level->set_index];
    build_level_layout(); g_level_width_tiles=g_level_cols*32; g_room=0;
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
    overlay_upload();
    /* Publish every palette black: start_gameplay() re-publishes at the level
       the caller asked for once the whole load has finished, and the KING
       overlay band is visible during the load even with both VDCs blanked. */
    upload_game_sprites(); set_game_palette_fade(0); pal_publish();
}

static unsigned char collision_at(int x,int y)
{
    int tx,ty,screen,seg,lx;
    if(g_in_boss){
        int ci=(g_boss_class<0||g_boss_class>5)?0:g_boss_class;
        if(x<0||x>=256)return 1;if(y<0)return 0;
        /* Slot 13's grid is the two storeys and nothing else: $718D8 marks
           rows 8 and 20 across x 64..191 and leaves every other cell clear,
           so the shafts the carriers run in are open all the way down and a
           miss is fatal.  The other arenas have no floor in their own grid
           either and lean on the room's bottom edge for one, so only this
           room opens out. */
        if(y>=256)return (g_boss_class==3)?0:1;
        /* Slot 13's two gold carriers are live objects $7B5C creates and
           walks, not room geometry: the ROM's own grid for the room is solid
           only across x 64..191 on rows 8 and 20, and the shafts the carriers
           run in are open air.  Nailing the carriers' captured positions into
           the grid walled both shafts off permanently, so ride the objects. */
        /* Exact 32x32 collision grids selected by ROM table $7038C. */
        return g_boss_collision[ci*1024+(y>>3)*32+(x>>3)];
    }
    if(x<0)return 1;tx=x>>3;ty=y>>3;
    if(!g_level||tx<0||tx>=g_level_width_tiles)return 1;if(ty<0)return 0;
    {int lr;screen=screen_index_at(tx,split_world_row(ty,&lr));ty=lr;}
    if(screen<0||ty<0||ty>=32)return 0;
    lx=tx&31;seg=g_level->route[screen];if(seg==255||seg>=g_stage_set->segment_count)return 0;
    /* The band under a storey's picture is drawn with the room's masonry but
       the source grid leaves it clear, and that is deliberate: rows 28..31 are
       the 32-line gap the climb passes through, so the whole grid stays the
       ROM's.  Giving the drawn masonry a collision of its own walls the seam
       off across the full width and traps Mario between two storeys. */
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

/* 8 is the warehouse conveyor the three "BARNETT'S" stages run their purple
 * gears on.  The source terrain walker at $17E2E stores the cell under an
 * actor and $1774E only keeps a fall going while that cell is 0, 4 or 6, so a
 * conveyor stops one exactly like a floor does; $18054 then drags whatever
 * came to rest on it one pixel left per tick.  Only the stage grids are read
 * that way: the captured boss grids carry stray bytes outside 1/2/4 that were
 * never terrain. */
static int conveyor(unsigned char c) { return !g_in_boss && c == 8; }

static int solid_v(int x, int y)
{
    unsigned char c = collision_at(x,y);
    return c == 1 || c == 2 || conveyor(c);
}

/* Level surfaces are one-way.  The extracted grids mark a floor with a single
   row of 1 (or 2) and mark the walls that hold it up separately with 4, and
   the source terrain walker at $17E2E only ever asks which surface an actor
   stands on - it has no upward test at all.  So the brown ledges are entered
   from below on the Mega Drive, and jumping through them has to work here too.
   Boss rooms keep their ceilings: their 32x32 grids draw the arena as a closed
   box, and the room has no other bound to stop Mario leaving through the top.

   A terrain mass two rows deep is a different thing from a ledge, though: the
   grids mark the top of a stack that must be entered from above with a second
   solid row under the first, and nothing the player is meant to pass through
   is ever that thick.  Taking the row below into account keeps the one-way
   ledges open while Mario's head still stops under a real overhang. */
static int solid_ceiling(int x, int y)
{
    if (g_in_boss) return collision_at(x,y) == 1;
    return collision_at(x,y) == 1 && collision_at(x,y-8) == 1;
}

static int player_body_x(void) { return (int)(g_player_x >> FIX_SHIFT) + PLAYER_BODY_XOFF; }
static int player_body_y(void) { return (int)(g_player_y >> FIX_SHIFT) + PLAYER_BODY_YOFF; }

/* True while Mario's feet rest on a conveyor cell, i.e. on the row the landing
   solver just put him on. */
static int player_on_conveyor(void)
{
    int bx=player_body_x(), probe=player_body_y()+PLAYER_BODY_H;
    return conveyor(collision_at(bx+3,probe))||
           conveyor(collision_at(bx+PLAYER_BODY_W-4,probe));
}

/* The first two arenas retain the six-pixel raster lead from their captured
   Mega Drive room plane.  Bowser's extracted poses include that lead, whereas
   Mario and the generic crate metasprites do not. */
/* Every boss arena is a capture of the Mega Drive screen, and a capture does
   not necessarily sit where the room's own collision grid says it does.  The
   Bowser rooms are six lines out; slot 13's is seven - its ledges are drawn
   at y=57 and y=153 where the ROM's 32x32 grid for the room (rows 8 and 20)
   puts their surfaces at 64 and 160.  Collision, the boss script and the
   carriers all stay in the ROM's room space; only the drawing of the things
   that live in it is nudged onto the picture, so they keep their alignment
   with each other as well as with the arena. */
static int bowser_room_object_y_offset(void)
{
    if (!g_in_boss) return 0;
    if (g_boss_class == 0) return -6;
    if (g_boss_class == 3) return BOSS13_ROOM_DY;
    return 0;
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
               the carriers: $B086 keeps no standing surface, and neither does
               the fireball $AECE throws up through the floor beside it. */
            if (p->kind==PLATFORM_KIND_BOULDER ||
                p->kind==PLATFORM_KIND_FIREBALL || p->hidden) continue;
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
            int hit=(left==1||right==1||conveyor(left)||conveyor(right))||
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
 * Releasing I during the rising half mirrors into the falling half,
 * reproducing the original short-jump behaviour.
 *
 * The rising half is not the Mega Drive's.  Its 75-pixel arc stops short of
 * ledges the port's terrain lands an 8px row above the original, and it reads
 * as too low here besides, so the twenty-one rising steps have been steepened
 * to carry Mario 100 pixels instead.  The three apex zeroes and the whole
 * descent are untouched, which is what keeps the mirrored short jump and the
 * landing speed behaving as they did. */
static const signed char g_md_jump_dy[45]={
    -7,-7,-6,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
    -5,-5,-4,-4,-3,-2,-2,0,0,0,2,2,2,2,2,2,2,
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

/* Exact state-10 drift table at $BEEE, thirty-three signed (dx,dy) pairs.
   Handler $BE6C is the graveyard ghost: it is not a ground enemy at all.  It
   only updates on every eighth frame ($BE76 masks the global counter with 7),
   walks this arc one pair at a time, and when the arc runs out $BEB4 nudges it
   eight pixels toward Mario on each axis and starts over.  It never probes
   terrain, so it drifts through the crypt walls at half a pixel a frame. */
#define GHOST_PATH_STEPS 33
static const signed char g_ghost_path[GHOST_PATH_STEPS*2]={
    -1, 0,-4,-3,-4,-2,-4,-1,-4,-2,-4,-1,-4, 0,-4,-1,-4, 0,-4, 0,-4, 1,
    -4, 0,-4, 1,-4, 2,-4, 1,-4, 2,-4, 3,-4, 2,-4, 4,-4, 4,-3, 4,-2, 4,
    -1, 4,-1, 4,-1, 4, 0, 4, 1, 4, 1, 4, 1, 4, 2, 4, 3, 4, 4, 4, 4, 4
};

/* True once Mario has walked off the right of the route's final screen. */
static int level_exit_reached(void)
{
    int px=(int)(g_player_x>>FIX_SHIFT);
    if(g_level_rows<=1)return px>=g_level_width_tiles*8-48;
    if(g_screen_count<=0||g_room!=g_screen_count-1)return 0;
    return px>=(int)g_screen_col[g_room]*256+256-48;
}

/* World 5 is the nuclear-dump trio, slots 11..13 -- the levels music.c gives
   CDDA_TRACK_NUCLEAR_DUMP. */
#define WORLD5_FIRST_LEVEL 10
#define WORLD5_LAST_LEVEL  12
static int level_is_world5(void)
{
    return !g_in_boss && g_current_level>=WORLD5_FIRST_LEVEL &&
           g_current_level<=WORLD5_LAST_LEVEL;
}

/* Whoever is riding one of the $F9C6 loop carriers, on a world that asks the
   camera to sit still for it. */
static int riding_loop_carrier(void)
{
    return level_is_world5() && g_on_moving_platform>=0 &&
           g_moving_platforms[g_on_moving_platform].kind==PLATFORM_KIND_PATH;
}

static void update_camera(void)
{
    if(g_in_boss){
        g_camera_x=0;g_camera_target_x=0;g_camera_tile=0;
        g_camera_y=0;g_camera_target_y=0;g_camera_row=0;
        /* $9596 sets the jolt flag when the elephant comes back down; the
           source shakes both planes for a few frames after the landing. */
        {
            /* Shake sideways rather than vertically: both BATs are 32 columns
               for a 256-pixel room, so an X offset wraps exactly, while a Y
               offset runs off the end of the populated rows and shows VDC
               scratch. */
            int jolt=(g_screen_shake>0)?((g_screen_shake&1)?2:-2):0;
            int wall=(g_boss_class==5)?(int)((g_boss_timer>>2)%224):0;
            vdc_set_scroll(FRONT_CHIP,(u16)((jolt+256)&255),0);
            vdc_set_scroll(BACK_CHIP,(u16)((jolt+256)&255),(u16)wall);
        }
        return;
    }
    int px=(int)(g_player_x>>FIX_SHIFT);
    int py=(int)(g_player_y>>FIX_SHIFT);
    int max_camera=g_level_width_tiles*8-SCREEN_W;
    int max_camera_y=(g_level_rows-1)*LEVEL_SCREEN_H;
    int min_camera=0;
    int min_camera_y=0;
    int target=g_camera_x;
    int target_y=g_camera_y;
    int new_tile,new_row;

    /* Confine the viewport to the room Mario is standing in and whichever
       neighbour the route pastes beside it. */
    if(g_level_rows>1){
        int clo,chi,rlo,rhi;
        update_current_room(px,py);
        room_window(g_room,&clo,&chi,&rlo,&rhi);
        min_camera=clo*256;
        if((chi+1)*256-SCREEN_W<max_camera)max_camera=(chi+1)*256-SCREEN_W;
        min_camera_y=rlo*LEVEL_SCREEN_H;
        max_camera_y=rhi*LEVEL_SCREEN_H;
        if(max_camera<min_camera)max_camera=min_camera;
    }

    /* The MD stage camera does not re-center on Mario every frame. Keep Mario
       inside a forward dead-zone and never pull the map backwards because an
       action temporarily stops him. This also prevents world-object snapping
       on throw/reversal frames. */
    if (!g_camera_hold && px-g_camera_x>112)
        target=px-112;
    if (target<min_camera) target=min_camera;
    if (target>max_camera) target=max_camera;

    /* Prepare wrapped BAT columns before publishing the target. These cells
       are outside the committed viewport, so the VRAM burst is not visible
       and does not consume the leading blanking interval used by KING. */
    /* Keep one complete tile behind the logical viewport before recycling its
       BAT column.  The VDC fetch window retains a few pixels to the left of
       BXR, so replacing a column at the exact 8-pixel boundary exposes a
       sliver of the far-ahead column as scrolling garbage on the left edge. */
    /* Vertical follow first: a new BAT row has to be pasted with the window
       the columns are about to be streamed against.

       The source has no vertical dead-zone.  $14804's horizontal handler is
       skipped entirely once FF0050 >= 2 and the room hands over to $17818,
       which scrolls the picture by exactly the jump step until the walk loads
       the next record; $14370 only performs that load when the accumulator has
       completed a whole screen, so the view comes to rest on a storey and
       never sits astride two of them.  Reproduce that: the camera belongs to
       the live room, and it travels -- one screen, at a rate that outruns the
       jump's seven-pixel first step so Mario cannot climb out of the top --
       only while a hand-over is in progress. */
    if(g_level_rows>1){
        int home=(int)g_screen_row[g_room]*LEVEL_SCREEN_H;
        if(home<min_camera_y)home=min_camera_y;
        if(home>max_camera_y)home=max_camera_y;
        /* Latch the picture the moment a ride starts, and let it go the
           moment the player leaves the carrier. */
        if(riding_loop_carrier()){
            if(!g_ride_camera_hold){g_ride_camera_hold=1;g_ride_camera_y=target_y;}
        }else g_ride_camera_hold=0;
        /* A spring launch is the one climb that does not wait on the room
           hand-over: $102B6 drives the scroll itself, a table step a frame,
           so the picture leaves with the player on the frame he is thrown and
           he keeps his screen position the whole way up. */
        if(g_spring_camera_step){
            target_y-=(int)g_spring_camera_step;
            if(target_y<min_camera_y)target_y=min_camera_y;
            if(target_y>max_camera_y)target_y=max_camera_y;
        }
        else if(g_ride_camera_hold){
            /* World 5's loop carriers dive 112 pixels and climb back every
               160 frames.  Riding one, the follow band below hands the
               picture down after the carrier and the storey drift walks it
               back up again, so the ledge the player is aiming for moves
               under him for the whole of the ride.  Hold the picture where
               it stood when he stepped aboard: it is his own screen height
               that is being spent, and the band underneath still rescues him
               if the carrier really would take him out of the window. */
            target_y=g_ride_camera_y;
        }
        else if(target_y<home){target_y+=8; if(target_y>home)target_y=home;}
        else if(target_y>home){target_y-=8; if(target_y<home)target_y=home;}
        /* The hand-over above is the resting behaviour, but on its own it is a
           snap: a fall crosses the storey boundary far faster than eight
           pixels a frame and leaves Mario hanging below the bottom of the
           240-line window until the drift catches up.  Bound the drift with a
           follow band instead, so the picture is dragged with him the instant
           he would leave it.  His sprite is 48 tall from g_player_y, and the
           band keeps 24 px of headroom and 8 px under his feet -- wide enough
           that an ordinary jump inside a storey never moves the camera, which
           is what makes the storey the resting position rather than a target
           the follow keeps fighting. */
        /* A held ride keeps the band, but only as the last resort it is:
           eight pixels of margin instead of the resting 24, so the picture
           gives way at the very edge of the window rather than following the
           carrier down. */
        {
            int head=g_ride_camera_hold?8:24;
            if(target_y>py-head)target_y=py-head;
            if(target_y<py+48-232)target_y=py+48-232;
        }
        if(target_y<min_camera_y)target_y=min_camera_y;
        if(target_y>max_camera_y)target_y=max_camera_y;
        if(g_ride_camera_hold)g_ride_camera_y=target_y;
        new_row=target_y>>3;
        while(g_camera_row<new_row){++g_camera_row;queue_level_row(g_camera_row+30);}
        while(g_camera_row>new_row){--g_camera_row;queue_level_row(g_camera_row-1);}
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
    g_spring_camera_step=0;
}

/* Slot 9's fake parallax.  $14804 fills the Mega Drive's per-cell horizontal
   scroll table for plane B from four free-running counters.  Its level-9
   dispatcher ($14A44) changes sources at d0=32,56,80,119:

     lines   0..31   $FF008E   +1 every 8 video frames (drifts right)
     lines  32..55   $FF0064   -1 every 4 video frames (drifts left)
     lines  56..79   $FF008C   -1 every 8 video frames (drifts left)
     lines 80..118   skipped, so it stands still
     lines 119 down  $FF003A   half the camera step

   A 7UP latches BXR for the next line after horizontal blanking.  Its raster
   compare IRQ is raised at the end of the compared line, specifically so BXR
   can be changed before the following line's latch.  Arm the first comparison
   at each frame boundary and chain the other three from the IRQ; polling the
   TETSU raster here used to stall the entire game through line 120. */
#define PARALLAX_LEVEL     8    /* g_current_level, i.e. ROM slot 9 */
#define PARALLAX_SPLIT_B   32
#define PARALLAX_SPLIT_C   56
#define PARALLAX_SPLIT_END 80
#define PARALLAX_SPLIT_BUSH 119
#define PARALLAX_VDC_IRQ_LEVEL 12 /* PC-FX interrupt input for HuC6270-A */
#define PARALLAX_VDC_REG_PORT  0x400
#define PARALLAX_RCR_BIAS      64

static unsigned g_parallax_clock;
static volatile u16 g_parallax_bxr[5];
static volatile unsigned char g_parallax_irq_phase;
static volatile unsigned char g_parallax_irq_active;

/* Raster IRQs can land between a main-loop register-select write and its data
   write.  Restore the interrupted selector exactly as the official GMaker
   7UP notes prescribe; otherwise an apparently unrelated BAT/SATB transfer
   can occasionally write its data to BXR or RCR. */
__attribute__ ((interrupt_handler)) static void parallax_vdc_irq(void)
{
    u16 saved_reg=vdc_get_last_regnum(BACK_CHIP);
    u16 status=vdc_status(BACK_CHIP); /* Reading status acknowledges the IRQ. */

    if(status&VDC_STAT_RR){
        unsigned phase=g_parallax_irq_phase;
        if(g_parallax_irq_active && phase<4){
            static const u16 next_raster[4]={
                PARALLAX_SPLIT_C+PARALLAX_RCR_BIAS,
                PARALLAX_SPLIT_END+PARALLAX_RCR_BIAS,
                PARALLAX_SPLIT_BUSH+PARALLAX_RCR_BIAS,
                0
            };
            ++phase;
            g_parallax_irq_phase=(unsigned char)phase;
            vdc_setreg(BACK_CHIP,VDC_REG_BXR,g_parallax_bxr[phase]);
            vdc_set_raster(BACK_CHIP,next_raster[phase-1]);
        }else{
            /* RCR=0 cannot match the 7UP's visible counter (RCR-64). */
            vdc_set_raster(BACK_CHIP,0);
        }
    }

    /* Selecting a register does not write it; it only restores the address
       port that the interrupted libpcfx operation was about to use. */
    out16(PARALLAX_VDC_REG_PORT,saved_reg);
}

static void init_parallax_irq(void)
{
    /* Mask ordinary sources while replacing the firmware vector, then expose
       only HuC6270-A.  Level 15 is not represented by the mask register. */
    irq_set_mask(0x7F);
    irq_set_raw_handler(PARALLAX_VDC_IRQ_LEVEL,parallax_vdc_irq);
    irq_set_level(8);
    irq_level_enable(PARALLAX_VDC_IRQ_LEVEL);
    irq_enable();
}

static void backdrop_parallax_arm(void)
{
    int camera;
    unsigned t;
    if(g_in_boss || !g_level || g_current_level!=PARALLAX_LEVEL){
        g_parallax_irq_active=0;
        vdc_set_raster(BACK_CHIP,0);
        return;
    }
    t=g_parallax_clock;
    /* commit_stage_camera() published this frame's camera immediately before
       this routine.  Keep its half-rate value for the forest-only final band. */
    camera=(int)((g_camera_x>>1)&0x1FF);
    /* BXR counts the view rightwards, so a band that drifts right on screen
       needs a falling BXR.  The plane is 64 cells wide and wraps at 512. */
    g_parallax_bxr[0]=(u16)((0u-(t>>3))&0x1FFu);
    g_parallax_bxr[1]=(u16)((t>>2)&0x1FFu);
    g_parallax_bxr[2]=(u16)((t>>3)&0x1FFu);
    g_parallax_bxr[3]=0;
    g_parallax_bxr[4]=(u16)camera;
    g_parallax_irq_phase=0;
    g_parallax_irq_active=1;

    /* Publish the top band before enabling the first comparison.  Gameplay
       has complete SATBs resident on both VDCs, so retain both display bits. */
    vdc_setreg(BACK_CHIP,VDC_REG_BXR,g_parallax_bxr[0]);
    vdc_set_raster(BACK_CHIP,PARALLAX_SPLIT_B+PARALLAX_RCR_BIAS);
    vdc_setreg(BACK_CHIP,VDC_REG_CR,
               (u16)(VDC_CR_BB|VDC_CR_SB|VDC_CR_IRQ_RC));
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
    /* The cellophane layer is part of that same plane, so it takes its scroll. */
    if(g_overlay_active)king_set_scroll(KING_BG0,(s16)(g_camera_x&0x01FF),0);
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
    /* Metasprite $1A9, the slot 13 carrier, is 64x24 anchored at its own
       top-left corner, so its surface is exactly the object's Y. */
    if (g_in_boss && g_boss_class==3) return 24;
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

/* Each slot 13 carrier's Y from before this frame's step, which is what the
   boss handler tests; see boss13_lift_y(). */
static short g_boss13_lift_prev[2];

/* $FF00BC: one latch for the whole level.  A lever ($10836) raises it when a
   player stands on it and it stays raised after they step off, which is what
   lets one player press a lever and then walk to the lift it releases; the
   lift ($10892) clears it again at the end of its round trip. */
static unsigned char g_stage_switch;

/* $10464, the rise the spring throws its rider through, one byte a frame. */
static const unsigned char g_spring_rise[69]={
    8,8,8,8,7,7,7,7,7,6,6,6,6,6,6,6,6,6,6,5,5,5,5,5,4,4,4,4,4,4,4,4,
    3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1
};
/* $FF00AE names the object in flight; while it does, $18028 skips that
   object's gravity and $145E2 scrolls the room instead.  Zero when nobody is
   airborne on a spring, otherwise one past the step taken last frame. */
static unsigned char g_spring_flight;

static void init_moving_platforms(void)
{
    MovingPlatform *p;
    int i;
    for(i=0;i<MAX_MOVING_PLATFORMS;++i){
        g_moving_platforms[i].kind=PLATFORM_KIND_LIFT;
        g_moving_platforms[i].state=PLATFORM_STATE_IDLE;
        g_moving_platforms[i].timer=0;
        g_moving_platforms[i].hidden=0;
    }
    g_moving_platform_count=0;
    g_on_moving_platform=-1;
    g_stage_switch=0;
    g_spring_flight=0;
    g_spring_camera_step=0;
    g_drop_platform_timer=0;
    if (g_in_boss) {
        /* The $8CEC room owns two live carriers. They are not decoration:
           both Mario and the frog use their current Y while waiting/jumping. */
        /* $7B5C creates them at ($80,$68) and ($140,$15E) with metasprite
           $1A9, walks each one pixel a frame in opposite directions and
           destroys it at the far end, where the next frame makes it again.
           They travel the whole shaft, not the short stroke the placeholder
           used, and they never turn around.  The descending one runs past
           the source's own $15E to BOSS13_LIFT_EXIT so it leaves the screen
           before it wraps rather than vanishing in view. */
        if(g_boss_class==3){
            p=&g_moving_platforms[g_moving_platform_count++];
            g_boss13_lift_prev[0]=BOSS13_LIFT_TOP;g_boss13_lift_prev[1]=BOSS13_LIFT_BOTTOM;
            p->x=(s32)0<<FIX_SHIFT;p->y=(s32)BOSS13_LIFT_TOP<<FIX_SHIFT;
            p->min_y=BOSS13_LIFT_TOP;p->max_y=BOSS13_LIFT_EXIT;p->dir=1;p->speed=1;
            p=&g_moving_platforms[g_moving_platform_count++];
            p->x=(s32)192<<FIX_SHIFT;p->y=(s32)BOSS13_LIFT_BOTTOM<<FIX_SHIFT;
            p->min_y=BOSS13_LIFT_TOP;p->max_y=BOSS13_LIFT_BOTTOM;p->dir=-1;p->speed=1;
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
            p->state=PLATFORM_STATE_IDLE;p->timer=0;p->step=0;p->hidden=0;
            /* A lever keeps its record's pressed pose here so it can swap
               back and forth with the raised one banked next to it. */
            if (rec[3]==PLATFORM_KIND_LEVER) p->step=(unsigned char)rec[2];
            /* A spring keeps its record's resting pose the same way, so it can
               reach the two wound ones banked behind it. */
            if (rec[3]==PLATFORM_KIND_SPRING) p->step=(unsigned char)rec[2];
            /* A pulse keeps its record's first pose here so its cycle can
               step forward through the three banked past it. */
            if (rec[3]==PLATFORM_KIND_PULSE) p->step=(unsigned char)rec[2];
            /* A fireball does the same for the four poses $AECE names, and
               starts where $B00C leaves it: at the bottom of its leap on the
               rising pose, with the arc index at zero. */
            if (rec[3]==PLATFORM_KIND_FIREBALL) p->step=(unsigned char)rec[2];
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
    case PLATFORM_KIND_SHUTTLE: {    /* $F78E */
        /* The record position is the right end of the run and the shuttle only
           ever leaves it, so these shafts can leave the player one frame to
           make a crossing.  Bound both ends against home_x rather than
           counting frames, so the extra travel cannot walk the whole run a
           little further right on every cycle. */
        int hx=(int)(p->home_x>>FIX_SHIFT),x;
        int overrun=(g_current_level==SHUTTLE_OVERRUN_LEVEL_12)?SHUTTLE_OVERRUN_SHORT:
                    (g_current_level==SHUTTLE_OVERRUN_LEVEL_13||
                     g_current_level==SHUTTLE_OVERRUN_LEVEL_19)?SHUTTLE_OVERRUN:0;
        p->x+=(s32)p->dir*FIX_ONE;
        x=(int)(p->x>>FIX_SHIFT);
        if (p->dir<0) { if (x<=hx-SHUTTLE_RUN) p->dir=1; }
        else if (x>=hx+overrun)
            p->dir=-1;
        break;
    }
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
    case PLATFORM_KIND_PHASE:        /* $FAF8 */
        /* Six timed phases, 80/21/16/32/16/21 frames.  The object drops 8px
           entering phase 2 and lifts back entering phase 5, and $FB80 blanks
           its metasprite for the whole of phase 3. */
        ++p->timer;
        switch (p->state) {
        case 0: if (p->timer>=0x50){p->state=1;p->timer=0;} break;
        case 1: if (p->timer>=0x15){p->state=2;p->timer=0;p->y+=8*FIX_ONE;} break;
        case 2: if (p->timer>=0x10){p->state=3;p->timer=0;p->hidden=1;} break;
        case 3: if (p->timer>=0x20){p->state=4;p->timer=0;p->hidden=0;} break;
        case 4: if (p->timer>=0x10){p->state=5;p->timer=0;p->y-=8*FIX_ONE;} break;
        default:if (p->timer>=0x15){p->state=0;p->timer=0;} break;
        }
        break;
    case PLATFORM_KIND_DRIFT_R:      /* $FC1E */
    case PLATFORM_KIND_DRIFT_L: {    /* $FDAC, the same run mirrored */
        s32 step=(p->kind==PLATFORM_KIND_DRIFT_R)?FIX_ONE:-FIX_ONE;
        ++p->timer;
        switch (p->state) {
        /* 0x40 + 0x25 + 0x20 = 0x85 pixels of travel, stepping down 8 pixels
           at each phase change. */
        case 0: p->x+=step; if(p->timer>=0x40){p->state=1;p->timer=0;} break;
        case 1: p->x+=step; if(p->timer>=0x25){p->state=2;p->timer=0;p->y+=8*FIX_ONE;} break;
        case 2: p->x+=step; if(p->timer>=0x20){p->state=3;p->timer=0;p->y+=8*FIX_ONE;} break;
        case 3: if(p->timer>=0x20){p->state=4;p->timer=0;p->hidden=1;} break;
        /* $FD90/$FF1E undo the two 8px drops and the whole 0x85 of travel, so
           the carrier reappears exactly on its record position. */
        default:if(p->timer>=0x30){
                    p->state=0;p->timer=0;p->hidden=0;
                    p->y-=16*FIX_ONE; p->x-=step*0x85;
                    if (g_on_moving_platform==index) { g_on_moving_platform=-1; g_grounded=0; }
                }
                break;
        }
        break;
    }
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
    case PLATFORM_KIND_FIREBALL: {   /* $AECE */
        /* Four phases in the source object's $1C, with $1D as the phase's own
           counter.  Nothing here is horizontal: the fireball is thrown 218
           pixels straight up the $B01E arc over 52 frames, drifts ten more up
           and ten back down a pixel a frame, then falls the same arc read
           backwards and drops under the floor to start again. */
        int sx=(int)(p->x>>FIX_SHIFT)-g_camera_x;
        /* $B242 streams the record in on its tile trigger.  Every record here
           is instantiated at level load instead, so hold it under the floor at
           its authored Y until the camera reaches its column -- the same
           moment -- rather than letting it cycle unseen. */
        if (sx>SCREEN_W+96 || sx<-96) {
            p->y=p->home_y;p->state=0;p->timer=0;
            p->shape=(unsigned char)p->step;
            break;
        }
        switch (p->state) {
        case 0:  /* $AED8: rising, one arc entry a frame */
            p->y-=(s32)g_fireball_arc[p->timer]<<FIX_SHIFT;
            if (++p->timer>=FIREBALL_ARC_LEN) {p->state=1;p->timer=0;}
            break;
        case 1:  /* $AF34: ten more pixels up, a pixel a frame */
            p->y-=FIX_ONE;
            if (++p->timer>=FIREBALL_APEX_TICKS) {p->state=2;p->timer=0;}
            break;
        case 2:  /* $AF7C: and ten back down */
            p->y+=FIX_ONE;
            if (++p->timer>=FIREBALL_APEX_TICKS) {
                p->y-=8*FIX_ONE;                       /* $AF8E */
                p->state=3;p->timer=FIREBALL_ARC_LEN-1;/* $AF98 loads $33 */
            }
            break;
        default: /* $AFC8: falling, the same arc walked backwards */
            p->y+=(s32)g_fireball_arc[p->timer]<<FIX_SHIFT;
            if (p->timer==0) {
                /* $AFEE resets every one of them to object Y $190.  Each
                   record is authored on its own floor line here, so return it
                   to that instead; on the source's single-storey stages the
                   two are the same place. */
                p->y=p->home_y;p->state=0;p->timer=0;
            } else --p->timer;
            break;
        }
        /* $AF16/$AF5E/$AFAA/$B00C name one pose per phase, banked in phase
           order behind the record's own shape. */
        p->shape=(unsigned char)(p->step+p->state);
        /* Like the boulder, it is a hazard and not a carrier. */
        if (g_invuln==0) {
            const unsigned short *sh=platform_shape(p);
            int bw=sh?(int)sh[5]:16,bh=sh?(int)sh[6]:16;
            if (aabb(player_body_x(),player_body_y(),PLAYER_BODY_W,PLAYER_BODY_H,
                     platform_draw_x(p),platform_draw_y(p),bw,bh))
                hurt_player();
        }
        break;
    }
    case PLATFORM_KIND_LEVER:        /* $10836 */
        /* The lever never moves; it only latches $FF00BC under a rider and
           shows the pressed pose for as long as the latch is up. */
        if (g_on_moving_platform==index) {
            if (!g_stage_switch) { g_stage_switch=1; p->shape=p->step; }
        } else if (!g_stage_switch) {
            p->shape=(unsigned char)(p->step+1);
        }
        break;
    case PLATFORM_KIND_RISER:        /* $10892 */
        base=platform_storey_base(p);
        if (p->state==PLATFORM_STATE_IDLE) {
            /* Both conditions, and in this order: the lift needs a rider of
               its own and the lever already pressed.  A rider alone leaves it
               parked, which is the whole of the shaft's puzzle. */
            if (g_on_moving_platform==index && g_stage_switch) p->state=1;
            break;
        }
        if (p->state==1) {
            p->y-=2*FIX_ONE;
            if ((int)(p->y>>FIX_SHIFT)<=base+STAGE_LIFT_TOP) p->state=2;
        } else {
            p->y+=2*FIX_ONE;
            if ((int)(p->y>>FIX_SHIFT)>=base+STAGE_LIFT_BOTTOM) {
                p->state=PLATFORM_STATE_IDLE; g_stage_switch=0;
            }
        }
        break;
    case PLATFORM_KIND_RETURN:       /* $1097C */
        base=platform_storey_base(p);
        if (p->state==PLATFORM_STATE_IDLE) {
            if (g_on_moving_platform==index) p->state=1;
            break;
        }
        if (p->state==1) {
            p->y+=2*FIX_ONE;
            if ((int)(p->y>>FIX_SHIFT)>=base+STAGE_LIFT_BOTTOM) p->state=2;
        } else {
            p->y-=2*FIX_ONE;
            /* $10A2E reaches this one threshold through d1, the rider slot,
               where its three siblings all use d6, the object -- a slip that
               makes the end of the climb depend on who is aboard.  The test
               is anchored to the lift, so it stops where 0x94 says. */
            if ((int)(p->y>>FIX_SHIFT)<=base+STAGE_LIFT_HOME) p->state=PLATFORM_STATE_IDLE;
        }
        break;
    case PLATFORM_KIND_SPRING:       /* $102B6 */
        switch (p->state) {
        case PLATFORM_STATE_IDLE:
            /* One player is all $102B6 needs: the run of tests it makes past
               the rider is the two-player gate at $FF0098, and with that flag
               clear the very first branch takes the launch. */
            if (g_on_moving_platform!=index) break;
            /* Each wound pose is taller than the last by exactly the amount
               its Y drops, so the spring grows out of a fixed foot.  The
               source nudges the rider 32 pixels along with it; the port's
               player anchor is not the source's, so centre him on the coil
               instead, which is what that nudge amounts to. */
            p->shape=(unsigned char)(p->step+1);
            p->y-=0x18*FIX_ONE;
            g_player_x=(s32)(platform_draw_x(p)+platform_width(p)/2
                             -PLAYER_BODY_XOFF-PLAYER_BODY_W/2)<<FIX_SHIFT;
            p->state=1; p->timer=0;
            break;
        case 1:
            if (++p->timer<SPRING_WIND_FRAMES) break;
            p->shape=(unsigned char)(p->step+2);
            p->y-=8*FIX_ONE;
            p->state=2; p->timer=0;
            break;
        case 2:
            /* Fully wound, and it holds there until someone is aboard: a
               rider who walks off during the wind-up leaves it cocked. */
            if (g_on_moving_platform!=index) break;
            g_spring_flight=1;
            g_on_moving_platform=-1; g_grounded=0;
            g_jump_active=0; g_bounce_active=0; g_vy=0;
            /* $103E0 throws the object down to 0x168 and $10456 clears its
               slot outright once the rise is walked, so the spring is spent
               after the one launch. */
            p->hidden=1;
            p->state=3;
            break;
        default:
            break;
        }
        break;
    case PLATFORM_KIND_DIAMOND:      /* $FF3A */
        p->x+=(p->state==0||p->state==1)?FIX_ONE:-FIX_ONE;
        p->y+=(p->state==0||p->state==3)?FIX_ONE:-FIX_ONE;
        if (++p->timer>=48) { p->timer=0; p->state=(unsigned char)((p->state+1)&3); }
        break;
    case PLATFORM_KIND_PULSE: {       /* $104AA */
        /* $105CC's pose table walks 2,3,4,5,3,2,1 before repeating; $105B8
           holds one jump-table entry per pose, each waiting 10, 10, 10, 20
           or 32 frames (poses 1-5) before the state advances.  Every pose
           but 5 also writes the metasprite one further past the record's
           own for each step up from it -- shape+0..+3 for poses 1..4 -- and
           5 just idles on shape+3 before the sequence runs back down. */
        static const unsigned char seq[7]={2,3,4,5,3,2,1};
        static const unsigned char delay[6]={0,10,10,10,20,32};
        unsigned char pose=seq[p->state%7];
        if (++p->timer>=delay[pose]) {
            p->timer=0;
            if (pose!=5) p->shape=(unsigned char)(p->step+(pose-1));
            p->state=(unsigned char)((p->state+1)%7);
        }
        break;
    }
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
        } else if (p->kind==PLATFORM_KIND_LIFT && g_in_boss && g_boss_class==3) {
            /* $7828/$7C2C destroy the carrier at the end of its run and the
               next frame recreates it at the other end, so it loops instead
               of shuttling.  $7B14 gates the whole routine on bit 0 of the
               frame counter, so a carrier crosses its shaft at one pixel
               every other frame -- the same cadence $8E10/$8EC0 give the
               boss's own descent and climb.  Running it at a pixel a frame
               made the boss and the carrier it is supposed to ride down
               drift apart by half the shaft. */
            /* $7B5C returns without touching either carrier while the
               driver holds cel 3, the pose $1633A selects for a hit, so the
               shafts stop with the boss. */
            if (i<2) g_boss13_lift_prev[i]=(short)(p->y>>FIX_SHIFT);
            if (half && g_boss_invuln==0) {
                p->y+=(s32)p->dir*FIX_ONE;
                if (p->dir>0 && (int)(p->y>>FIX_SHIFT)>=p->max_y) {
                    if (g_on_moving_platform==i) { g_on_moving_platform=-1; g_grounded=0; }
                    p->y=(s32)p->min_y<<FIX_SHIFT;
                } else if (p->dir<0 && (int)(p->y>>FIX_SHIFT)<=p->min_y) {
                    if (g_on_moving_platform==i) { g_on_moving_platform=-1; g_grounded=0; }
                    p->y=(s32)p->max_y<<FIX_SHIFT;
                }
            }
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
            /* Flying Koopas use the terrain-independent patrol.  CHIP flies the
               same path but breaks off to bomb, so $C748 drives it itself, and
               state-5 pterodactyls retain their separate $C472 behavior. */
            g_enemies[i].flying=(e[7]==ENEMY_KIND_FLYING_KOOPA);
            g_enemies[i].flight_phase=0;
            g_enemies[i].behavior_phase=0;g_enemies[i].behavior_timer=0;
            g_enemies[i].face_right=0;g_enemies[i].shot_slot=0;
            g_enemies[i].ledge_pinned=0;
            g_enemies[i].jumped_over=0;
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
    g_luigi.carry_index=-1;g_luigi.lift_timer=0;g_luigi.throw_timer=0;
    g_luigi.crate_hide=0;
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
            if (multiplayer_active() && g_luigi.carry_index==i) {
                c->x=g_luigi.x+(s32)4*FIX_ONE;
                c->y=g_luigi.y+(s32)(g_luigi.crate_hide?24:-13)*FIX_ONE;
            } else {
                c->x=g_player_x+(s32)4*FIX_ONE;
                /* The disguised 24px crate rests on the carrier's standing
                   surface (anchor + 24), rather than floating above it. */
                c->y=g_player_y+(s32)(g_crate_hide?24:-13)*FIX_ONE;
            }
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

static int find_luigi_pickup_crate(void)
{
    int i;
    int px=(int)(g_luigi.x>>FIX_SHIFT)+PLAYER_BODY_XOFF;
    int py=(int)(g_luigi.y>>FIX_SHIFT)+PLAYER_BODY_YOFF;
    int best=-1,bestd=9999;
    for(i=0;i<MAX_CRATES;++i){
        int cx,cy,d;
        if(!g_crates[i].active||g_crates[i].carried)continue;
        cx=(int)(g_crates[i].x>>FIX_SHIFT);cy=(int)(g_crates[i].y>>FIX_SHIFT);
        if(!aabb(px-20,py-8,PLAYER_BODY_W+40,PLAYER_BODY_H+16,
                 cx,cy,CRATE_W,CRATE_H))continue;
        d=iabs_i((cx+12)-(px+8));
        if(d<bestd){bestd=d;best=i;}
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

static void do_luigi_crate_action(u16 btn)
{
    if(g_luigi.carry_index>=0){
        Crate *c=&g_crates[g_luigi.carry_index];
        spawn_crate_item(c);c->carried=0;
        if(btn&JOY_UP){c->x=g_luigi.x+4*FIX_ONE;c->y=g_luigi.y-8*FIX_ONE;c->vx=0;c->vy=-6*FIX_ONE;}
        else if(btn&JOY_DOWN){c->x=g_luigi.x+4*FIX_ONE;c->y=g_luigi.y+34*FIX_ONE;c->vx=0;c->vy=6*FIX_ONE;}
        else{c->x=g_luigi.x+(g_luigi.facing>0?18:-14)*FIX_ONE;c->y=g_luigi.y+26*FIX_ONE;c->vx=g_luigi.facing*6*FIX_ONE;c->vy=0;}
        c->thrown=1;c->flight_timer=0;g_luigi.carry_index=-1;
        g_luigi.lift_timer=0;g_luigi.throw_timer=31;
        Play_Sound_Sample(SFX_CRATE_THROW,0,0);
    }else{
        int i=find_luigi_pickup_crate();
        if(i>=0){g_luigi.carry_index=i;g_crates[i].carried=1;g_crates[i].thrown=0;g_luigi.lift_timer=18;g_luigi.vx=0;}
    }
}

static void fire_luigi_fireball(void)
{
    int i;Fireball *f=0;
    if((!g_debug_mode&&g_luigi.fireball_ammo<=0)||g_luigi.mode!=PMODE_NORMAL)return;
    for(i=0;i<MAX_FIREBALLS;++i)if(!g_fireballs[i].active){f=&g_fireballs[i];break;}
    if(!f)return;
    if(!g_debug_mode)--g_luigi.fireball_ammo;
    f->active=1;f->x=g_luigi.x+(g_luigi.facing>0?32:-24)*FIX_ONE;
    f->y=g_luigi.y+16*FIX_ONE;f->vx=g_luigi.facing*7*FIX_ONE;f->life=52;
    g_luigi.throw_timer=31;
    Play_Sound_Sample(SFX_FIREBALL,0,0);
}

/* stomped is 1 when Mario landed on the enemy's head.  The pop is the
   stomp sound: fireballs and thrown crates defeat silently, which is why
   the flag arrives from the call site.  e->jumped_over cannot carry it --
   that state machine only arms while the player is airborne above the
   enemy, and the landing frame (bottom at or past the enemy's top) both
   fails its arming test and clears a previously armed flag before the
   stomp check below runs. */
static void defeat_enemy(Enemy *e, int stomped)
{
    if (!e->active || e->defeated) return;
    e->defeated=1;
    e->vx=0;
    e->vy=FIX_ONE;
    if (stomped) Play_Sound_Sample(SFX_ENEMY_DEAD,0,0);
}

/* The rolling boulder is scenery with a hitbox, not a creature: $B844 never
   registers it with the defeat handler, so nothing Mario has -- a stomp, a
   thrown crate, a fireball -- removes it.  It only leaves the stage by
   rolling out of view.  Landing on one is a hit like touching it anywhere
   else. */
static int enemy_indestructible(const Enemy *e)
{
    return e->kind==ENEMY_KIND_BOULDER;
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
        /* Fire is not stopped by the stage: it passes through ledges, walls
           and everything else the map is made of, and only ends when it runs
           out of life or leaves the camera's reach. */
        if (x<g_camera_x-32 || x>g_camera_x+288 || f->life==0) {
            f->active=0; continue;
        }
        --f->life;
        for (j=0;j<MAX_ENEMIES;++j) {
            Enemy *e=&g_enemies[j];
            int ex,ey,eh;
            if (!e->active || !e->spawned || e->defeated) continue;
            ex=(int)(e->x>>FIX_SHIFT); ey=(int)(e->y>>FIX_SHIFT); eh=e->height;
            if (enemy_indestructible(e)) continue;
            if (aabb(x+4,y+5,16,14,ex+3,ey+3,18,eh-5)) {
                /* Fire travels through a target after defeating it. */
                defeat_enemy(e,0);
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

/* The Y past which there is no more stage under Mario.  A flat level ends at
   its single storey; a stacked one ends at the bottom of whichever storey the
   route joins to the live room -- room_window's rhi -- because that is the
   lowest the camera is ever allowed to travel. */
static int player_pit_floor(void)
{
    int clo,chi,rlo,rhi;
    if(g_level_rows<=1||g_in_boss)return g_level_rows*LEVEL_SCREEN_H;
    room_window(g_room,&clo,&chi,&rlo,&rhi);
    (void)clo;(void)chi;(void)rlo;
    return (rhi+1)*LEVEL_SCREEN_H;
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
    if(!multiplayer_active()) music_stop_for_transition();
    Play_Sound_Sample(SFX_PLAYER_DEATH,0,0);
}

static void multiplayer_all_out(void)
{
    music_stop_for_transition();
    if(g_continues>0) load_continue_hidden();
    else enter_boot();
}

static void finish_mario_multiplayer_death(void)
{
    if(g_lives>0)--g_lives;
    if(g_lives<=0){
        g_lives=0;g_player_mode=PMODE_OUT;
        if(g_luigi.out)multiplayer_all_out();
        return;
    }
    g_player_x=(s32)((int)(g_player_x>>FIX_SHIFT))<<FIX_SHIFT;
    g_player_y=(s32)g_camera_y<<FIX_SHIFT;
    g_vx=g_vy=0;g_death_timer=0;g_player_hearts=PLAYER_MAX_HEARTS;
    g_fireball_ammo=3;
    g_player_mode=PMODE_REENTRY;
}

static void hurt_luigi(void)
{
    if(g_luigi.crate_hide&&g_luigi.carry_index>=0)return;
    if(g_luigi.mode!=PMODE_NORMAL||g_luigi.invuln>0||g_luigi.out)return;
    if(g_luigi.hearts>0)--g_luigi.hearts;
    g_luigi.invuln=HURT_FRAMES;
}

static void begin_luigi_death(void)
{
    if(g_luigi.mode!=PMODE_NORMAL||g_luigi.out)return;
    if(g_luigi.carry_index>=0){
        Crate *c=&g_crates[g_luigi.carry_index];
        c->carried=0;c->thrown=0;c->vx=c->vy=0;
        g_luigi.carry_index=-1;g_luigi.crate_hide=0;
    }
    g_luigi.mode=PMODE_DYING;g_luigi.death_timer=0;g_luigi.invuln=0;
    g_luigi.vx=0;g_luigi.vy=-(6*FIX_ONE+FIX_ONE/2);g_luigi.down=0;
    Play_Sound_Sample(SFX_PLAYER_DEATH,0,0);
}

static void update_luigi(u16 btn,u16 pressed)
{
    s32 ox,oy,ovx,ovy;int og,of,oja,oji,opby,oop,odrop;
    const s32 speed=2*FIX_ONE;
    int i,px,py;
    if(!multiplayer_active()||g_state!=STATE_PLAY)return;
    if(g_luigi.out)return;
    if(g_luigi.mode==PMODE_FADE_IN)g_luigi.mode=PMODE_NORMAL;
    if(g_luigi.mode==PMODE_DYING){
        g_luigi.y+=g_luigi.vy;g_luigi.vy+=64;
        if(g_luigi.vy>7*FIX_ONE)g_luigi.vy=7*FIX_ONE;
        ++g_luigi.death_timer;
        if((int)(g_luigi.y>>FIX_SHIFT)>g_camera_y+256){
            if(g_luigi.lives>0)--g_luigi.lives;
            if(g_luigi.lives<=0){g_luigi.lives=0;g_luigi.out=1;g_luigi.mode=PMODE_OUT;if(g_player_mode==PMODE_OUT)multiplayer_all_out();return;}
            g_luigi.y=(s32)g_camera_y<<FIX_SHIFT;g_luigi.vx=g_luigi.vy=0;g_luigi.down=0;
            g_luigi.death_timer=0;g_luigi.hearts=PLAYER_MAX_HEARTS;g_luigi.fireball_ammo=3;
            g_luigi.mode=PMODE_REENTRY;
        }
        return;
    }
    if(g_luigi.mode==PMODE_REENTRY){
        if(btn&JOY_LEFT){g_luigi.x-=speed;g_luigi.facing=-1;}
        else if(btn&JOY_RIGHT){g_luigi.x+=speed;g_luigi.facing=1;}
        if((int)(g_luigi.x>>FIX_SHIFT)<g_camera_x)g_luigi.x=(s32)g_camera_x<<FIX_SHIFT;
        if((int)(g_luigi.x>>FIX_SHIFT)>g_camera_x+224)g_luigi.x=(s32)(g_camera_x+224)<<FIX_SHIFT;
        if(++g_luigi.death_timer>=MULTI_HOVER_FRAMES){g_luigi.mode=PMODE_NORMAL;g_luigi.invuln=MULTI_INVULN_FRAMES;g_luigi.vy=0;g_luigi.grounded=0;}
        return;
    }
    if(g_luigi.invuln>0)--g_luigi.invuln;
    if(g_luigi.hearts<=0&&g_luigi.invuln==0){begin_luigi_death();return;}

    ox=g_player_x;oy=g_player_y;ovx=g_vx;ovy=g_vy;og=g_grounded;of=g_facing;
    oja=g_jump_active;oji=g_jump_index;opby=g_prev_body_y;oop=g_on_moving_platform;odrop=g_drop_platform_timer;
    g_player_x=g_luigi.x;g_player_y=g_luigi.y;g_vx=g_luigi.vx;g_vy=g_luigi.vy;
    g_grounded=g_luigi.grounded;g_facing=g_luigi.facing;g_jump_active=g_luigi.jump_active;g_jump_index=g_luigi.jump_index;
    g_prev_body_y=player_body_y();g_on_moving_platform=-1;g_drop_platform_timer=0;
    g_luigi.crate_hide=(g_luigi.carry_index>=0&&g_grounded&&(btn&JOY_DOWN))?1:0;
    g_luigi.down=(g_luigi.carry_index<0&&g_grounded&&g_luigi.lift_timer==0&&
                  g_luigi.throw_timer==0&&(btn&JOY_DOWN))?1:0;
    if((pressed&JOY_II)&&!g_luigi.crate_hide&&g_luigi.lift_timer==0&&g_luigi.throw_timer==0)
        do_luigi_crate_action(btn);
    if((pressed&JOY_III)&&g_luigi.lift_timer==0&&g_luigi.throw_timer==0)
        fire_luigi_fireball();
    if(g_luigi.down&&!(pressed&JOY_I))g_vx=0;
    else if(g_luigi.lift_timer>0){--g_luigi.lift_timer;g_vx=0;}
    else if(g_luigi.throw_timer>0&&g_grounded)g_vx=0;
    else if(btn&JOY_LEFT){g_vx=-speed;g_facing=-1;}else if(btn&JOY_RIGHT){g_vx=speed;g_facing=1;}else g_vx=0;
    if(g_grounded&&(pressed&JOY_I)&&g_luigi.lift_timer==0&&g_luigi.throw_timer==0){g_grounded=0;g_jump_active=1;g_jump_index=0;g_luigi.down=0;g_luigi.crate_hide=0;Play_Sound_Sample(SFX_JUMP,0,0);}
    resolve_player_horizontal(g_player_x+g_vx);
    if((int)(g_player_x>>FIX_SHIFT)<g_camera_x)g_player_x=(s32)g_camera_x<<FIX_SHIFT;
    if(g_jump_active){int dy;if(g_jump_index<=19&&!(btn&JOY_I))g_jump_index=44-g_jump_index;if(g_jump_index>44)g_jump_index=44;dy=g_md_jump_dy[g_jump_index++];g_vy=(s32)dy*FIX_ONE;resolve_player_vertical(g_player_y+g_vy);if(g_jump_index>=45)g_jump_active=0;}
    else{g_vy+=88;if(g_vy>5*FIX_ONE)g_vy=5*FIX_ONE;resolve_player_vertical(g_player_y+g_vy);}
    g_luigi.x=g_player_x;g_luigi.y=g_player_y;g_luigi.vx=g_vx;g_luigi.vy=g_vy;g_luigi.grounded=g_grounded;g_luigi.facing=g_facing;g_luigi.jump_active=g_jump_active;g_luigi.jump_index=g_jump_index;++g_luigi.anim_clock;
    if(g_luigi.throw_timer>0)--g_luigi.throw_timer;
    if(g_player_mode==PMODE_OUT)update_camera();
    g_player_x=ox;g_player_y=oy;g_vx=ovx;g_vy=ovy;g_grounded=og;g_facing=of;g_jump_active=oja;g_jump_index=oji;g_prev_body_y=opby;g_on_moving_platform=oop;g_drop_platform_timer=odrop;
    if((int)(g_luigi.y>>FIX_SHIFT)>player_pit_floor()){begin_luigi_death();return;}
    px=(int)(g_luigi.x>>FIX_SHIFT)+PLAYER_BODY_XOFF;py=(int)(g_luigi.y>>FIX_SHIFT)+PLAYER_BODY_YOFF;
    if(g_luigi.invuln==0){
        for(i=0;i<MAX_ENEMIES;++i)if(g_enemies[i].active&&!g_enemies[i].defeated&&aabb(px+4,py+5,8,PLAYER_BODY_H-10,(int)(g_enemies[i].x>>FIX_SHIFT),(int)(g_enemies[i].y>>FIX_SHIFT),g_enemies[i].width,g_enemies[i].height)){hurt_luigi();break;}
        for(i=0;i<4;++i)if(g_enemy_shots[i].active&&aabb(px,py,PLAYER_BODY_W,PLAYER_BODY_H,(int)(g_enemy_shots[i].x>>FIX_SHIFT),(int)(g_enemy_shots[i].y>>FIX_SHIFT),g_enemy_shots[i].cols?g_enemy_shots[i].cols*16:16,16)){g_enemy_shots[i].active=0;hurt_luigi();break;}
    }
}

static void hurt_player(void)
{
    if (g_debug_mode) return;
    /* Holding DOWN while carrying enters the source box-disguise state: the
       crate-with-eyes is the only visible body and attacks pass through it. */
    if (g_crate_hide && g_carry_index>=0) return;
    if (g_player_mode!=PMODE_NORMAL || g_invuln>0) return;
    if (g_player_hearts>0) --g_player_hearts;
    Play_Sound_Sample(SFX_PLAYER_HURT,0,0);
    /* A killing blow (last heart spent) skips the one-second hurt interval
       and goes straight into the death animation.  The non-killing case still
       gets the 60-frame flicker so the player can recover from a near miss
       and so the source visual cue remains visible. */
    if (g_player_hearts<=0) {
        begin_player_death();
    } else {
        g_invuln=HURT_FRAMES;
        g_hurt_pending_death=0;
    }
}

/* $15AB0 dispatches on the item id through the jump table at $15B96.  Sound
   $9C -- the port's SFX_MENU_CONFIRM -- is the shared collect chime played by
   the caller; the $B0 jingle the life and ammo entries add on top of it has no
   capture in the ADPCM bank, so those two are left with the chime alone. */
static void apply_item(unsigned char kind, int player2)
{
    switch (kind) {
    case ITEM_FULL_HEAL:
        if(player2)g_luigi.hearts=PLAYER_MAX_HEARTS;
        else g_player_hearts=PLAYER_MAX_HEARTS;
        break;
    case ITEM_FIRST_AID:
        if(player2){if(g_luigi.hearts<PLAYER_MAX_HEARTS)++g_luigi.hearts;}
        else if(g_player_hearts<PLAYER_MAX_HEARTS)++g_player_hearts;
        break;
    case ITEM_POISON:
        /* The one harmful box.  $15AF4 spends a heart with the ordinary hurt
           sound and kills outright at zero, granting no mercy invulnerability
           the way an enemy hit does. */
        if(player2){if(g_luigi.hearts>0)--g_luigi.hearts;}
        else if(g_player_hearts>0)--g_player_hearts;
        Play_Sound_Sample(SFX_PLAYER_HURT,0,0);
        /* A killing poison pick-up (last heart spent) goes straight to the
           death animation instead of the 60-frame hurt flicker used for
           non-killing hits. */
        if(player2){if(g_luigi.hearts<=0)begin_luigi_death();}
        else if(g_player_hearts<=0)begin_player_death();
        break;
    case ITEM_EXTRA_LIFE:
        if(player2){if(g_luigi.lives<9)++g_luigi.lives;}
        else if(g_lives<9)++g_lives;
        break;
    case ITEM_STOPWATCH:
        g_enemy_freeze = STOPWATCH_FRAMES;
        break;
    case ITEM_SHIELD:
        if(player2)g_luigi.invuln=SHIELD_FRAMES;
        else g_invuln=SHIELD_FRAMES;
        break;
    case ITEM_FIREBALL:
        if(player2){if(g_luigi.fireball_ammo<9)++g_luigi.fireball_ammo;}
        else if(g_fireball_ammo<9)++g_fireball_ammo;
        break;
    default:
        break;
    }
}

/* $15984 tests the item pool against the player's whole 28x44 object box, not
   the narrower body rectangle the port uses for terrain and enemies.

   The drop is the port's own: a freed item pops out of the crate and falls
   under the same gravity a loose crate uses until it rests on the floor.  It
   becomes collectable as soon as the initial pop reaches its apex and starts
   falling.  Keeping the upward half of the pop non-collectable prevents the
   item from being swallowed on its first frame, while no longer forcing the
   player to wait for it to land. */
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
            /* Let either player collect the item during its descent. */
            if (!it->grounded && it->vy <= 0) continue;
        }
        {
            int player2=0;
            int ix=(int)(it->x>>FIX_SHIFT)+4,iy=(int)(it->y>>FIX_SHIFT);
            if(g_player_mode!=PMODE_NORMAL||
               !aabb(px,py,28,44,ix,iy,ITEM_W-4,ITEM_H-4)){
                int lx=(int)(g_luigi.x>>FIX_SHIFT)+4;
                int ly=(int)(g_luigi.y>>FIX_SHIFT);
                if(!multiplayer_active()||g_luigi.out||g_luigi.mode!=PMODE_NORMAL||
                   !aabb(lx,ly,28,44,ix,iy,ITEM_W-4,ITEM_H-4))continue;
                player2=1;
            }
        it->active = 0;
        Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
            apply_item(it->kind,player2);
        }
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

/* Is there something to stand on directly under the span x0..x1?  Both stage
   terrain and a resting crate count, so a walker will step onto a crate that
   has been set down at the end of its ledge rather than turn away from it. */
static int enemy_floor_under_depth(int x0,int x1,int bottom,int depth)
{
    int i,d;
    /* Terrain sits on an eight-pixel grid, so probing every eighth pixel
       cannot step over a tile row -- three reads cover the whole drop. */
    for(d=1;d<=depth;d+=8)
        if(solid_v(x0,bottom+d)||solid_v(x1,bottom+d))return 1;
    for(i=0;i<MAX_CRATES;++i){
        const Crate *c=&g_crates[i];
        int cx,cy;
        if(!c->active||c->carried||c->thrown)continue;
        cx=(int)(c->x>>FIX_SHIFT);cy=(int)(c->y>>FIX_SHIFT);
        if(x1<cx||x0>=cx+CRATE_W)continue;
        if(bottom+depth>=cy&&bottom+1<cy+CRATE_H)return 1;
    }
    return 0;
}

static int enemy_floor_under(int x0,int x1,int bottom)
{
    return enemy_floor_under_depth(x0,x1,bottom,1);
}

/* How far below its feet a walker will still call ground "ground".  A step
   down out of a ledge is part of the terrain and the source walks it; only an
   actual pit turns one round, and every pit in these stages runs to the
   bottom of the storey.  Probing a single pixel made the walkers refuse to
   follow the ground down a one-tile step and pace the little plateau they
   were left on instead. */
#define ENEMY_LEDGE_DROP 20

/* Which walkers refuse to step off the end of what they are standing on.  The
   hopper is out because its $BA7A leap is exactly how it changes platform, and
   the boulder is out because rolling off the edge is its whole behaviour --
   both of those drive themselves and never reach the shared solver anyway, so
   this only has to name the families that do. */
static int enemy_turns_at_ledge(const Enemy *e)
{
    return e->kind!=ENEMY_KIND_HOPPER && e->kind!=ENEMY_KIND_BOULDER;
}

/* One step of the mimic's reveal.  $C8B2 and $C8F0 each subtract eight from
   the object's Y, and its metasprite grows by exactly those eight pixels --
   24 tall closed, 32 half open, 40 walking -- so all three poses stand on one
   line.  Carrying the growth in the record keeps the terrain solver honest:
   handing the crate the walking height instead made it sink straight through
   the ledge it was authored on and settle somewhere under the stage. */
static void mimic_open(Enemy *e)
{
    e->y-=(s32)8*FIX_ONE;
    e->height=(unsigned char)(e->height+8);
    ++e->behavior_phase;
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
        int was_grounded;

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

        /* State 10, ROM $BE6C.  Two families share the fall-through kind, and
           this is the one that has no business on the ground: the graveyard's
           green ghost.  It was reaching the generic Goomba solver, which gave
           it gravity and walls it does not have in the source and pinned it to
           whatever ledge it was authored over.  Drive it from its own table
           instead: one pair every eighth frame, then a nudge toward Mario. */
        if(e->kind==ENEMY_KIND_GENERIC && e->state==10){
            e->vx=e->vy=0;
            if((g_state_frame&7)==0){
                const signed char *p=&g_ghost_path[e->flight_phase*2];
                e->x+=(s32)p[0]*FIX_ONE;
                e->y+=(s32)p[1]*FIX_ONE;
                e->face_right=(unsigned char)(p[0]>0);
                if(++e->flight_phase>=GHOST_PATH_STEPS){
                    /* $BEB4/$BECE close the distance eight pixels at a time so
                       the drift always circles back onto the player. */
                    e->flight_phase=0;
                    x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
                    e->x+=(s32)((x<px)?8:-8)*FIX_ONE;
                    e->y+=(s32)((y<py)?8:-8)*FIX_ONE;
                }
            }
            x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
            goto enemy_attack_checks;
        }

        /* State 3, ROM $C9B0.  The other family sharing the fall-through kind:
           the ball thrower.  $CA72 walks it a pixel a frame and flips it every
           $3F.  The ROM retries almost immediately after a ball clears; the
           port deliberately leaves a short patrol gap so the golfer does not
           lock into a continuous attack.  $CA20 puts metasprite $1D1 in its
           child slot eighteen pixels down, which $C9D8 then carries two pixels
           a frame in the direction it faces until $130C6 retires it off the
           view. */
        if(e->kind==ENEMY_KIND_GENERIC && e->state==3){
            if(e->behavior_phase){
                e->vx=0;
                /* Family 2's state-1 script is selector 3 for seven ticks,
                   followed by selector 4 until the ball slot clears.  Keep a
                   local script clock so the renderer can use those actual
                   golf-swing cels instead of substituting walk selector 1. */
                if(e->behavior_timer<255)++e->behavior_timer;
                if(!e->shot_slot||!g_enemy_shots[e->shot_slot-1].active){
                    e->shot_slot=0;e->behavior_phase=0;e->behavior_timer=0;
                    e->vx=e->face_right?FIX_ONE:-FIX_ONE;
                }
                y=(int)(e->y>>FIX_SHIFT);
                goto enemy_attack_checks;
            }
            e->face_right=(unsigned char)(e->vx>0);
            if(e->behavior_timer<GOLFER_RETRY_TICKS)++e->behavior_timer;
            if(e->behavior_timer>=GOLFER_RETRY_TICKS &&
               (y>=(int)(g_player_y>>FIX_SHIFT) ||
                (multiplayer_active()&&!g_luigi.out&&
                 y>=(int)(g_luigi.y>>FIX_SHIFT)))){
                e->behavior_phase=1;e->behavior_timer=0;e->vx=0;
                e->shot_slot=(unsigned char)
                    (throw_enemy_shot(e,18,0,BALL_THROW_SPEED,SHOT_MODE_SPEAR)+1);
                y=(int)(e->y>>FIX_SHIFT);
                goto enemy_attack_checks;
            }
            /* Idle deliberately falls through to the shared solver so the walk
               still lands on terrain and turns at walls and ledges. */
        }

        if(e->kind!=ENEMY_KIND_GENERIC){
            int near=iabs_i(px-x);
            if(e->kind==ENEMY_KIND_CHIP){
                /* State 1, ROM $C748.  The flier is not a Koopa on a loop: it
                   runs the shared $C832 path exactly once -- 58 steps of one
                   pixel left and the table's dy -- and $C828 then counts the
                   traversal, which stops it dead.  $C7A0 puts a bomb in its
                   child slot four pixels right and sixteen below its origin,
                   holds animation state 1 while $C766 walks that bomb two
                   pixels a frame straight down, and only when $130C6 has
                   retired it off the bottom does $C784 clear the state and let
                   the patrol start over.  The port used to hand the record to
                   the flying-Koopa patrol, so it drifted across the stage for
                   ever and never dropped anything. */
                e->vx=e->vy=0;
                if(e->behavior_phase==0){
                    e->x-=FIX_ONE;
                    e->y+=(s32)g_flying_koopa_dy[e->flight_phase]*FIX_ONE;
                    if(++e->flight_phase>=58){e->flight_phase=0;e->behavior_phase=1;}
                }else if(e->behavior_phase==1){
                    /* $C7B8 offsets the bomb to the right whichever way the
                       flier is travelling, so place it after the throw rather
                       than through the facing-aware offset. */
                    int k=throw_enemy_shot(e,16,0,0,SHOT_MODE_DROP);
                    if(k>=0){
                        g_enemy_shots[k].x=e->x+(s32)4*FIX_ONE;
                        g_enemy_shots[k].base=CHIP_BOMB_BASE_WORD;
                        g_enemy_shots[k].cols=1;
                        e->shot_slot=(unsigned char)(k+1);
                        e->behavior_phase=2;e->behavior_timer=0;
                    }
                }else{
                    if(e->behavior_timer<255)++e->behavior_timer;
                    if(!e->shot_slot||!g_enemy_shots[e->shot_slot-1].active){
                        e->shot_slot=0;e->behavior_phase=0;e->behavior_timer=0;
                    }
                }
                x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
                goto enemy_attack_checks;
            }
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
                if(e->behavior_phase==0)mimic_open(e);
                e->behavior_timer=0;e->vx=0;
#else
                if(e->behavior_phase==0){
                    e->vx=0;
                    if(near<=60){mimic_open(e);e->behavior_timer=0;}
                }
                else if(e->behavior_phase==1&&++e->behavior_timer>=24){mimic_open(e);e->behavior_timer=0;e->vx=-FIX_ONE;}
#endif
                y=(int)(e->y>>FIX_SHIFT);h=e->height;
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
            }else if(e->kind==ENEMY_KIND_SPARKER){
                /* State 11, ROM $BCC8.  The duck holds its ground and does
                   nothing at all until $BD54 finds the player at or past its
                   own X minus $3C; then it turns to face them, switches to
                   animation state 3 and $BE26 puts a spark in its child slot
                   eighteen pixels below its origin.  $BDDA/$BDE2 crawl that
                   spark two pixels a frame in the direction it is facing and
                   $130C6 retires it off the edge of the view, which is what
                   returns the duck to its idle phase.  The port used to hand
                   this record to the generic solver, so the duck walked off
                   like a Goomba and never spat anything. */
                e->vx=e->vy=0;
                if(e->behavior_phase==0){
                    if(px>=x-SPARK_TRIGGER_DX){
                        e->behavior_phase=1;e->behavior_timer=0;
                        e->face_right=(unsigned char)(px>=x);
                        e->shot_slot=(unsigned char)(throw_enemy_shot(e,24,0,2,SHOT_MODE_SPEAR)+1);
                    }
                }else{
                    if(e->behavior_timer<255)++e->behavior_timer;
                    if(!e->shot_slot||!g_enemy_shots[e->shot_slot-1].active){
                        e->shot_slot=0;e->behavior_phase=0;e->behavior_timer=0;
                    }
                }
                y=(int)(e->y>>FIX_SHIFT);goto enemy_attack_checks;
            }else if(e->kind==ENEMY_KIND_HOPPER){
                /* State 14, ROM $B98C.  It walks a pixel a frame for $3F
                   frames, then spends the whole of $BA7A hopping -- still
                   moving that pixel a frame sideways, and with no terrain
                   probe of any kind -- and finally stands for another $3F on
                   animation state 1 before walking again.  Only the walking
                   third of that was ported, which is why it never left the
                   ground.

                   The source picks a fresh random direction for each leg out
                   of $C00008; here the leg keeps whichever way the terrain
                   solver has the walk pointed, so a hop cannot launch the
                   bird into the wall it just turned away from. */
                if(e->behavior_phase==1){
                    e->y+=(s32)g_boulder_bounce1[e->behavior_timer]*FIX_ONE;
                    e->x+=e->face_right?FIX_ONE:-FIX_ONE;
                    if(++e->behavior_timer>=HOPPER_JUMP_STEPS){
                        e->behavior_phase=2;e->behavior_timer=0;
                    }
                    e->vx=e->vy=0;
                    x=(int)(e->x>>FIX_SHIFT);y=(int)(e->y>>FIX_SHIFT);
                    goto enemy_attack_checks;
                }
                if(e->behavior_phase==0){
                    e->face_right=(unsigned char)(e->vx>0);
                    if(++e->behavior_timer>=63){
                        e->behavior_phase=1;e->behavior_timer=0;
                    }
                }else{
                    e->vx=0;
                    if(++e->behavior_timer>=63){
                        e->behavior_phase=0;e->behavior_timer=0;
                        e->vx=e->face_right?FIX_ONE:-FIX_ONE;
                    }
                }
                /* Phases 0 and 2 deliberately fall through to the generic
                   solver so the walk still lands on terrain and turns at
                   walls the way every other ground enemy in the port does. */
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

        /* Grab the standing flag before gravity is applied.  The vertical
           solver zeroes vy on landing, so vy==0 here means "was on a floor
           last frame"; testing it after the += below made the ledge probe
           further down unreachable and let every walker stroll off its
           authored ledge. */
        was_grounded=(e->vy==0);
        e->vy+=64;
        if (e->vy>4*FIX_ONE) e->vy=4*FIX_ONE;

        /* Test horizontal collision before committing the step so reversing
           at a wall does not visibly snap one pixel back and forth. */
        {
            s32 nx=e->x+e->vx;
            int nxi=(int)(nx>>FIX_SHIFT);
            int side=e->vx>0?nxi+e->width-1:nxi;
            int lead=e->vx>0?nxi+e->width-4:nxi+3;
            int bottom;
            y=(int)(e->y>>FIX_SHIFT);
            bottom=y+h-1;
            if (solid_h(side,y+8)||solid_h(side,y+h-3) ||
                enemy_crate_blocked(e,nx,y))
                e->vx=-e->vx;
            else if (enemy_turns_at_ledge(e) && was_grounded && e->vx &&
                     !e->ledge_pinned &&
                     enemy_floor_under(x+3,x+e->width-4,bottom) &&
                     !enemy_floor_under_depth(lead,lead,bottom,
                                              ENEMY_LEDGE_DROP))
                /* A walker that is already standing on something will not step
                   off the end of it.  The source keeps these enemies on their
                   authored ledge by giving them fixed-length legs and no
                   terrain probe at all; giving the port's solver a probe ahead
                   of the leading edge reaches the same place without having to
                   re-time every patrol.  The hopper is the deliberate
                   exception -- its $BA7A leap crosses gaps, and it runs that
                   leap outside this solver entirely.

                   A turn here does not move the walker, so two of them in a
                   row is a deadlock: on a ledge shorter than its own body, or
                   pinned between a wall and a drop, it stood shivering on the
                   spot forever.  One stationary ledge turn is all it gets --
                   after that it walks off the end as it did before the probe
                   existed, which is the one outcome that cannot lock up. */
                { e->vx=-e->vx; e->ledge_pinned=1; }
            else
                { e->x=nx; e->ledge_pinned=0; }
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
        if (!enemy_indestructible(e)) {
            int c;
            for (c=0;c<MAX_CRATES;++c) {
                int cx,cy;
                Crate *cr=&g_crates[c];
                if (!cr->active || cr->carried || !cr->thrown) continue;
                cx=(int)(cr->x>>FIX_SHIFT); cy=(int)(cr->y>>FIX_SHIFT);
                if (aabb(cx+3,cy+4,CRATE_W-6,CRATE_H-8,x+3,y+3,e->width-6,h-5)) {
                    defeat_enemy(e,0);
                    cr->active=0;
                    cr->thrown=0;
                    break;
                }
            }
        }
        if (e->defeated) continue;

        /* Jump-over bonus: while airborne over a living enemy, a player who
           clears its whole body horizontally earns the chime -- once per pass,
           and only for obstacle-free jumps, never for a landing stomp, which
           defeats the enemy before this check can sound it.  It runs on the
           second ADPCM voice so it never has to wait for (or cut) the main
           channel's sample, which is what delayed it behind the jump itself. */
        {
            int player_bottom = py + PLAYER_BODY_H - 1;
            int over = px + PLAYER_BODY_W > x + 2 && px < x + e->width - 2;
            int above = player_bottom < y && !g_grounded;
            if (e->jumped_over == 1) {
                if (!over) {
                    e->jumped_over = 2;
                } else if (!above) {
                    e->jumped_over = 0;
                }
            } else if (e->jumped_over == 2 && !over) {
                /* The pass is fully over; re-arm so a jump back across the
                   same enemy can earn the chime again. */
                e->jumped_over = 0;
            } else if (e->jumped_over == 0 && over && above &&
                       !enemy_indestructible(e)) {
                e->jumped_over = 1;
            }
        }

        /* Use separate stomp and hurt volumes. The visible sprites are much
           wider than their gameplay bodies in the MD code; r6's single broad
           rectangle made side contacts feel unfair. */
        {
            int old_bottom=old_py+PLAYER_BODY_H-1;
            int new_bottom=py+PLAYER_BODY_H-1;
            int stomp_x=aabb(px+3,py+PLAYER_BODY_H-6,10,8,x+2,y-2,e->width-4,8);
            if (g_vy>0 && stomp_x && old_bottom<=y+5 && new_bottom>=y &&
                !enemy_indestructible(e)) {
                defeat_enemy(e,1);
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
        }else if(s->mode==SHOT_MODE_DROP){
            s->y+=2*FIX_ONE;
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

/* Slot 13's bank, in the order tools/build_full_game.py renders it.  Frames
   0..3 are the driver alone, facing left, with $7124's direction-0 offset
   from the vehicle already baked in; the renderer mirrors them to face right.
   The rearing and hit metasprites ($28E/$294 and $290/$296) draw the vehicle
   themselves and the vehicle never mirrors, so those keep one stored pose per
   direction.  Frames 8/9 are the vehicle's two tread cels. */
#define BOSS13_FRAME_REST  0
#define BOSS13_FRAME_LEAN  1
#define BOSS13_FRAME_BOW   2
#define BOSS13_FRAME_HOLD  3
#define BOSS13_FRAME_FIRE  4   /* +1 facing right */
#define BOSS13_FRAME_HIT   6   /* +1 facing right */
#define BOSS13_FRAME_TREAD 8   /* +1 on the alternate tread cel */

/* True while the stored pose already carries the vehicle and its direction,
   so the renderer must neither mirror it nor draw the vehicle underneath. */
static int boss13_frame_is_whole(int frame)
{
    return frame>=BOSS13_FRAME_FIRE && frame<BOSS13_FRAME_TREAD;
}

/* The driver's cel: $8D3C parks it at 0 for every movement state, the $922A
   cel scripts step it through 1/2/5 while the boss turns, both attacks set 6
   at $90D0 and $9184, and $1633A/$163C0 select 3 and 4 for hit and death. */
static int boss13_driver_frame(void)
{
    int right=(g_boss_face<0)?1:0;
    if(g_boss_invuln>0||g_boss_defeated)return BOSS13_FRAME_HIT+right;
    if(g_boss_cel==6)return BOSS13_FRAME_FIRE+right;
    if(g_boss_cel==1)return BOSS13_FRAME_LEAN;
    if(g_boss_cel==2)return BOSS13_FRAME_BOW;
    if(g_boss_cel==5)return BOSS13_FRAME_HOLD;
    return BOSS13_FRAME_REST;
}

/* $7124, indexed by graphics bank then direction: where $6F3A puts the driver
   relative to the vehicle.  Frames 0..3 are banks 1..4 in that order. */
static const signed char g_boss13_driver_dx[2][4]={{-16,-24,8,-16},{16,24,16,24}};

/* The driver's X offset from the vehicle for the pose being drawn.  $7040 and
   $705C anchor the rearing and hit poses on the vehicle itself instead. */
static int boss13_driver_dx(void)
{
    int frame=boss13_driver_frame();
    if(frame>=BOSS13_FRAME_FIRE)return 0;
    return g_boss13_driver_dx[(g_boss_face<0)?1:0][frame];
}

/* The two directions are not mirror images for the bowing and holding poses.
   Mirroring a pose stored at its direction-0 offset lands it at the negated
   offset, so facing right needs the sum of the two as a correction. */
static int boss13_driver_flip_dx(int frame)
{
    return (frame<4)?(g_boss13_driver_dx[0][frame]+g_boss13_driver_dx[1][frame]):0;
}

/* Slot 19's cel byte to strip pose, from the level's cel table at $7400. */
static const unsigned char BOSS19_CEL_FRAME[6]={0,1,2,4,4,3};

/* $15C9C and $162FE read the body straight out of $18BAC for whichever
   metasprite is drawn, and the elephant's five differ far too much for one
   rectangle to stand in for them all: around the actor $270 is -48..55 by
   -96..-1, $272 -56..55 by -96..31, $274 -56..55 by -104..-1, $276 -48..39
   by -96..31 and $278 -56..47 by -80..55.  The port draws all five bottom
   aligned in one 112x144 canvas with the actor at (56,144), which turns
   those into the canvas rectangles below -- and every one of them ends at
   the canvas floor, exactly as the source's boxes all end at the actor once
   the pose's own storey offset is taken back out.  The old 96x80 at (8,8)
   sat in the empty top of the canvas, sixty pixels clear of the elephant,
   so a thrown mallet could only connect from a jump. */
static const unsigned char g_boss19_box[5][4]={
    { 8, 48,104, 96},   /* $270 */
    { 0, 16,112,128},   /* $272 */
    { 0, 40,112,104},   /* $274 */
    { 8, 16, 88,128},   /* $276 */
    { 0,  8,104,136},   /* $278 */
};

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
    /* The three later handlers choose their own pose: $8D3C and its
       neighbours write the source object's +$B every time an action starts
       or a cel script steps, and only the idle cycle is left to the timer.
       Zero is the resting pose for all three, so a non-zero cel is a real
       selection and is used as the frame directly. */
    case 3: /* tank: the driver's pose is the object's cel byte, resolved
               through the per-level cel table at $716E. */
        frame=boss13_driver_frame();
        break;
    case 4: /* mage */
        /* $73B8's cel list is (1,$25C),(2,$25E),(3,$260),(4,$262),(4,$262):
           cels 0..2 are the idle/cast cycle and both cel 3 (the hit pose
           $1633A selects) and cel 4 (the death pose $163C0 selects) name the
           same metasprite.  The supplied strip splits those two apart -- pose
           3 is the recoil, pose 4 is the same recoil dissolving into sparks --
           so being hit must stop at the recoil and only the death may take the
           last pose.  Using frame_count-1 for both played the vanish every
           time the mage was touched. */
        if(g_boss_defeated)frame=v->frame_count-1;
        else if(g_boss_invuln>0)frame=v->frame_count-2;
        else if(g_boss_cel)frame=g_boss_cel;
        else frame=(t/8)%3;
        break;
    default: /* elephant */
        /* $7400's cel list is not in strip order: cel 3 and cel 4 both name
           metasprite $278 (trunk raised, spitting), cel 5 names $276 (reared
           up), and only cels 0..2 line up with the strip.  Resolve it rather
           than using the cel as an index, which showed the resting pose for
           the whole of the $965E spout and the reared pose for being hit. */
        if(g_boss_defeated)frame=BOSS19_CEL_FRAME[4];
        else if(g_boss_invuln>0)frame=BOSS19_CEL_FRAME[3];
        else if(g_boss_cel)frame=BOSS19_CEL_FRAME[g_boss_cel%6];
        /* $741C is one entry, ($1,$270) held for $0FFF frames, and $9418
           never touches the cel byte while it counts the idle out.  The
           elephant is genuinely still between moves; cycling it against
           $272 was the port's own invention. */
        else frame=BOSS19_CEL_FRAME[0];
        break;
    }
    if(frame<0)frame=0;
    return frame%v->frame_count;
}

/* Emit one boss pose's occupied cells, optionally mirrored about the pose's
   own centre, and return the new SATB cursor. */
static int emit_boss_cells(u16 *satb,int n,const BossVisualData *v,int frame,
                           int bx,int by,int mirror)
{
    unsigned long long mask=v->sprite_masks[frame];
    int frame_words=v->cols*v->rows*64,cx,cy;
    for(cy=0;cy<v->rows && n<GAME_SPRITE_CAPACITY-2;++cy)for(cx=0;cx<v->cols && n<GAME_SPRITE_CAPACITY-2;++cx){
        int cell=cy*v->cols+cx,dx,palette;
        if(!(mask&(1ULL<<cell)))continue;
        dx=mirror?(v->cols-1-cx)*16:cx*16;
        palette=v->sprite_cell_palettes[frame*v->cols*v->rows+cell];
        set_sprite_entry(satb,n++,bx+dx,by+cy*16,
            (u16)(BOSS_BASE_WORD+frame*frame_words+cell*64),
            (u16)(VDC_SPR_PRIO_SP|(palette?BOSS_SPR_PAL_AUX:BOSS_SPR_PAL)|
                  (mirror?VDC_SPR_X_INVERT:0)));
    }
    return n;
}

/* One entry per loose crate, up to the caller's SATB ceiling. */
static int emit_crate_sprites(u16 *satb,int n,int limit,int cam_x,int cam_y)
{
    int i;
    for (i = 0; i < MAX_CRATES && n < limit; ++i) {
        int sx, sy;
        if (!g_crates[i].active || g_crates[i].carried) continue;
        sx = (int)(g_crates[i].x >> FIX_SHIFT) - cam_x;
        sy = (int)(g_crates[i].y >> FIX_SHIFT) - cam_y + bowser_room_object_y_offset();
        if (sx < -32 || sx > 255 || sy < -32 || sy > 239) continue;
        {u16 addr=CRATE_BASE_WORD,pal=(u16)(g_in_boss?MARIO_BOSS_SPR_PAL:0); int width=2,height=2;
        if(g_in_boss&&g_boss_class==1){
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
    return n;
}

/* Blit one 16x16 VDC sprite cell into the HUD strip.  The strip's origin is
   the HUD's own first line, so y is 0 for the icon row and 8 for the digit
   row.  Colour 0 is transparent exactly as it is for a sprite, which is what
   lets the life icon's 32-pixel cell pair carry only 24 pixels of art without
   erasing the lives digit that starts inside its right-hand cell. */
static void hud_king_blit(int x,int y,const unsigned short *cell)
{
    int row,col;
    for(row=0;row<16;++row){
        int sy=y+row;
        u16 p0,p1,p2,p3;
        if(sy<0||sy>=HUD_KING_ROWS*8)continue;
        p0=cell[row];p1=cell[16+row];p2=cell[32+row];p3=cell[48+row];
        if(!(p0|p1|p2|p3))continue;
        for(col=0;col<16;++col){
            int bit=15-col,sx=x+col,idx,w,sh;
            idx=(int)((p0>>bit)&1)|(int)(((p1>>bit)&1)<<1)|
                (int)(((p2>>bit)&1)<<2)|(int)(((p3>>bit)&1)<<3);
            if(!idx||sx<0||sx>=HUD_KING_COLS*8)continue;
            w=((sy>>3)*HUD_KING_COLS+(sx>>3))*HUD_KING_CELL_WORDS
              +(sy&7)*2+((sx&7)>>2);
            sh=(3-(sx&3))*4;
            g_hud_cg[w]=(u16)((g_hud_cg[w]&~(0xF<<sh))|(idx<<sh));
        }
    }
}

/* The heart art is one 8x8 glyph in the corner of a 16x16 cell, so the strip
   places one per eight pixels instead of paying for the sprite path's
   pre-doubled 2- and 4-heart cells. */
static void hud_king_hearts(int x,int count)
{
    int i;
    if(count>PLAYER_MAX_HEARTS)count=PLAYER_MAX_HEARTS;
    for(i=0;i<count;++i)hud_king_blit(x+i*8,8,g_hud_heart_words);
}

/* Player one owns the columns left of player two's life icon.  Nothing in
   the layout straddles that boundary, so a whole cell can take one palette
   line and the two HUDs keep the colours they had as sprites. */
static void hud_king_write_bat(void)
{
    u16 one=(u16)((g_in_boss?MARIO_BOSS_SPR_PAL:0)<<12);
    u16 two=(u16)(LUIGI_SPR_PAL<<12);
    int row,col;
    /* Both rows are one contiguous run of BAT words, and cell (row,col)
       always holds tile 1+row*32+col, so the BAT is only rewritten to change
       a palette line. */
    king_set_kram_write(HUD_KING_BAT_ADDR*1024+HUD_KING_BAT_ROW*HUD_KING_COLS,1);
    for(row=0;row<HUD_KING_ROWS;++row)
        for(col=0;col<HUD_KING_COLS;++col)
            king_kram_write((u16)((col<HUD_KING_LUIGI_COL?one:two)|
                                  (1+row*HUD_KING_COLS+col)));
}

/* Rebuild and upload the layer, but only when something it shows has moved:
   the strip is a kilobyte of KRAM and the values behind it change a handful
   of times per stage. */
static void hud_king_refresh(void)
{
    int lives=g_lives,ammo=g_fireball_ammo,i;
    u32 sig;
    if(lives<0)lives=0; if(lives>9)lives=9;
    if(ammo<0)ammo=0; if(ammo>9)ammo=9;
    sig=(u32)1|((u32)lives<<1)|((u32)g_player_hearts<<5)|((u32)ammo<<9);
    if(multiplayer_active())
        sig|=((u32)1<<13)|((u32)(g_luigi.lives&15)<<14)|
             ((u32)(g_luigi.hearts&7)<<18)|((u32)(g_luigi.fireball_ammo&15)<<21);
    if(g_in_boss)sig|=(u32)1<<25;
    if(sig==g_hud_king_sig)return;
    g_hud_king_sig=sig;

    for(i=0;i<HUD_KING_CELLS*HUD_KING_CELL_WORDS;++i)g_hud_cg[i]=0;
    /* The same layout emit_game_sprites() uses, minus the 209/217 origin the
       strip already carries. */
    hud_king_blit(8,0,g_hud_life_words);
    hud_king_blit(24,0,g_hud_life_words+64);
    hud_king_blit(80,0,g_hud_fire_words);
    hud_king_blit(32,8,g_hud_digit_words+lives*64);
    hud_king_hearts(40,g_player_hearts);
    hud_king_blit(96,8,g_hud_digit_words+ammo*64);
    if(multiplayer_active()){
        int llives=g_luigi.lives,lammo=g_luigi.fireball_ammo;
        if(llives<0)llives=0; if(llives>9)llives=9;
        if(lammo<0)lammo=0; if(lammo>9)lammo=9;
        hud_king_blit(120,0,g_hud_life_words);
        hud_king_blit(136,0,g_hud_life_words+64);
        hud_king_blit(144,8,g_hud_digit_words+llives*64);
        hud_king_hearts(152,g_luigi.hearts);
        hud_king_blit(192,0,g_hud_fire_words);
        hud_king_blit(208,8,g_hud_digit_words+lammo*64);
    }

    king_set_kram_write(HUD_KING_CG_ADDR*1024+HUD_KING_CELL_WORDS,1);
    for(i=0;i<HUD_KING_CELLS*HUD_KING_CELL_WORDS;++i)king_kram_write(g_hud_cg[i]);
    hud_king_write_bat();
}

static void emit_hud_hearts(u16 *satb,int *n,int x,int y,int count,u16 palette)
{
    u16 ctrl=(u16)(VDC_SPR_PRIO_SP|palette);
    if(count>PLAYER_MAX_HEARTS)count=PLAYER_MAX_HEARTS;
    if(count>=4&&*n<GAME_SPRITE_CAPACITY){
        set_sprite_entry(satb,(*n)++,x,y,HUD_HEART_STRIP_BASE_WORD,
                         (u16)(ctrl|VDC_SPR_X_WIDTH_2));
        x+=32;count-=4;
    }
    if(count>=2&&*n<GAME_SPRITE_CAPACITY){
        set_sprite_entry(satb,(*n)++,x,y,HUD_HEART_STRIP_BASE_WORD,ctrl);
        x+=16;count-=2;
    }
    if(count&&*n<GAME_SPRITE_CAPACITY)
        set_sprite_entry(satb,(*n)++,x,y,
                         (u16)(HUD_HEART_STRIP_BASE_WORD+128),ctrl);
}

static void draw_game_sprites(void)
{
    static u16 satb[GAME_SPRITE_CAPACITY * 4];
    static unsigned char group_len[GAME_SPRITE_CAPACITY];
    int i, n = 0;
    int frame = current_player_frame();
    /* The HuC6270 fetches the SATB list at the VDW->VCR transition, ahead of
       the CPU's vblank service that written it, so a list is displayed one
       whole frame after the blanking that produced it, while the scroll
       register written in that same blanking is already live.  World sprites
       therefore have to be positioned against the camera that will be
       committed next frame -- the target update_camera() has just computed --
       or every camera step shows up as a one-frame sprite offset against the
       background that pops back as soon as the scroll pauses, which reads as
       sprite wobble on upward climbs. */
    int cam_x=g_camera_target_x;
    int cam_y=g_camera_target_y;
    int px = (int)(g_player_x >> FIX_SHIFT) - cam_x;
    int py = (int)(g_player_y >> FIX_SHIFT) - cam_y + bowser_room_object_y_offset();
    /* Every normal-level palette carries the original Mario/object colors in
       source line 0 (identical across all 19 level palettes).  r11 moved
       normal sprites to line 13 and produced the observed yellow/green Mario
       regression.  Keep the separate line only for boss rooms, where the
       arena uses lines 0..2. */
    u16 player_pal=(u16)(g_in_boss?MARIO_BOSS_SPR_PAL:0);
    u16 pctrl = (u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_1 | player_pal);
    u16 paddr = (u16)(PLAYER_BASE_WORD + (frame-1) * PLAYER_FRAME_WORDS_LOCAL);
    if (g_player_down && g_grounded) {
        /* Participant count is not character identity: player one remains
           Mario in both menu modes. */
        paddr=GAMEPLAY_DOWN_MARIO_BASE_WORD;
        pctrl=(u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_1 |
                    (g_in_boss?BOSS_CROUCH_SPR_PAL:CONTINUE_PLAYER_SPR_PAL));
        py+=8;
    }
    int player_visible=1;
    int hud_yoff=(g_in_boss||(!g_in_boss&&g_current_level==2))?-16:0;

    for (i = 0; i < GAME_SPRITE_CAPACITY * 4; ++i) satb[i] = 0;
    for (i = 0; i < GAME_SPRITE_CAPACITY; ++i) group_len[i] = 1;

    /* Exact Mega Drive HUD object boundaries, emitted first in SATB so a busy
       room cannot drop them: 24x16 life icon/x, 16x16 fire icon, then live
       digits and individual heart sprites.  Where the cellophane band is up
       the whole HUD lives on KING BG1 instead, above the band rather than
       under it, and none of this is emitted. */
    if(g_overlay_active)hud_king_refresh();
    else{
    set_sprite_entry(satb,n++,8,209+hud_yoff,HUD_LIFE_BASE_WORD,
        (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|player_pal));
    set_sprite_entry(satb,n++,80,209+hud_yoff,HUD_FIRE_BASE_WORD,
        (u16)(VDC_SPR_PRIO_SP|player_pal));
    if (n<GAME_SPRITE_CAPACITY) {
        int lives=g_lives; if (lives<0) lives=0; if (lives>9) lives=9;
        set_sprite_entry(satb,n++,32,217+hud_yoff,(u16)(HUD_DIGIT_BASE_WORD+lives*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
    }
    emit_hud_hearts(satb,&n,40,217+hud_yoff,g_player_hearts,player_pal);
    if (n<GAME_SPRITE_CAPACITY) {
        int digit=g_fireball_ammo;
        if (digit<0) digit=0; if (digit>9) digit=9;
        set_sprite_entry(satb,n++,96,217+hud_yoff,(u16)(HUD_DIGIT_BASE_WORD+digit*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
    }
    if(multiplayer_active() && n<GAME_SPRITE_CAPACITY){
        int lives=g_luigi.lives,digit=g_luigi.fireball_ammo;
        if(lives<0)lives=0;if(lives>9)lives=9;
        if(digit<0)digit=0;if(digit>9)digit=9;
        set_sprite_entry(satb,n++,120,209+hud_yoff,HUD_LIFE_BASE_WORD,
            (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LUIGI_SPR_PAL));
        if(n<GAME_SPRITE_CAPACITY)set_sprite_entry(satb,n++,144,217+hud_yoff,
            (u16)(HUD_DIGIT_BASE_WORD+lives*64),(u16)(VDC_SPR_PRIO_SP|LUIGI_SPR_PAL));
        emit_hud_hearts(satb,&n,152,217+hud_yoff,g_luigi.hearts,LUIGI_SPR_PAL);
        if(n<GAME_SPRITE_CAPACITY)set_sprite_entry(satb,n++,192,209+hud_yoff,
            HUD_FIRE_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|LUIGI_SPR_PAL));
        if(n<GAME_SPRITE_CAPACITY)set_sprite_entry(satb,n++,208,217+hud_yoff,
            (u16)(HUD_DIGIT_BASE_WORD+digit*64),(u16)(VDC_SPR_PRIO_SP|LUIGI_SPR_PAL));
    }
    }
    /* Everything emitted from here on is world content and may be occluded by
       a front-plane scenery layer; the HUD above never is. */
    int hud_entries=n;

    /* The boss body leads the world content so it cannot lose cells to a
       full SATB and so nothing in the room draws over it.  On the Mega
       Drive the sprite order is the object pool's own: the player is slot
       0 and the boss slot 12, so Mario passes in front of it there, but
       both the carriers (slots 38/39) and the room's boxes are behind it,
       which is what the old order got wrong.  */
    if(g_in_boss && (g_boss_hp>0||g_boss_defeated) && n<GAME_SPRITE_CAPACITY-6){
        /* Hurt includes an impact puff four pixels above the body, so its
           capture origin needs +4 relative to the old value to keep Bowser's
           feet aligned with the neighboring walk poses. */
        int bx=(int)(g_boss_x>>FIX_SHIFT),cx,cy;
        int by=(int)(g_boss_y>>FIX_SHIFT)+bowser_room_object_y_offset();
        unsigned long long mask;
        u16 flip=(g_boss_face<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            const BossVisualData *v=&g_later_boss_visuals[g_boss_class-1];
            int frame=later_boss_frame(v);
            /* $9B52 blanks the mage's metasprite for the middle of its
               teleport and then blinks it back in. */
            if(g_boss_hidden)frame=-1;
            if(frame>=0&&g_boss_class==3){
                /* $8CEC's boss is two objects: the vehicle at the actor
                   point, whose tread cel $8D72 alternates every eighth
                   frame, and the driver on top of it.  Emitting them
                   separately is what keeps the two welded together whatever
                   pose the driver takes, and it is why the treads move. */
                if(!boss13_frame_is_whole(frame))
                    n=emit_boss_cells(satb,n,v,BOSS13_FRAME_TREAD+((g_state_frame>>3)&1),bx,by,0);
                n=emit_boss_cells(satb,n,v,frame,
                                  bx+((g_boss_face<0&&!boss13_frame_is_whole(frame))?boss13_driver_flip_dx(frame):0),
                                  by,(g_boss_face<0&&!boss13_frame_is_whole(frame)));
            }else if(frame>=0){
                n=emit_boss_cells(satb,n,v,frame,bx,by,g_boss_face<0);
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
        /* Bowser's supplied 112x96 canvases are authored nine pixels below
           the actor anchor the Mega Drive object uses.  The room's own -6 has
           already been applied above, along with every other object in the
           arena; taking a further 15 here double-counted it and left his feet
           six pixels clear of the catwalk Mario stands on. */
        by-=9;
        mask=g_bowser_special_masks[special];
        for(cy=0;cy<6 && n<GAME_SPRITE_CAPACITY-2;++cy)for(cx=0;cx<7 && n<GAME_SPRITE_CAPACITY-2;++cx){int cell=cy*7+cx,dx;if(!(mask&(1ULL<<cell)))continue;dx=(g_boss_face<0)?(6-cx)*16:cx*16;set_sprite_entry(satb,n++,bx+dx,by+cy*16,(u16)(BOWSER_ANIM_BASE_WORD+special*BOWSER_SPECIAL_FRAME_WORDS+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));}
        }
    }


    /* SATB entries with lower indices win sprite/sprite overlap.  A carried
       crate is the one exception to the normal object ordering: it must cover
       Mario's hands/body, while every world crate remains behind Mario. */
    {
      int owner;
      for(owner=0;owner<(multiplayer_active()?2:1)&&n<GAME_SPRITE_CAPACITY;++owner){
        int carry=owner?g_luigi.carry_index:g_carry_index;
        Crate *c;
        if(carry<0||carry>=MAX_CRATES||!g_crates[carry].active||!g_crates[carry].carried)continue;
        c=&g_crates[carry];
        int sx=(int)(c->x>>FIX_SHIFT)-cam_x;
        int sy=(int)(c->y>>FIX_SHIFT)-cam_y+bowser_room_object_y_offset();
        if (sx>=-32 && sx<=255 && sy>=-32 && sy<=239) {
            u16 addr=CRATE_BASE_WORD,pal=owner?LUIGI_SPR_PAL:player_pal;
            int width=2,height=2;
            if(g_in_boss&&g_boss_class==1) {
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
    } else if (g_player_mode!=PMODE_FADE_OUT && g_player_mode!=PMODE_OUT && player_visible) {
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

    if (multiplayer_active() && !g_luigi.out && n<GAME_SPRITE_CAPACITY-3) {
        int lx=(int)(g_luigi.x>>FIX_SHIFT)-cam_x;
        int ly=(int)(g_luigi.y>>FIX_SHIFT)-cam_y+bowser_room_object_y_offset();
        int lf=3;
        u16 la,lc=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|
                        VDC_SPR_Y_HEIGHT_1|LUIGI_SPR_PAL);
        int visible=(g_luigi.invuln<=0)||
                    (((g_luigi.invuln/HURT_FLICKER_HALF_PERIOD)&1)!=0);
        if(g_luigi.crate_hide&&g_luigi.carry_index>=0)visible=0;
        if (g_luigi.mode==PMODE_DYING) {
            lf=((const unsigned char[]){4,5,6,5})[(g_luigi.death_timer/6)&3];
            la=(u16)(PLAYER_BASE_WORD+(lf-1)*PLAYER_FRAME_WORDS_LOCAL);
        } else if (g_luigi.down && g_luigi.grounded) {
            /* The dedicated crouch art has its own Mario/Luigi indexed
               palette, so it must not use the shared standing-frame line. */
            la=GAMEPLAY_DOWN_LUIGI_BASE_WORD;
            lc=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_1|
                     (g_in_boss?BOSS_CROUCH_SPR_PAL:CONTINUE_PLAYER_SPR_PAL));
            ly+=8;
        } else if(g_luigi.throw_timer>0){
            la=THROW_BASE_WORD;
        } else {
            if (!g_luigi.grounded) lf=4;
            else if (g_luigi.vx) lf=((const unsigned char[]){1,2,1,3})[(g_luigi.anim_clock/7)&3];
            la=(u16)(PLAYER_BASE_WORD+(lf-1)*PLAYER_FRAME_WORDS_LOCAL);
        }
        if(g_luigi.facing<0)lc|=VDC_SPR_X_INVERT;
        if(visible || g_luigi.mode==PMODE_DYING || g_luigi.mode==PMODE_REENTRY){
            if(g_luigi.throw_timer>0){
                u16 tc=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_Y_HEIGHT_2|LUIGI_SPR_PAL);
                if(g_luigi.facing>0){
                    set_sprite_entry(satb,n++,lx,ly+16,THROW_BASE_WORD,(u16)(tc|VDC_SPR_X_WIDTH_2));
                    set_sprite_entry(satb,n++,lx+32,ly+16,(u16)(THROW_BASE_WORD+256),tc);
                }else{
                    tc|=VDC_SPR_X_INVERT;
                    set_sprite_entry(satb,n++,lx+16,ly+16,THROW_BASE_WORD,(u16)(tc|VDC_SPR_X_WIDTH_2));
                    set_sprite_entry(satb,n++,lx,ly+16,(u16)(THROW_BASE_WORD+256),tc);
                }
            }else{
                set_sprite_entry(satb,n++,lx,ly,la,lc);
                set_sprite_entry(satb,n++,lx,ly+16,(u16)(la+PLAYER_STRIP_WORDS),lc);
                set_sprite_entry(satb,n++,lx,ly+32,(u16)(la+PLAYER_STRIP_WORDS*2),lc);
            }
        }
    }

    /* Gems use their original 97/98/99 three-frame timing: 15,7,15. */
    {
        int t = g_state_frame % 37;
        int gf = (t < 15) ? 0 : (t < 22 ? 1 : 2);
        for (i = 0; i < MAX_GEMS && n < GAME_SPRITE_CAPACITY-14; ++i) {
            int sx, sy;
            if (!g_gems[i].active) continue;
            sx = g_gems[i].x - cam_x; sy = g_gems[i].y - cam_y;
            if (sx < -16 || sx > 255 || sy < -16 || sy > 239) continue;
            set_sprite_entry(satb,n++,sx,sy,(u16)(GEM_BASE_WORD + gf*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
        }
    }

    /* Freed crate contents.  Metasprites 191..197 are single 16x16 parts on
       the same sprite palette line the crate and gem use. */
    for (i = 0; i < MAX_ITEMS && n < GAME_SPRITE_CAPACITY-10; ++i) {
        int sx, sy;
        if (!g_items[i].active) continue;
        sx = (int)(g_items[i].x >> FIX_SHIFT) - cam_x;
        sy = (int)(g_items[i].y >> FIX_SHIFT) - cam_y;
        if (sx < -16 || sx > 255 || sy < -16 || sy > 239) continue;
        set_sprite_entry(satb,n++,sx,sy,
            (u16)(ITEM_BASE_WORD + (g_items[i].kind-1)*ITEM_CELL_WORDS),
            (u16)(VDC_SPR_PRIO_SP|player_pal));
    }

    /* A boss room keeps the order -- $6BA0's pool draws the boss ahead of
       the room's boxes -- but not the ceiling.  The elephant alone is thirty-
       six to forty-eight cells depending on its pose, and with the HUD and
       Mario ahead of it the cursor stood at fifty-eight on the fatter ones,
       past this loop's old limit of fifty-four: the room's one mallet blinked
       out for as long as the elephant held a wide pose.  Ammunition is not
       decoration here -- with no mallet on the catwalk the fight cannot be
       won -- so in a boss room the loop runs to the edge of the bubbles' own
       reservation instead. */
    n=emit_crate_sprites(satb,n,g_in_boss?GAME_SPRITE_CAPACITY-2:GAME_SPRITE_CAPACITY-10,cam_x,cam_y);

    /* Traversal-critical Level-2 moving platforms get SATB priority over
       enemies so a busy screen cannot make the floor disappear. */
    for (i=0;i<g_moving_platform_count && n<GAME_SPRITE_CAPACITY-6;++i) {
        MovingPlatform *mp=&g_moving_platforms[i];
        int sx,sy,group_start;
        if (mp->hidden) continue;   /* metasprite 0: parked out of the cycle */
        sx=platform_draw_x(mp)-cam_x;
        sy=platform_draw_y(mp)-cam_y+bowser_room_object_y_offset();
        if (sx<-platform_width(mp) || sx>255 || sy<-platform_height(mp) || sy>239) continue;
        group_start=n;
        if(PLATFORM_KIND_IS_STAGE_OBJECT(mp->kind)){
            /* Cells are stored row-major with an even pitch, so each pair is
               128-word aligned and can go out as one 32-pixel-wide sprite. */
            const unsigned short *sh=platform_shape(mp);
            u16 base=(u16)(STAGE_OBJECT_BASE_WORD+sh[0]);
            int cols=sh[1],rows=sh[2],cy2,cx2;
            u16 pal=(u16)((mp->kind==PLATFORM_KIND_FIREBALL)?
                          FIREBALL_SPR_PAL:LEVEL8_OBJ_SPR_PAL);
            for(cy2=0;cy2<rows&&n<GAME_SPRITE_CAPACITY-6;++cy2)for(cx2=0;cx2<cols&&n<GAME_SPRITE_CAPACITY-6;cx2+=2){
                u16 addr=(u16)(base+(cy2*cols+cx2)*64);
                set_sprite_entry(satb,n++,sx+cx2*16,sy+cy2*16,addr,
                    (u16)(VDC_SPR_PRIO_SP|((cols-cx2>=2)?VDC_SPR_X_WIDTH_2:0)|pal));
            }
        }else if(mp->kind==PLATFORM_KIND_BRICK){
            set_sprite_entry(satb,n++,sx,sy,PLATFORM_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
            if(n<GAME_SPRITE_CAPACITY-6)set_sprite_entry(satb,n++,sx+32,sy,(u16)(PLATFORM_BASE_WORD+128),
                (u16)(VDC_SPR_PRIO_SP|LEVEL8_OBJ_SPR_PAL));
        }else if(mp->kind==PLATFORM_KIND_WEDGE_R){
            /* 48x64: four 32x16 strips plus the 16-wide right column. */
            int s;
            for(s=0;s<4&&n<GAME_SPRITE_CAPACITY-6;++s){
                set_sprite_entry(satb,n++,sx,sy+s*16,(u16)(LEVEL8_WEDGE_R_BASE_WORD+s*128),
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
                if(n<GAME_SPRITE_CAPACITY-6)set_sprite_entry(satb,n++,sx+32,sy+s*16,(u16)(LEVEL8_WEDGE_R_RIGHT_WORD+s*64),
                    (u16)(VDC_SPR_PRIO_SP|LEVEL8_OBJ_SPR_PAL));
            }
        }else if(mp->kind==PLATFORM_KIND_WEDGE_L){
            int s;
            for(s=0;s<3&&n<GAME_SPRITE_CAPACITY-6;++s)
                set_sprite_entry(satb,n++,sx,sy+s*16,(u16)(LEVEL8_WEDGE_L_BASE_WORD+s*128),
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|LEVEL8_OBJ_SPR_PAL));
        }else if(g_in_boss&&g_boss_class==3){
            set_sprite_entry(satb,n++,sx,sy,BOSS13_PLATFORM_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS13_PLATFORM_SPR_PAL));
            if(n<GAME_SPRITE_CAPACITY-6)set_sprite_entry(satb,n++,sx+32,sy,(u16)(BOSS13_PLATFORM_BASE_WORD+256),(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS13_PLATFORM_SPR_PAL));
        }else{
            set_sprite_entry(satb,n++,sx,sy,PLATFORM_BASE_WORD,(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2));
            if(n<GAME_SPRITE_CAPACITY-6)set_sprite_entry(satb,n++,sx+32,sy,(u16)(PLATFORM_BASE_WORD+128),VDC_SPR_PRIO_SP);
        }
        group_len[group_start]=(unsigned char)(n-group_start);
    }

    /* Projectiles are emitted ahead of the enemies: a screen full of stage
       objects can exhaust the 64-entry SATB, and dropping the thing that
       hurts the player is worse than dropping one enemy cell. */
    for(i=0;i<4&&n<GAME_SPRITE_CAPACITY-2;++i)if(g_enemy_shots[i].active){
        int sx=(int)(g_enemy_shots[i].x>>FIX_SHIFT)-cam_x,sy=(int)(g_enemy_shots[i].y>>FIX_SHIFT)-cam_y;
        int flip=(g_enemy_shots[i].mode==SHOT_MODE_SPEAR&&g_enemy_shots[i].vx<0);
        int c;
        for(c=0;c<g_enemy_shots[i].cols&&n<GAME_SPRITE_CAPACITY-2;++c){
            int draw_c=flip?g_enemy_shots[i].cols-1-c:c;
            set_sprite_entry(satb,n++,sx+draw_c*16,sy,(u16)(g_enemy_shots[i].base+c*64),
                (u16)(VDC_SPR_PRIO_SP|g_enemy_shots[i].palette|
                      (flip?VDC_SPR_X_INVERT:0)));
        }
    }

    for (i=0;i<MAX_ENEMIES && n<GAME_SPRITE_CAPACITY-6;++i) {
        int sx,sy,anim,li,gi,cx,cy,cols,rows,grid_cols,special_snake,special_monkey,special_crate,special_chip,special_frame,use_hit_frame,use_alt=0,use_atk=0,use_atk2=0; unsigned mask; u16 flip,base;
        Enemy *e=&g_enemies[i]; const EnemyVariantData *v;
        if(!e->active||!e->spawned)continue;
        sx=(int)(e->x>>FIX_SHIFT)-cam_x;sy=(int)(e->y>>FIX_SHIFT)-cam_y;
        if(sx<-56||sx>255||sy<-56||sy>239)continue;
        li=e->variant;if(li>=g_level->variant_count)continue;gi=g_level->variant_ids[li];v=&g_enemy_variants[gi];
        /* mimic_open() keeps the record's box the same height as the pose on
           screen, so every cel is simply bottom-aligned in it.  The variant's
           own poses are packed onto the closed crate's 24-pixel floor and the
           baseline shift below restores it; the separate walking bank is the
           one bank packed at its full 40.  Drawing all of them from the top of
           the box is what left the walking mimic hanging twenty-odd pixels
           above the ledge it was supposed to be crossing. */
        if(e->kind==ENEMY_KIND_CRATE_MIMIC)
            sy+=(int)e->height-((e->behavior_phase==2&&!e->defeated)?40:24);
        anim=(int)((g_state_frame/8)&1);
        /* Frame selector 2 is a valid live wing-flap cel for MATTHEW, so
           keep the defeated state separate from the frame number. */
        use_hit_frame=e->defeated&&v->has_hit_frame;
        if(e->defeated)anim=use_hit_frame?2:1;
        else if(e->kind!=ENEMY_KIND_GENERIC){
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
            /* $BCC8 writes 3 into the animation-state word for the whole of
               the spit and $B98C writes 1 for the whole of the recovery it
               spends standing after a hop; both of those poses land in the
               port's attack bank, which is selector 3 here. */
            /* $D372 is two cels of seven ticks each, so the spit alternates
               them for as long as the spark is in the air. */
            else if(e->kind==ENEMY_KIND_SPARKER)
                anim=e->behavior_phase?(((e->behavior_timer/7)&1)?4:3):
                     (int)((g_state_frame/8)&1);
            else if(e->kind==ENEMY_KIND_HOPPER)
                anim=(e->behavior_phase==2)?3:(int)((g_state_frame/8)&1);
            else if(e->kind==ENEMY_KIND_SPEARMAN)
                anim=e->behavior_phase?
                     ((e->behavior_timer>=THROWER_RELEASE_TICKS&&
                       e->behavior_timer<THROWER_RELEASE_TICKS*2)?4:3):
                     (int)((g_state_frame/8)&1);
        }
        /* Family 2's state-1 script at $D07C is selector 3 for seven ticks,
           then selector 4 for the rest of the throw.  These are the golfer's
           real swing/release poses, not either cel from its walk script. */
        else if(e->kind==ENEMY_KIND_GENERIC&&e->state==3&&e->behavior_phase)
            anim=(e->behavior_timer<7)?3:4;
#ifdef FORCE_ENEMY_HURT
        if(v->has_hit_frame){use_hit_frame=1;anim=2;}
#endif
#ifdef FORCE_SNAKE_FRAME
        if(e->kind==ENEMY_KIND_STRIKE_SNAKE)anim=FORCE_SNAKE_FRAME;
#endif
        /* Only the three live poses live in the 64x40 bank.  A dead snake has
           a real death cel in its variant's hit bank like every other enemy,
           so it has to leave the special path -- drawing selector 2 of the
           strike bank there is how the corpse ended up mid-lunge. */
        special_snake=(e->kind==ENEMY_KIND_STRIKE_SNAKE&&!use_hit_frame);
        special_monkey=(e->kind==ENEMY_KIND_MONKEY&&(e->defeated||e->behavior_phase==3));
        special_crate=(e->kind==ENEMY_KIND_CRATE_MIMIC&&e->behavior_phase==2&&!e->defeated);
        /* $C7AC writes 1 into the flier's animation-state word the moment it
           stops to bomb and holds it there until the bomb is gone, so the whole
           drop plays on the one cel of that script -- the shared bank. */
        special_chip=(e->kind==ENEMY_KIND_CHIP&&e->behavior_phase&&!e->defeated);
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
        else if(special_chip){mask=g_chip_attack_mask;cols=rows=grid_cols=2;}
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
        else if(special_chip)base=CHIP_ATTACK_BASE_WORD;
        else if(use_atk2)base=g_enemy_atk2_vram[li];
        else if(use_atk)base=g_enemy_atk_vram[li];
        else if(use_alt)base=g_enemy_alt_vram[li];
        else if(use_hit_frame)base=g_enemy_hit_vram[li];
        else base=(u16)(g_enemy_vram[li]+(anim?enemy_frame_words(v):0));
        flip=((e->kind==ENEMY_KIND_PTERODACTYL||e->kind==ENEMY_KIND_THROWER||
               e->kind==ENEMY_KIND_SPARKER||e->kind==ENEMY_KIND_HOPPER||
               (e->kind==ENEMY_KIND_SPEARMAN&&e->behavior_phase)||
               (e->kind==ENEMY_KIND_GENERIC&&e->state==3&&e->behavior_phase))
              ?e->face_right:(e->vx>0))?VDC_SPR_X_INVERT:0;
        for(cy=0;cy<rows && n<GAME_SPRITE_CAPACITY-3;++cy)for(cx=0;cx<cols && n<GAME_SPRITE_CAPACITY-3;++cx){
            int cell=cy*grid_cols+cx,dx;
            if(!(mask&(1u<<cell)))continue;dx=flip?(cols-1-cx)*16:cx*16;
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(base+cell*64),(u16)(VDC_SPR_PRIO_SP|flip|g_enemy_palette_line[li]));
        }
    }

    /* The frog's shockwaves and bomb, the mage's ring volley and bell, and
       the elephant's four hammers all live in the same array. */
    if(g_in_boss && g_boss_class>=3){
        for(i=0;i<MAX_BOSS_SHOTS && n<GAME_SPRITE_CAPACITY-1;++i){
            const BossShot *s=&g_boss_shots[i];
            int sx,sy;
            u16 addr,pal,ctrl;
            if(!s->active)continue;
            sx=(int)(s->x>>FIX_SHIFT);sy=(int)(s->y>>FIX_SHIFT)+bowser_room_object_y_offset();
            if(sx<-32||sx>255||sy<-32||sy>239)continue;
            /* $1A3 names tile 1216 -- the bubble -- three tiles square.
               1225 next door is the mallet, which is the room's ammunition
               and is drawn by the crate loop instead. */
            if(s->kind==BOSS_SHOT_BUBBLE){addr=BOSS_RING_BASE_WORD;pal=BOSS_AUX_SPR_PAL;}
            else if(s->kind==BOSS_SHOT_BOMB){addr=(u16)(BOSS_BOMB_BASE_WORD+((g_state_frame>>3)&1)*256);pal=BOSS_ITEM_SPR_PAL;}
            else if(s->kind==BOSS_SHOT_BELL){addr=BOSS_BELL_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;}
            else{addr=BOSS_RING_BASE_WORD;pal=BOSS_AUX_SPR_PAL;}
            ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|pal);
            if(s->vx<0)ctrl|=VDC_SPR_X_INVERT;
            set_sprite_entry(satb,n++,sx,sy,addr,ctrl);
        }
    }

    if(g_in_boss && g_boss_shot_active && g_boss_class<3 && n<GAME_SPRITE_CAPACITY-1){
        int sx=(int)(g_boss_shot_x>>FIX_SHIFT),sy=(int)(g_boss_shot_y>>FIX_SHIFT),cx,cy;
        u16 flip=(g_boss_shot_vx<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            if(g_boss_class==1)set_sprite_entry(satb,n++,sx,sy,BOSS_WATER_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|BOSS_WATER_SPR_PAL));
            else if(g_boss_class==4||g_boss_class==5){
                int count=(g_boss_class==4)?3:4,k;
                for(k=0;k<count&&n<GAME_SPRITE_CAPACITY;++k)set_sprite_entry(satb,n++,sx-(g_boss_shot_vx<0?-k*18:k*18),sy-k*22,BOSS_RING_BASE_WORD,
                    (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|BOSS_AUX_SPR_PAL|flip));
            }else set_sprite_entry(satb,n++,sx,sy,FIREBALL_BASE_WORD,
                (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|MARIO_BOSS_SPR_PAL|flip));
        }else{
        /* The breath object's coordinates precede its visible metasprite by
           (+32,+13) when travelling right; mirror that offset to the left. */
        sx+=(g_boss_shot_vx<0)?-32:32;sy-=3;
        for(cy=0;cy<2&&n<GAME_SPRITE_CAPACITY;++cy)for(cx=0;cx<3&&n<GAME_SPRITE_CAPACITY;++cx){
            int cell=cy*3+cx,dx;if(!(g_bowser_flame_mask&(1u<<cell)))continue;
            dx=(g_boss_shot_vx<0)?(2-cx)*16:cx*16;
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(BOWSER_FLAME_BASE_WORD+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));
        }
        }
    }

    for (i=0;i<MAX_FIREBALLS && n<GAME_SPRITE_CAPACITY;++i) {
        int sx,sy;
        u16 ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|player_pal);
        if (!g_fireballs[i].active) continue;
        sx=(int)(g_fireballs[i].x>>FIX_SHIFT)-cam_x;
        sy=(int)(g_fireballs[i].y>>FIX_SHIFT)-cam_y;
        if (sx<-32 || sx>255 || sy<-32 || sy>239) continue;
        if (g_fireballs[i].vx<0) ctrl|=VDC_SPR_X_INVERT;
        set_sprite_entry(satb,n++,sx,sy,FIREBALL_BASE_WORD,ctrl);
    }

    if (level_front_scenery_over_sprites())
        for (i=hud_entries;i<n;++i) satb[i*4+3]&=(u16)~VDC_SPR_PRIO_SP;

    submit_game_satbs(satb,group_len,n);
}

static void set_continue_palette_fade(int level)
{
    int i;
    for (i=0;i<256;++i)
        pal_set((u16)i,fade_yuv(g_continue_base_asset.palette[i],level));
    for (i=0;i<16;++i)
        pal_set((u16)(CONTINUE_PLAYER_SPR_PAL*16+i),fade_yuv(g_continue_player_palette[i],level));
    /* Large countdown digit uses dedicated sprite palette line 15. */
    for (i=0;i<16;++i)
        pal_set((u16)(240+i),fade_yuv(g_continue_digit_palette[i],level));
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
    overlay_disable();
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
    /* Both boss BATs are 64 columns wide and the rooms are 256 pixels, so
       the second half has to repeat the first: $9596's landing jolt is the one
       thing that ever scrolls a boss room sideways, and a two-pixel jolt to
       the left wraps the window onto columns 32..63.  Leaving those empty
       turned the whole wall into VDC scratch on every other frame of the
       elephant's landing. */
    for(y=0;y<((g_boss_class==5)?64:32);++y){int sy=(g_boss_class==5)?(y%28):y;vdc_set_vram_write(BACK_CHIP,(u16)(y*BG_BAT_W));for(x=0;x<64;++x)vdc_vram_write(BACK_CHIP,relocate_bat(bv?bv->bg_bat[sy*32+(x&31)]:br->bg_bat[sy*32+(x&31)],BOSS_BG_TILE_BASE_INDEX));}
    if(bv){
        vdc_set_vram_write(FRONT_CHIP,FG_TILE_BASE_WORD);
        for(i=0;i<bv->fg_tile_count*TILE_WORDS;++i)vdc_vram_write(FRONT_CHIP,bv->fg_tile_words[i]);
        for(y=0;y<32;++y){vdc_set_vram_write(FRONT_CHIP,(u16)(y*FG_BAT_W));for(x=0;x<64;++x)vdc_vram_write(FRONT_CHIP,relocate_bat(bv->fg_bat[y*32+(x&31)],FG_TILE_BASE_INDEX));}
    }
    else{
        vdc_set_vram_write(FRONT_CHIP,FG_TILE_BASE_WORD);
        for(i=0;i<br->tile_count*TILE_WORDS;++i)vdc_vram_write(FRONT_CHIP,br->tile_words[i]);
        for(y=0;y<32;++y){vdc_set_vram_write(FRONT_CHIP,(u16)(y*FG_BAT_W));for(x=0;x<64;++x)vdc_vram_write(FRONT_CHIP,relocate_bat(br->fg_bat[y*32+(x&31)],FG_TILE_BASE_INDEX));}
    }
    upload_game_sprites();
    if(bv){
        upload_world_sprite_words(BOSS_BASE_WORD,bv->sprite_words,
            (unsigned)(bv->frame_count*bv->cols*bv->rows*64));
    }else{
        /* Every Bowser body pose comes from the canonical bowser/ bank. */
        upload_world_sprite_words(BOWSER_FLAME_BASE_WORD,g_bowser_flame_words,384);
        upload_world_sprite_words(BOWSER_ANIM_BASE_WORD,g_bowser_special_words,
            BOWSER_SPECIAL_FRAME_WORDS*BOWSER_SPECIAL_FRAME_COUNT);
    }
    if(bv){
        upload_world_sprite_words(BOSS_AUX_BASE_WORD,g_final_ring_words,256);
        upload_world_sprite_words((u16)(BOSS_AUX_BASE_WORD+256),g_final_hammer_words,256);
        upload_world_sprite_words((u16)(BOSS_AUX_BASE_WORD+512),g_boss_pot_words,256);
        upload_world_sprite_words((u16)(BOSS_AUX_BASE_WORD+768),g_boss_bell_words,256);
        upload_world_sprite_words((u16)(BOSS_AUX_BASE_WORD+1024),g_boss_bomb_words,512);
        upload_world_sprite_words((u16)(BOSS_AUX_BASE_WORD+1536),g_boss_crate4_words,256);
        if(g_boss_class==3)upload_world_sprite_words(BOSS13_PLATFORM_BASE_WORD,g_boss13_platform_words,512);
        if(g_boss_class==1)upload_world_sprite_words(BOSS_WATER_BASE_WORD,g_boss_water_words,64);
    }
    init_objects();

    /* Special-mode player spawn is the alternate table at ROM $1DC56.
       Like the ordinary spawn table, coordinates are object-space +$80. */
    x=(int)g_boss_alt_spawn[g_current_level*2]-128;
    y=(int)g_boss_alt_spawn[g_current_level*2+1]-128;
    if(x<-32||x>240)x=24;
    if(y<-32||y>208)y=144;
#ifdef FORCE_PLAYER_X
    x=FORCE_PLAYER_X;
#endif
#ifdef FORCE_PLAYER_Y
    y=FORCE_PLAYER_Y;
#endif
    g_player_x=(s32)x<<FIX_SHIFT;g_player_y=(s32)y<<FIX_SHIFT;
    g_vx=g_vy=0;g_grounded=(y>=144);g_facing=1;
    g_player_anim_clock=0;g_jump_active=0;g_bounce_active=0;g_camera_x=0;g_camera_tile=0;g_camera_hold=0;
    if(!g_keep_ammo){g_fireball_ammo=3;g_luigi.fireball_ammo=3;}g_keep_ammo=0;
    g_player_hearts=PLAYER_MAX_HEARTS;g_invuln=0;g_hurt_pending_death=0;g_throw_timer=0;g_death_timer=0;

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
    /* $6D54 places the frog's dragon itself rather than taking the loader's
       record, and the mage and the elephant start on the corner their group
       machine also teleports back to.  All three are actor origins, so they
       go through the same conversion the handlers use. */
    if(g_boss_class>=3){
        static const short origin[3][2]={{288,192},{320,224},{320,288}};
        x=origin[g_boss_class-3][0]-BOSS_ROOM_BIAS-g_boss_origin_dx[g_boss_class-3];
        y=origin[g_boss_class-3][1]-BOSS_ROOM_BIAS-g_boss_origin_dy[g_boss_class-3];
    }
    g_boss_x=(s32)x<<FIX_SHIFT;g_boss_y=(s32)y<<FIX_SHIFT;
    g_boss_ground_y=y;g_boss_vx=-2*FIX_ONE;g_boss_vy=0;
    g_boss_phase=0;g_boss_face=-1;
    g_boss_action=0;g_boss_action_timer=(g_boss_class==1||g_boss_class==2)?-1:0;g_boss_action_dir=0;g_boss_script_index=0;
    g_boss_init=0;g_boss_group=0;g_boss_cel=0;g_boss_anim_step=0;g_boss_anim_div=0;
    g_boss_hidden=0;g_boss_intangible=0;g_screen_shake=0;g_boss16_pick_count=0;
    g_boss19_ammo_delay=0;g_boss19_ammo_slot=2;g_boss19_tick=0;
    for(i=0;i<MAX_BOSS_SHOTS;++i)g_boss_shots[i].active=0;
    g_boss10_floor_y=y;
    g_boss8_crate=-1;

    /* Exact 1P HP table $6D6E: 8,8,10,10,12,12,12 for the seven bosses.
       $6D94 is also exported for the later two-player implementation. */
    g_boss_hp=(int)g_boss_hp_1p[g_current_level];
    if(g_boss_hp<=0)g_boss_hp=8;
    g_boss_timer=0;g_boss_invuln=0;g_boss_defeated=0;g_boss_shot_active=0;g_boss_shot_vy=0;g_boss_drop_rng=0xF2A74DE4u;g_boss_random_drop_timer=72;g_after_boss=0;
    g_player_mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;set_game_palette_fade(fade_in?0:FADE_FULL);
    if(multiplayer_active()&&!g_luigi.out){
        g_luigi.x=g_player_x+24*FIX_ONE;g_luigi.y=g_player_y;
        g_luigi.vx=g_luigi.vy=0;g_luigi.grounded=g_grounded;g_luigi.facing=1;
        g_luigi.anim_clock=0;g_luigi.jump_active=0;g_luigi.jump_index=0;g_luigi.down=0;
        g_luigi.mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;g_luigi.death_timer=0;
        g_luigi.invuln=0;g_luigi.hearts=PLAYER_MAX_HEARTS;
        g_luigi.carry_index=-1;g_luigi.lift_timer=0;g_luigi.throw_timer=0;g_luigi.crate_hide=0;
    }
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

static unsigned boss_random(void)
{
    /* $99DC and $942C read the VDP's HV counter and take the remainder
       against the current group's length; the port keeps a deterministic
       per-room stream instead so recordings stay reproducible.  The seed is
       not arbitrary: with the old one the mage and the elephant both opened
       with a long run of plain walks before their first attack, because the
       stream now chooses actions and no longer only spaces out bomb drops. */
    g_boss_drop_rng^=g_boss_drop_rng<<13;
    g_boss_drop_rng^=g_boss_drop_rng>>17;
    g_boss_drop_rng^=g_boss_drop_rng<<5;
    return g_boss_drop_rng;
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

/* --- Bosses 5-7: actor space and the objects their handlers create ------ */

/* g_boss_x/g_boss_y are the art's top-left corner because that is what the
   renderer wants.  The handlers work in the source's actor space instead. */
static int boss_origin_x(void)
{
    return (int)(g_boss_x>>FIX_SHIFT)+g_boss_origin_dx[g_boss_class-3];
}

static int boss_origin_y(void)
{
    return (int)(g_boss_y>>FIX_SHIFT)+g_boss_origin_dy[g_boss_class-3];
}

static void boss_set_origin(int ox,int oy)
{
    g_boss_x=(s32)(ox-g_boss_origin_dx[g_boss_class-3])<<FIX_SHIFT;
    g_boss_y=(s32)(oy-g_boss_origin_dy[g_boss_class-3])<<FIX_SHIFT;
}

/* The two carriers $7B5C runs.  Their Y is what $8DD4 and $8E88 wait on, and
   the source runs the boss handler ($7454) before the carrier routine
   ($74DA), so the value the boss tests is the one from before this frame's
   step.  The port updates platforms first, so read the remembered value; the
   two waits are exact comparisons and a frame of skew would move where the
   boss meets its carrier by a whole pixel. */
static int boss13_lift_y(int which)
{
    if(which>=g_moving_platform_count)return 0;
    return g_boss13_lift_prev[which];
}

static int boss_shot_of_kind(int kind)
{
    int i;
    for(i=0;i<MAX_BOSS_SHOTS;++i)
        if(g_boss_shots[i].active&&g_boss_shots[i].kind==kind)return i;
    return -1;
}

static void boss_clear_shots(int kind)
{
    int i;
    for(i=0;i<MAX_BOSS_SHOTS;++i)
        if(g_boss_shots[i].kind==kind)g_boss_shots[i].active=0;
}

static void boss_spawn_shot(int kind,int x,int y,s32 vx,s32 vy)
{
    int i;
    for(i=0;i<MAX_BOSS_SHOTS;++i)if(!g_boss_shots[i].active){
        BossShot *s=&g_boss_shots[i];
        s->active=1;s->kind=(unsigned char)kind;s->phase=0;s->timer=0;
        s->x=(s32)x<<FIX_SHIFT;s->y=(s32)y<<FIX_SHIFT;s->vx=vx;s->vy=vy;
        return;
    }
}

/* $91B2 and $9D2E both end the same way: the flying object is copied into the
   room's own slot, where it stops being the boss's and becomes ammunition. */
static int boss_shot_to_crate(int i,int extra)
{
    int k;
    BossShot *s=&g_boss_shots[i];
    for(k=0;k<MAX_CRATES;++k)if(!g_crates[k].active){
        Crate *c=&g_crates[k];
        c->active=1;c->carried=0;c->thrown=0;c->flight_timer=0;
        c->extra=(unsigned char)extra;
        c->x=s->x;c->y=s->y;c->vx=0;c->vy=0;
        s->active=0;
        return 1;
    }
    s->active=0;
    return 0;
}

/* The room's own bomb object, $FF7404: a released bomb, carried or not. */
static int boss13_room_bomb(void)
{
    int i;
    for(i=0;i<MAX_CRATES;++i)
        if(g_crates[i].active&&g_crates[i].extra==3)return 1;
    return 0;
}

static void boss13_release_bomb(int i)
{
    /* $91D0 only creates the room object; unlike the other boss drops it has
       no sound-effect call in the original path. */
    boss_shot_to_crate(i,3);
}

static void boss16_release_bell(int i)
{
    /* $9D4C-$9D50 spawns the room object once $ff74e4 counts the fall out;
       unlike $91D0's bomb landing there is no sound-effect call in that
       path, so the bell should land silently. */
    boss_shot_to_crate(i,4);
}

/* Everything the three handlers put in the air moves here.  Only the frog's
   bomb is walked by its own handler, because $9132 owns the arc table. */
static void update_boss_shots(void)
{
    int i;
    for(i=0;i<MAX_BOSS_SHOTS;++i){
        BossShot *s=&g_boss_shots[i];
        int sx,sy,py;
        if(!s->active)continue;
        if(s->kind==BOSS_SHOT_BUBBLE){
            /* $96CE carries it two pixels sideways a pass while $98CE lifts
               it.  $971A then drops it a pixel a pass and jitters it left or
               right instead -- the aimed drift belongs to the rise alone, so
               the fall must not keep adding it. */
            if(!s->phase){
                s->x+=s->vx;
                if(g_boss19_bubble_arc[s->timer]==0x88){s->phase=1;s->timer=0;}
                else{
                    /* $98CE throws them 144 pixels up from $40 above the
                       actor, so they leave through the top of the room and
                       rain back in a pixel a pass.  Pinning them to the
                       ceiling instead, as the port used to, turned a spout
                       that arcs away and comes back into a row of bubbles
                       sitting on the top edge. */
                    s->y-=(s32)g_boss19_bubble_arc[s->timer]*FIX_ONE;
                    ++s->timer;
                }
            }else{
                s->y+=FIX_ONE;
                s->x+=(boss_random()&1)?2*FIX_ONE:-2*FIX_ONE;
            }
        }else if(s->kind!=BOSS_SHOT_BOMB){
            s->x+=s->vx;s->y+=s->vy;
        }
        sx=(int)(s->x>>FIX_SHIFT);sy=(int)(s->y>>FIX_SHIFT);
        /* Nothing retires a bubble by position: $965E owns all four slots and
           $9742 is the only thing that empties them, so one that wanders off
           the side has to stay in its slot or the spout would refill it. */
        if(s->kind!=BOSS_SHOT_BUBBLE&&(sx<-48||sx>288||sy>248)){s->active=0;continue;}
        py=player_body_y()+bowser_room_object_y_offset();
        /* $9CE6 lowers the bell on a rope to place it, the way $8564 lowers
           the pig's crate -- it is cargo being set down, not a thrown
           weapon, and the source never tests it against the player at all.
           Falling into that shared hit test below (written for the frog's
           thrown bomb) let a descending bell hurt Mario just for standing
           under it, which is boss 5's own behaviour leaking onto boss 6. */
        if(s->kind==BOSS_SHOT_BELL)continue;
        {   /* $15C9C reads the thrower's box out of $18BAC for the object's
               own metasprite and shrinks it eight pixels; the frog's bomb is
               $1A8/$1AB, 32x24, so it tests as 24x16.  The player side is
               $15C0E's 28x40 from its sprite corner. */
            int hit;
            if(g_boss_class==3&&s->kind==BOSS_SHOT_BOMB)
                hit=aabb((int)(g_player_x>>FIX_SHIFT)+4,
                         (int)(g_player_y>>FIX_SHIFT)+bowser_room_object_y_offset()+8,28,40,
                         sx+4,sy+4,24,16);
            else
                hit=aabb(player_body_x()+4,py+5,8,PLAYER_BODY_H-10,sx+4,sy+4,20,20);
            if(g_invuln==0&&hit){
                hurt_player();
                /* $15CD8 marks the player hurt, takes the heart and rings the
                   sound; it never touches the object slot, so a bubble is not
                   spent on the player it rains onto.  Retiring one here let
                   $9666 find the hole and start that slot's whole flight over
                   -- a spout aimed at a standing player refilled itself for
                   ever, and the elephant would not move on to its next move
                   for the best part of a minute.  Only the bubble is exempt:
                   the frog's bomb and the mage's bell were tuned against the
                   port's own behaviour and are left alone. */
                if(s->kind!=BOSS_SHOT_BUBBLE)s->active=0;
            }
        }
    }
}

static void update_boss_motion(void)
{
    int bx=(int)(g_boss_x>>FIX_SHIFT);
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
    case 3: { /* level 13/$8CEC, transcribed from the 68000 handler */
        int ox=boss_origin_x(),oy=boss_origin_y(),state,dir;
        const unsigned char *p;
        g_boss_vx=0;g_boss_vy=0;
        /* $8CEC: with no move running, take the next script pair.  The source
           returns for that frame without moving, so the previous pose is held
           for one frame exactly as it is by the pig and the ostrich. */
        if(!g_boss_init){
            p=&g_boss13_script[g_boss_script_index*2];
            if(p[0]==0x88){g_boss_script_index=0;p=g_boss13_script;}
            g_boss_action=p[0];g_boss_action_dir=p[1];++g_boss_script_index;
            g_boss_init=1;g_boss_action_timer=0;g_boss_phase=0;
            g_boss_anim_step=0;g_boss_anim_div=0;g_boss_cel=0;
            /* The cleaned tank strip is drawn facing left, which is the
               source's direction 0. */
            g_boss_face=g_boss_action_dir?-1:1;
            break;
        }
        state=g_boss_action;dir=g_boss_action_dir;
        switch(state){
        case 1: /* $8DAA */
            --ox;if(ox<=80){ox=80;g_boss_init=0;}
            break;
        case 2: /* $8DCA: leave only once the left carrier is exactly at $B3 */
            /* $8DD4 is a cmpi/bne pair: the carrier has to be *at* room 51,
               not past it.  Testing for "past" let the boss set off whenever
               the carrier happened to be anywhere below that, so it walked
               into the shaft and descended through empty air.  The carrier
               moves a pixel at a time and holds each value for two frames,
               so the equality is always seen. */
            if(!g_boss_phase){if(boss13_lift_y(0)==51)g_boss_phase=1;break;}
            ox-=2;if(ox<=32){ox=32;g_boss_init=0;g_boss_phase=0;}
            break;
        case 3: /* $8E10: half-speed descent */
            /* $8E10 and $8EC0 test the same frame counter bit $7B14 gates
               the carriers on, so the tank tracks the carrier it rides down
               exactly.  Using the boss's own timer here let the two run on
               different parities. */
            if(g_state_frame&1)break;
            ++oy;if(oy>=160){oy=160;g_boss_init=0;}
            break;
        case 4: /* $8E3E */
            ox+=2;if(ox>=80){ox=80;g_boss_init=0;}
            break;
        case 5: /* $8E5E */
            ++ox;if(ox>=160){ox=160;g_boss_init=0;}
            break;
        case 6: /* $8E7E: wait for the right carrier to be exactly at $130 */
            /* $8E94 sets the flag and falls straight into $8E9A's move, so
               unlike state 2 this one steps on the frame the carrier arrives. */
            if(!g_boss_phase){if(boss13_lift_y(1)!=176)break;g_boss_phase=1;}
            ox+=2;if(ox>=224){ox=224;g_boss_init=0;g_boss_phase=0;}
            break;
        case 7: /* $8EC0: half-speed climb */
            if(g_state_frame&1)break;
            --oy;if(oy<=64){oy=64;g_boss_init=0;}
            break;
        case 8: /* $8EEE */
            ox-=2;if(ox<=160){ox=160;g_boss_init=0;}
            break;
        case 11: case 12: case 13: case 14: { /* $8FCA..$9060: cel scripts */
            static const unsigned char *const anims[4]={
                g_boss13_anim11,g_boss13_anim12,g_boss13_anim13,g_boss13_anim14};
            const unsigned char *a=anims[state-11];
            if(++g_boss_anim_div<3)break;
            g_boss_anim_div=0;
            if(a[g_boss_anim_step*2]==0x88){g_boss_anim_step=0;g_boss_init=0;break;}
            g_boss_cel=a[g_boss_anim_step*2];
            g_boss_action_dir=a[g_boss_anim_step*2+1];
            g_boss_face=g_boss_action_dir?-1:1;
            ++g_boss_anim_step;
            break;
        }
        case 15: /* $907E: two shockwaves spread out of the body for 30 frames */
            if(!g_boss_phase){
                g_boss_phase=1;g_boss_action_timer=0;g_boss_cel=6;
                boss_spawn_shot(BOSS_SHOT_RING,ox-40,oy-32,-4*FIX_ONE,0);
                boss_spawn_shot(BOSS_SHOT_RING,ox+32,oy-32, 4*FIX_ONE,0);
            }else if(++g_boss_action_timer>=30){
                boss_clear_shots(BOSS_SHOT_RING);
                g_boss_init=0;g_boss_phase=0;g_boss_cel=0;
            }
            break;
        case 16: /* $9132: lob a bomb along the $9262 arc */
            if(!g_boss_phase){
                /* $9132 opens on "cmpi.w #0,$FF7404 / bne", and $FF7404 is
                   the room's own bomb object, the one $91C6 fills in when
                   the throw lands.  So the boss will not lob another while
                   the last one is still lying in the room, whether or not it
                   is still in the air.  Testing only the airborne one let the
                   arena silt up with bombs. */
                if(boss_shot_of_kind(BOSS_SHOT_BOMB)>=0||boss13_room_bomb()){g_boss_init=0;break;}
                g_boss_phase=1;g_boss_action_timer=0;g_boss_cel=6;
                boss_spawn_shot(BOSS_SHOT_BOMB,ox+(dir?32:-56),oy-32,0,0);
            }else{
                int i=boss_shot_of_kind(BOSS_SHOT_BOMB);
                const signed char *arc;
                if(i<0){g_boss_init=0;g_boss_phase=0;g_boss_cel=0;break;}
                arc=&g_boss13_bomb_arc[g_boss_action_timer*2];
                if(arc[0]==-128){
                    /* $91B2 copies it into the room's own object slot: from
                       here it is an ordinary box Mario can pick up. */
                    boss13_release_bomb(i);
                    g_boss_init=0;g_boss_phase=0;g_boss_cel=0;
                }else{
                    g_boss_shots[i].y+=(s32)arc[0]*FIX_ONE;
                    g_boss_shots[i].x+=(s32)(dir?arc[1]:-arc[1])*FIX_ONE;
                    ++g_boss_action_timer;
                }
            }
            break;
        default: g_boss_init=0; break;  /* 9/10 are the death and win states */
        }
        /* $8D64 flips the tread cel every eighth frame while a move runs. */
        if(state<11&&!g_boss_cel)g_boss_anim_step=(t>>3)&1;
        boss_set_origin(ox,oy);
        break;
    }
    case 4: { /* level 16/$99BE, transcribed from the 68000 handler */
        int ox=boss_origin_x(),oy=boss_origin_y(),state,dir;
        g_boss_vx=0;g_boss_vy=0;
        /* $99C8: a move is chosen every 23rd idle frame.  The source divides
           the VDP counter by the current group's length; the port's own
           reproducible stream stands in for that counter. */
        if(!g_boss_init){
            if(++g_boss_script_index<0x17)break;
            g_boss_script_index=0;
            if(++g_boss16_pick_count>=4&&boss_shot_of_kind(BOSS_SHOT_BELL)<0){
                g_boss16_pick_count=0;
                g_boss_action=8;g_boss_action_dir=g_boss_face<0?1:0;
            }else{
                const unsigned char *rec=g_boss16_groups[g_boss_group]
                    [boss_random()%g_boss16_group_len[g_boss_group]];
                g_boss_action=rec[0];g_boss_group=rec[1];g_boss_action_dir=rec[2];
            }
            g_boss_init=1;g_boss_action_timer=0;g_boss_phase=0;g_boss_cel=0;
            g_boss_face=g_boss_action_dir?-1:1;
            break;
        }
        state=g_boss_action;dir=g_boss_action_dir;
        switch(state){
        case 1: /* $9A88 */
            ox+=dir?2:-2;
            if(++g_boss_action_timer>=0x40){g_boss_action_timer=0;g_boss_init=0;}
            break;
        case 2: /* $9ABC */
            oy-=2;if(oy<=96){oy=96;g_boss_action_timer=0;g_boss_init=0;}
            break;
        case 3: /* $9ADA */
            if(dir){oy-=2;ox+=3;}else{ox-=3;oy+=2;}
            if(++g_boss_action_timer>=0x30){g_boss_action_timer=0;g_boss_init=0;}
            break;
        case 4: /* $9B16 */
            if(dir){oy+=2;ox+=3;}else{ox-=3;oy-=2;}
            if(++g_boss_action_timer>=0x30){g_boss_action_timer=0;g_boss_init=0;}
            break;
        case 5: /* $9B52: vanish, reappear in this group's corner, flicker in */
            ++g_boss_action_timer;
            if(g_boss_action_timer==0x40){
                ox=g_boss16_tele[g_boss_group][0]-BOSS_ROOM_BIAS;
                oy=g_boss16_tele[g_boss_group][1]-BOSS_ROOM_BIAS;
            }
            /* $9B88: blank until $70, then alternate the metasprite against
               zero every frame until $A0 - the source's materialise blink. */
            if(g_boss_action_timer>=0x40&&g_boss_action_timer<0x70)g_boss_hidden=1;
            else if(g_boss_action_timer<0xA0)g_boss_hidden=g_boss_action_timer&1;
            else{g_boss_hidden=0;g_boss_action_timer=0;g_boss_init=0;}
            g_boss_intangible=(g_boss_action_timer>=0x40&&g_boss_action_timer<0xA0)?1:0;
            break;
        case 6: /* $9BE6: three rings leave together and fan apart */
            if(!g_boss_phase){
                int sx=ox+(dir?40:-56),sy=oy-48;
                g_boss_phase=1;g_boss_action_timer=0;g_boss_cel=6;
                boss_spawn_shot(BOSS_SHOT_RING,sx,sy,(s32)(dir?2:-2)*FIX_ONE,-2*FIX_ONE);
                boss_spawn_shot(BOSS_SHOT_RING,sx,sy,(s32)(dir?2:-2)*FIX_ONE,0);
                boss_spawn_shot(BOSS_SHOT_RING,sx,sy,(s32)(dir?2:-2)*FIX_ONE, 2*FIX_ONE);
            }else if(++g_boss_action_timer>=0x64){
                boss_clear_shots(BOSS_SHOT_RING);
                g_boss_action_timer=0;g_boss_init=0;g_boss_phase=0;g_boss_cel=0;
            }
            break;
        case 7: /* $9CC8 */
            oy+=2;if(oy>=192){oy=192;g_boss_action_timer=0;g_boss_init=0;}
            break;
        case 8: /* $9CE6: lower a bell until it reaches the floor and let go */
            if(!g_boss_phase){
                if(boss_shot_of_kind(BOSS_SHOT_BELL)>=0){g_boss_init=0;break;}
                g_boss_phase=1;
                boss_spawn_shot(BOSS_SHOT_BELL,ox,oy-32,0,2*FIX_ONE);
            }else{
                int i=boss_shot_of_kind(BOSS_SHOT_BELL);
                if(i<0||(int)(g_boss_shots[i].y>>FIX_SHIFT)>=160){
                    if(i>=0)boss16_release_bell(i);
                    g_boss_init=0;g_boss_phase=0;
                }
            }
            break;
        default: g_boss_init=0; break;
        }
        boss_set_origin(ox,oy);
        break;
    }
    case 5: { /* level 19/$940E, transcribed from the 68000 handler */
        int ox=boss_origin_x(),oy=boss_origin_y(),state;
        const unsigned char *hop;
        g_boss_vx=0;g_boss_vy=0;
        /* $9418: the same group machine as the mage, on a 48-frame idle. */
        if(!g_boss_init){
            if(++g_boss_script_index<0x30)break;
            g_boss_script_index=0;
            {
                const unsigned char *rec=g_boss19_groups[g_boss_group]
                    [boss_random()%g_boss19_group_len[g_boss_group]];
                g_boss_action=rec[0];g_boss_group=rec[1];g_boss_action_dir=rec[2];
            }
            g_boss_init=1;g_boss_action_timer=0;g_boss_phase=0;
            g_boss_face=g_boss_action_dir?-1:1;
            /* $94BC picks the wind-up pose: cel 1 for a leap, cel 5 for the
               spout.  The `subi.w #$20` that goes with it is not motion --
               $272 and $276 are both drawn a storey lower than $270 ($272 is
               -96..+31 against $270's -96..-1), so lifting the actor 32 is
               exactly what keeps the feet still while the pose changes.  The
               port's five poses all share one bottom-aligned canvas, so that
               compensation is already built in and re-applying it here popped
               the elephant a storey up at every wind-up and a storey down at
               every apex. */
            g_boss_cel=(g_boss_action<=5)?1:5;
            break;
        }
        state=g_boss_action;
        switch(state){
        case 1: case 2: case 3: case 4: case 5: {
            static const unsigned char *const hops[5]={
                g_boss19_hop1,g_boss19_hop2,g_boss19_hop3,g_boss19_hop4,g_boss19_hop5};
            /* $9516 and $95CE step one pixel, $95FC and $961C two.  $963C
               steps none at all: it re-writes X from its group's anchor on
               every frame of the leap, so the boss goes straight up and comes
               straight back down on that column.  Walking it sideways for the
               eighty frames of $988B/$98B4 as well was what carried the
               elephant out through the left wall. */
            int step=(state<=2)?1:2;
            hop=hops[state-1];
            if(state==5)ox=g_boss19_anchor_x[g_boss_group]-BOSS_ROOM_BIAS;
            /* $95CE tests $FF002A and skips the move on its odd passes. */
            else if(state!=2||!(g_boss19_tick&1))ox+=g_boss_action_dir?step:-step;
            if(hop[g_boss_action_timer]==0x88){
                if(!g_boss_phase){
                    /* $9540 turns the arc around and comes back down on cel 2.
                       Its `addi.w #$20` undoes the wind-up pose's storey; see
                       the note at $94BC. */
                    g_boss_phase=1;g_boss_action_timer=0;g_boss_cel=2;
                }else{
                    g_boss_phase=0;g_boss_action_timer=0;g_boss_cel=0;g_boss_init=0;
                    /* $9596 shakes the room on the landing. */
                    g_screen_shake=8;
                }
            }else{
                oy+=g_boss_phase?hop[g_boss_action_timer]:-hop[g_boss_action_timer];
                ++g_boss_action_timer;
            }
            break;
        }
        case 6: { /* $965E: spout the bubbles one at a time, then let them rain */
            /* $9666 walks the four projectile slots in order and stops dead
               at the first one that is not already falling: an empty slot is
               filled and the frame ends there, a still-climbing bubble is
               stepped and the frame ends there, and only a falling one lets
               the walk reach the next slot.  So each bubble waits out the
               whole of $98CE -- twenty-four frames -- before the next leaves
               the trunk, and $96A8 does not drop the reared cel until the
               fourth is out.  Filling all four slots on four consecutive
               frames instead put the attack pose on screen for four frames
               and threw the spread as one clump. */
            int k;
            for(k=0;k<MAX_BOSS_SHOTS;++k){
                BossShot *s=&g_boss_shots[k];
                if(!s->active){
                    /* $9670 spawns at the actor plus $2A/-$42 and minus $40.
                       The source actor is a storey up here from $94F4's pose
                       compensation, which the port does not apply, so the
                       trunk tip is 96 above this actor rather than 64. */
                    boss_spawn_shot(BOSS_SHOT_BUBBLE,
                                    ox+(g_boss_action_dir?42:-66),oy-96,
                                    (s32)(g_boss_action_dir?2:-2)*FIX_ONE,0);
                    /* $96A0: only the last slot drops the cel. */
                    if(k==MAX_BOSS_SHOTS-1)g_boss_cel=0;
                    break;
                }
                if(!s->phase)break;   /* still on the rise: the walk ends here */
            }
            /* $9742: with all four falling, the first one thrown decides when
               $1312A clears the lot.  They pop, they never become ammunition. */
            if(k>=MAX_BOSS_SHOTS&&
               (int)(g_boss_shots[0].y>>FIX_SHIFT)>=BOSS19_BUBBLE_END_Y){
                boss_clear_shots(BOSS_SHOT_BUBBLE);
                g_boss_init=0;g_boss_phase=0;
            }
            break;
        }
        default: g_boss_init=0; break;
        }
        boss_set_origin(ox,oy);
        break;
    }
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
        if(g_boss_invuln==0){++g_boss19_tick;update_boss_motion();}
    }
    if(g_boss_class>=3)update_boss_shots();
    if(g_screen_shake>0)--g_screen_shake;
    /* The source dispenser ($7958) waits while its previous crate object is
       live.  Do the same so unused ammunition cannot carpet the catwalk.
       Bosses whose own handler supplies the throwable are excluded: the pig
       lowers crates from $8564, boss 5 drops bombs, and the ostrich spits a
       pot out of move 11.  Leaving the generic dispenser on for those parks
       a box in the room permanently, and their handlers then skip their own
       drop for ever because the source's occupancy check sees it. */
    /* Bosses 5, 6 and 7 now put their own bombs, bells and hammers into the
       room from their handlers, so the generic dispenser is off for every
       later boss and only Bowser still needs it. */
    /* $7D04 is level 19's own dispenser, and it is not the generic one.  It
       refuses to act at all while $FF7404 still names a live room object, so
       exactly one mallet is ever on the catwalk; once that one is gone it
       counts down a fresh $40..$5F frames, walks its column counter to the
       next of three, and lowers the next mallet from $68 two pixels a frame.
       The elephant supplies no ammunition itself -- $9742 pops its bubbles --
       so without this the final room had nothing to throw. */
    if(g_boss_class==5&&g_boss_invuln==0&&!g_boss_defeated){
        int any_crate=0;
        for(i=0;i<MAX_CRATES;++i)if(g_crates[i].active){any_crate=1;break;}
        if(any_crate)g_boss19_ammo_delay=0;
        else if(g_boss19_ammo_delay==0){
            g_boss19_ammo_delay=(int)(boss_random()&0x1F)+0x40;
            g_boss19_ammo_slot=(g_boss19_ammo_slot+1)%3;
        }else if(--g_boss19_ammo_delay==0){
            for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
                g_crates[i].active=1;g_crates[i].carried=0;g_crates[i].thrown=0;
                g_crates[i].flight_timer=0;g_crates[i].extra=5;
                /* $7D70: $98 + column*$40, in the same +$80 room space. */
                g_crates[i].x=(s32)(0x98-BOSS_ROOM_BIAS+g_boss19_ammo_slot*0x40)<<FIX_SHIFT;
                g_crates[i].y=(s32)BOSS19_AMMO_TOP_Y<<FIX_SHIFT;
                g_crates[i].vx=0;g_crates[i].vy=2*FIX_ONE;break;
            }
        }
    }
    if(g_boss_class==0&&g_boss_invuln==0&&(g_boss_timer%120)==1){
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
    /* The player and crate sides of this test are quoted in the arena's drawn
       space, so the body has to be too. */
    bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT)+bowser_room_object_y_offset();
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
        if(g_boss_class==5){
            const BossVisualData *v=&g_later_boss_visuals[4];
            int f=later_boss_frame(v);
            const unsigned char *b=g_boss19_box[(f<0||f>4)?0:f];
            /* emit_boss_cells mirrors the pose about the canvas centre when
               the elephant faces right, so the box has to turn with it. */
            hitx=bx+((g_boss_face<0)?(112-b[0]-b[2]):b[0]);
            hity=by+b[1];hitw=b[2];hith=b[3];
        }
        /* Slot 13's body is not a guess: $15C9C and $162FE both read the box
           straight out of $18BAC for the driver's current metasprite, which
           is 56x88 for every one of $28C/$28E/$290 and their mirrors, and
           place it at the driver object minus $15D20's (32,56) for the slot.
           The driver object is the vehicle's own point less $20 in Y for the
           poses $6F3A offsets ($7000), and the vehicle's point exactly for
           the rearing and hit poses ($7040/$705C).  In canvas terms that is
           x 24+$7124 wide 56, and rows 8..95 - so it takes in the tank, which
           the old 80x56 driver-only box left untouchable and harmless. */
        if(g_boss_class==3){
            hitx=bx+24+boss13_driver_dx();
            hity=by+40+((boss13_driver_frame()>=BOSS13_FRAME_FIRE)?0:-32);
            hitw=56;hith=88;
        }
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
    /* The class-0 canvases carry Bowser from row 17 down to the bottom edge
       at 96, and the image is drawn at by-9, so his body runs by+8..by+87 --
       79 tall, not the 72 the old value assumed, and the shortfall was all at
       the foot end.  That left the box covering only his head and shell while
       a crate thrown along the floor passed under his legs untouched. */
    else{hitx=bx+12;hity=by+8;hitw=72;hith=79;
        if(g_boss_hit_offsets[g_current_level*2]>0)hitx=bx+(g_boss_hit_offsets[g_current_level*2]>>1);
        if(g_boss_hit_offsets[g_current_level*2+1]>0)hith=g_boss_hit_offsets[g_current_level*2+1];
        if(hith<48)hith=64;if(hith>88)hith=88;
        /* Bowser is perched on a one-way ledge.  Trim the upper 16 pixels of
           the recovered body so a crate thrown on the platform above cannot
           hit him while he is below it.  Keep the bottom edge unchanged. */
        hity+=16;hith-=16;}
    /* $9B88's vanish-and-blink covers $40..$A0 of action 5: the actor is
       fully blanked for the first half and flickers on and off for the
       second.  $18BAC's box comes from the currently drawn metasprite, so
       neither half has a real pose to read a box from -- checking only the
       instantaneous g_boss_hidden state left every other flicker frame
       hittable. Use g_boss_intangible, which covers the whole window. */
    if(g_boss_invuln==0&&hitw>0&&hith>0&&!(g_boss_class==4&&g_boss_intangible)){
        for(i=0;i<MAX_FIREBALLS&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_fireballs[i].active){int fx=(int)(g_fireballs[i].x>>FIX_SHIFT),fy=(int)(g_fireballs[i].y>>FIX_SHIFT);if(aabb(fx+4,fy+4,16,16,hitx,hity,hitw,hith)){boss_take_hit();g_fireballs[i].active=0;}}
        for(i=0;i<MAX_CRATES&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_crates[i].active&&g_crates[i].thrown){int cx=(int)(g_crates[i].x>>FIX_SHIFT),cy=(int)(g_crates[i].y>>FIX_SHIFT)+bowser_room_object_y_offset();if(aabb(cx,cy,24,24,hitx,hity,hitw,hith)){boss_take_hit();g_crates[i].active=0;}}
        /* $15C90 and $162F2 both abandon the test the moment the driver
           holds cel 3, which $1633A sets as the box lands.  The projectile
           loops above can set that this very frame, so the contact test has
           to see it too: without the re-read a box that connects also hurts
           the player, because the body it just hit is still standing in the
           same place for the rest of the frame. */
        if(!g_boss_defeated&&g_invuln==0&&g_boss_invuln==0){
            /* $15C0E measures the player from its own sprite corner as
               (x+4,y+8) by 28x40 and shrinks the boss body by four on every
               side; only the projectile path at $162A0 uses the full body.
               The port's Mario occupies the same 32x48 sprite box, so those
               numbers carry over directly.  The old test pitted an 8x22
               sliver against an already shrunken body, which is why the tank
               could drive through Mario without touching him. */
            int px=(int)(g_player_x>>FIX_SHIFT),py=(int)(g_player_y>>FIX_SHIFT)+bowser_room_object_y_offset();
            int hurt=(g_boss_class==3)
                ?aabb(px+4,py+8,28,40,hitx+4,hity+4,hitw-8,hith-8)
                :aabb(player_body_x()+4,player_body_y()+bowser_room_object_y_offset()+5,8,PLAYER_BODY_H-10,
                      hitx+4,hity+4,hitw-8,hith-8);
            if(hurt)hurt_player();
        }
    }
}

static void start_gameplay(int fade_in)
{
    int ycrop;
    /* Every normal level is selected through the 19-entry reconstruction table. */
    g_in_boss=0; g_paused=0;
    if(g_current_level==PARALLAX_LEVEL)g_parallax_clock=0;
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
    /* The walk starts on the record the level entered through, which is the
       one holding the spawn -- normally route 0, but the debug warps drop
       Mario straight into a later room. */
    if(g_level_rows>1){
        int k,lr,gr=split_world_row((int)((g_player_y>>FIX_SHIFT)+16)>>3,&lr);
        int sc=(int)((g_player_x>>FIX_SHIFT)+8)>>8;
        for(k=0;k<g_screen_count;++k)
            if(g_screen_col[k]==sc&&g_screen_row[k]==gr){g_room=k;break;}
        g_camera_y=g_camera_target_y=(int)g_screen_row[g_room]*LEVEL_SCREEN_H;
        g_camera_row=g_camera_y>>3;
        g_ride_camera_hold=0;
    }
    g_vx=g_vy=0;
    g_grounded=1;
    g_facing=1;
    g_player_anim_clock=0;
    g_player_down=0;
    g_jump_active=0; g_jump_index=0;
    g_bounce_active=0; g_bounce_index=0;
    g_camera_hold=0;
    if(!g_keep_ammo){g_fireball_ammo=3;g_luigi.fireball_ammo=3;}
    g_keep_ammo=0;
    g_player_hearts=PLAYER_MAX_HEARTS;
    g_invuln=0;
    g_hurt_pending_death=0;
    g_throw_timer=0;
    g_death_timer=0;
    g_player_mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;
    if(multiplayer_active()&&!g_luigi.out){
        g_luigi.x=g_player_x+24*FIX_ONE;g_luigi.y=g_player_y;
        g_luigi.vx=g_luigi.vy=0;g_luigi.grounded=1;g_luigi.facing=1;
        g_luigi.anim_clock=0;g_luigi.jump_active=0;g_luigi.jump_index=0;g_luigi.down=0;
        g_luigi.mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;g_luigi.death_timer=0;
        g_luigi.invuln=0;g_luigi.hearts=PLAYER_MAX_HEARTS;
        g_luigi.carry_index=-1;g_luigi.lift_timer=0;g_luigi.throw_timer=0;g_luigi.crate_hide=0;
    }
    {int c;for(c=0;c<PALETTE_CYCLE_COUNT;++c)g_palette_cycle_shift[c]=0;}
    set_game_palette_fade(fade_in?0:FADE_FULL);
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

    update_gear_palette();

    if (pressed&JOY_MODE1) {
        g_debug_mode^=1;
        if (g_debug_mode) {
            g_player_hearts=PLAYER_MAX_HEARTS;g_fireball_ammo=DEBUG_AMMO_DISPLAY;
            g_luigi.hearts=PLAYER_MAX_HEARTS;g_luigi.fireball_ammo=DEBUG_AMMO_DISPLAY;
            g_invuln=0;g_hurt_pending_death=0;
            if(g_player_mode==PMODE_DYING||g_player_mode==PMODE_FADE_OUT)g_player_mode=PMODE_NORMAL;
        }
    }
    if (g_debug_mode) {g_player_hearts=PLAYER_MAX_HEARTS;g_fireball_ammo=DEBUG_AMMO_DISPLAY;g_luigi.hearts=PLAYER_MAX_HEARTS;g_luigi.fireball_ammo=DEBUG_AMMO_DISPLAY;}
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
    if (g_player_mode==PMODE_OUT) {
        draw_game_sprites();
        return;
    }
    if (g_player_mode==PMODE_REENTRY) {
        if(btn&JOY_LEFT){g_player_x-=run_speed;g_facing=-1;}
        else if(btn&JOY_RIGHT){g_player_x+=run_speed;g_facing=1;}
        if((int)(g_player_x>>FIX_SHIFT)<g_camera_x)g_player_x=(s32)g_camera_x<<FIX_SHIFT;
        if((int)(g_player_x>>FIX_SHIFT)>g_camera_x+224)g_player_x=(s32)(g_camera_x+224)<<FIX_SHIFT;
        if(++g_death_timer>=MULTI_HOVER_FRAMES){
            g_player_mode=PMODE_NORMAL;g_invuln=MULTI_INVULN_FRAMES;
            g_grounded=0;g_vy=0;g_death_timer=0;
        }
        draw_game_sprites();
        return;
    }
    if (g_player_mode==PMODE_DYING) {
        g_player_y+=g_vy;
        g_vy+=64;
        if (g_vy>7*FIX_ONE) g_vy=7*FIX_ONE;
        ++g_death_timer;
        draw_game_sprites();
        if (multiplayer_active() && (int)(g_player_y>>FIX_SHIFT)>g_camera_y+256) {
            finish_mario_multiplayer_death();
        } else if (!multiplayer_active() && (int)(g_player_y>>FIX_SHIFT)>256) {
            g_player_mode=PMODE_FADE_OUT;
            g_death_timer=0;
        }
        return;
    }
    if (g_player_mode==PMODE_FADE_OUT) {
        int level;
        ++g_death_timer;
        level=FADE_FULL-(g_death_timer*FADE_FULL/48);
        if (level<0) level=0;
        set_game_palette_fade(level);
        draw_game_sprites();
        if (g_death_timer>=48) {
            Stop_Sound_Sample(SFX_PLAYER_HURT,0);
            /* A normal death consumes one reserve life.  Do not enter the
               CONTINUE room while reserves remain: reload the current level
               from its source spawn instead.  A death inside a boss arena
               reloads that arena, so the fight restarts rather than the
               level that led into it. */
            if (g_lives>0) --g_lives;
            if (g_lives>0) {if (g_in_boss) start_boss(1); else start_gameplay(1);}
            else if (g_continues>0) load_continue_hidden();
            else enter_boot();
        }
        return;
    }
    if (g_player_mode==PMODE_FADE_IN) {
        int level;
        ++g_death_timer;
        level=g_death_timer*FADE_FULL/48;
        if (level>FADE_FULL) level=FADE_FULL;
        set_game_palette_fade(level);
        draw_game_sprites();
        if (g_death_timer>=48) {
            set_game_palette_fade(FADE_FULL);
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
    /* $17238/$170F8 stop Mario at the room edge when the route does not carry
       on that way, so he cannot walk out over the void beside a shaft. */
    if (g_level_rows>1) {
        int px=(int)(g_player_x>>FIX_SHIFT),clo,chi,rlo,rhi;
        update_current_room(px,(int)(g_player_y>>FIX_SHIFT));
        room_window(g_room,&clo,&chi,&rlo,&rhi);
        if (px<clo*256) { g_player_x=(s32)(clo*256)<<FIX_SHIFT; if(g_vx<0)g_vx=0; }
        else if (px>(chi+1)*256-24) { g_player_x=(s32)((chi+1)*256-24)<<FIX_SHIFT; if(g_vx>0)g_vx=0; }
    }

    if (g_spring_flight) {
        /* The source never moves the launched player at all: $18028 withholds
           his gravity while $FF00AE names him, and $146BC scrolls the room up
           by the table step instead, leaving him where he is.  Here the world
           is fixed and the camera moves, so the player takes the step and the
           camera takes it with him -- see g_spring_camera_step.  Nothing on
           the way is tested for either, the source being a scroll and not a
           move, so the rise runs through the shaft's mouth rather than
           stopping under it. */
        int step=(int)g_spring_rise[g_spring_flight-1];
        g_player_y-=(s32)step<<FIX_SHIFT;
        g_spring_camera_step=(unsigned char)step;
        g_vy=0; g_grounded=0;
        if (++g_spring_flight>(unsigned char)sizeof(g_spring_rise)) g_spring_flight=0;
    } else if (g_bounce_active) {
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

    /* $18054 runs the belt: standing on a conveyor cell subtracts one from the
       actor's X every tick, so Mario slides back along the machine while he
       walks over it. */
    if (g_grounded && player_on_conveyor()) {
        g_player_x-=FIX_ONE;
        if ((int)(g_player_x>>FIX_SHIFT)<g_camera_x)
            g_player_x=(s32)g_camera_x<<FIX_SHIFT;
    }

    if (g_throw_timer>0) {
        --g_throw_timer;
        if (!g_grounded || g_throw_timer==0) g_camera_hold=0;
    }

    /* Falling through an open pit is fatal once Mario leaves the 224-line
       source playfield. The visual extension rows remain non-colliding.
       In a stacked level the pit is the bottom of the room window rather than
       the bottom of the whole tower: the route's own neighbour is the only
       storey below that has been pasted, so dropping out of a shaft with
       nothing under it has to end there.  Measuring against the tower instead
       let the camera's follow band trail him down through storeys the route
       never reached, which is the fall that stayed on screen long after it
       should have been a death. */
    if ((int)(g_player_y>>FIX_SHIFT)>player_pit_floor()) {
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
#ifdef FORCE_EXIT_AFTER
    /* Build with EXTRA_CFLAGS=-DFORCE_EXIT_AFTER=<frames> to end the stage on a
       timer instead of at its exit, so a headless capture can reach a
       level-to-level transition without playing the level. */
    if(!g_in_boss && g_state_frame>=(unsigned)FORCE_EXIT_AFTER){
        g_after_boss=0;
        if(g_boss_level_flags[g_current_level]) music_stop_for_transition();
        else music_prepare_level_transition(g_current_level+1);
        g_state=STATE_LEVEL_FADE; g_state_frame=0; return;
    }
#endif
    /* $143EA ends the stage when the route walk runs off its $8888 terminator,
       i.e. on leaving the last screen of the route -- not the right edge of the
       grid's bounding box, which a climbing level never reaches. */
    if(!g_in_boss && level_exit_reached()){
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
    static u16 satb[64*4],back[64*4];
    u16 base;
    int i; for (i=0;i<64*4;++i) satb[i]=0;
    submit_front_satb(satb);
    for (i=0;i<64*4;++i) back[i]=0;
    base=(u16)(g_back_satb_page?BACK_SATB_BASE1:BACK_SATB_BASE0);
    vdc_set_vram_write(BACK_CHIP,base);
    for(i=0;i<64*4;++i)vdc_vram_write(BACK_CHIP,back[i]);
    vdc_set_satb_address(BACK_CHIP,base);
    g_back_satb_page^=1;
}

static void set_front_sprites_enabled(int enabled)
{
    /* SATB replacement is deferred by the VDC, so clearing it alone can leave
       the old, fully lit sprite list on one output frame.  Gate the sprite
       plane itself for every screen transition; the background plane remains
       available for its normal palette fade. */
    vdc_setreg(FRONT_CHIP, VDC_REG_CR,
               enabled ? (u16)(VDC_CR_BB | VDC_CR_SB) : VDC_CR_BB);
    if(!enabled)vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
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
        pal_set((u16)(240+i),fade_yuv(g_player_palette[i],level));
        pal_set((u16)(224+i),fade_yuv(g_coin_palette[i],level));
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

    fade=(sf<108)?(int)((sf-60)*FADE_FULL/48):FADE_FULL;
    if (fade<0) fade=0; if (fade>FADE_FULL) fade=FADE_FULL;
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
    int panel=(choice==MENU_2P)?1:0;
    vdc_set_scroll(BACK_CHIP,(u16)(panel*SCREEN_W),256);
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

    /* The menu pair shares Mario's indexed patterns, as it does in gameplay;
       palette selection alone distinguishes Luigi. */
    for (i=0;i<16;++i) {
        pal_set((u16)(240+i),fade_yuv(g_menu_mario_palette[i],g_menu_sprite_fade));
        pal_set((u16)(LUIGI_SPR_PAL*16+i),
                          fade_yuv(g_luigi_reuse_palette[i],g_menu_sprite_fade));
    }

    add_menu_32x48(satb,&n,mx,160,
        (u16)(MENU_MARIO_BASE_WORD+frame*PLAYER_FRAME_WORDS_LOCAL),1,15);
    if (g_menu_choice==MENU_2P)
        add_menu_32x48(satb,&n,mx+30,160,
            (u16)(MENU_MARIO_BASE_WORD+frame*PLAYER_FRAME_WORDS_LOCAL),
            1,LUIGI_SPR_PAL);

    /* Never scroll VDC1 during the menu. r5's scroll-based walk caused the
       brief centered/shifted frame during transitions. */
    vdc_set_scroll(FRONT_CHIP,0,0);
    submit_front_satb(satb);
}

#include "staff_font_generated.h"

/* ------------------------------------------------------------------ *
 * Staff roll.
 *
 * The Mega Drive ending scrolls its cast and staff over a moving field.  The
 * port rebuilds that from parts it already has: the credit stream lives in
 * src/staff_roll.c, the glyphs are expanded out of the 1bpp table in
 * src/staff_font_generated.h, and each named family is drawn from its own
 * level enemy bank rather than from a separate ending art set.
 *
 * Plane assignment follows the rest of the game.  VDC0 carries the starfield,
 * which is opaque and scrolls on its full 64x64 map, and VDC1 carries the text
 * on the 64x32 foreground map, whose index-0 cells let the stars through.  The
 * text map wraps every 256 lines, so rows are refilled from the stream as they
 * leave the top edge.
 * ------------------------------------------------------------------ */

/* Glyph tiles reuse the foreground tile bank, which no other staff-roll bank
   touches.  Index 0 is the space glyph, so a blank cell is a real transparent
   tile rather than tile 0 aliasing the BAT. */
#define STAFF_FONT_TILE_BASE_WORD FG_TILE_BASE_WORD
#define STAFF_FONT_TILE_INDEX (STAFF_FONT_TILE_BASE_WORD / TILE_WORDS)
#define STAFF_BLANK_CELL ((u16)STAFF_FONT_TILE_INDEX)
/* Starfield tiles: one empty plus eight sparse patterns, on VDC0. */
#define STAFF_STAR_TILE_BASE_WORD BG_TILE_BASE_WORD
#define STAFF_STAR_TILE_INDEX (STAFF_STAR_TILE_BASE_WORD / TILE_WORDS)
#define STAFF_STAR_TILE_COUNT 9
/* Cast art starts above the glyph bank and has the rest of VDC1 to itself. */
#define STAFF_ENEMY_BASE_WORD 0x1000
#define STAFF_ENEMY_TOP_WORD  0xE000
#define STAFF_ENEMY_PAL0 4
#define STAFF_ENEMY_PAL_COUNT 12
#define STAFF_CAST_MAX 20
/* One text line every 16 lines, i.e. every second BAT row. */
#define STAFF_LINE_PITCH 16
#define STAFF_VISIBLE_COLS 32
#define STAFF_TEXT_MAP_ROWS 32
/* 8.8 pixels of scroll per frame.  The roll runs 87 seconds at this rate,
   which is the length of the staff cue on the disc; the cue loops rather than
   falling silent, so overshooting it is untidy rather than broken. */
#define STAFF_SCROLL_STEP 88
#define STAFF_FADE_FRAMES 48

static u16 g_staff_palette[256];
/* Everything draw_staff_sprites() needs, resolved once at load time so the
   scroll loop does not have to re-derive a pose's bank or geometry. */
static u16 g_staff_cast_vram[STAFF_CAST_MAX];
static u16 g_staff_cast_mask[STAFF_CAST_MAX][2];
static u16 g_staff_cast_frame_words[STAFF_CAST_MAX];
static unsigned char g_staff_cast_cols[STAFF_CAST_MAX];
static unsigned char g_staff_cast_pal[STAFF_CAST_MAX];
static unsigned char g_staff_cast_ready[STAFF_CAST_MAX];
/* Cell-space bounding box of the two poses, so a member whose art is packed
   into a corner of its grid is still centred on its own name. */
static unsigned char g_staff_cast_x0[STAFF_CAST_MAX],g_staff_cast_x1[STAFF_CAST_MAX];
static unsigned char g_staff_cast_y0[STAFF_CAST_MAX],g_staff_cast_y1[STAFF_CAST_MAX];
static s32 g_staff_scroll;      /* 8.8 pixels */
/* Scroll targets, published at frame wrap by commit_staff_scroll().  Writing
   either plane's registers from update_staff() lands after that frame's
   palette and SATB burst has already started, and the VDC restarts its line
   fetch from the new BYR where the write falls -- which repeats the top of the
   text plane a dozen lines down.  This is the same split commit_stage_camera()
   exists to keep out of gameplay. */
static u16 g_staff_scroll_y, g_staff_star_x, g_staff_star_y;

static void commit_staff_scroll(void)
{
    vdc_set_scroll(FRONT_CHIP,0,g_staff_scroll_y);
    vdc_set_scroll(BACK_CHIP,g_staff_star_x,g_staff_star_y);
}
static int g_staff_filled_row;  /* next absolute 8-line row to write */
static int g_staff_total_px;
static int g_staff_done;

static void staff_upload_font(void)
{
    int g,row;
    vdc_set_vram_write(FRONT_CHIP, STAFF_FONT_TILE_BASE_WORD);
    for (g=0;g<STAFF_FONT_GLYPH_COUNT;++g) {
        const unsigned char *bits=&g_staff_font[g*8];
        /* Colour 1 is the face and colour 2 the drop shadow one pixel down and
           right, which is what keeps the text readable over the stars.  Both
           live in the low pair of bitplanes, so the high pair is all zero. */
        for (row=0;row<8;++row) {
            unsigned face=bits[row];
            unsigned shadow=(row?bits[row-1]:0u)>>1;
            shadow&=~face;
            vdc_vram_write(FRONT_CHIP,(u16)(face|(shadow<<8)));
        }
        for (row=0;row<8;++row) vdc_vram_write(FRONT_CHIP,0);
    }
}

/* A cheap deterministic sequence: the starfield only needs to look unplanned,
   and a table of star positions would cost ROM for no visible gain. */
static unsigned g_staff_rand;
static unsigned staff_rand(void)
{
    g_staff_rand=g_staff_rand*1103515245u+12345u;
    return (g_staff_rand>>16)&0x7FFF;
}

static void staff_upload_stars(void)
{
    int t,row,i;
    vdc_set_vram_write(BACK_CHIP, STAFF_STAR_TILE_BASE_WORD);
    for (row=0;row<TILE_WORDS;++row) vdc_vram_write(BACK_CHIP,0);
    for (t=1;t<STAFF_STAR_TILE_COUNT;++t) {
        u16 p0[8],p1[8];
        for (row=0;row<8;++row) { p0[row]=0; p1[row]=0; }
        /* One to three stars a tile, each in one of the three brightnesses
           held in palette entries 1..3, which the twinkle then cycles. */
        for (i=0;i<1+(int)(staff_rand()%3);++i) {
            int x=(int)(staff_rand()&7), y=(int)(staff_rand()&7);
            int c=1+(int)(staff_rand()%3);
            if (c&1) p0[y]|=(u16)(1<<(7-x));
            if (c&2) p1[y]|=(u16)(1<<(7-x));
        }
        for (row=0;row<8;++row) vdc_vram_write(BACK_CHIP,(u16)(p0[row]|(p1[row]<<8)));
        for (row=0;row<8;++row) vdc_vram_write(BACK_CHIP,0);
    }
}

static void staff_fill_star_bat(void)
{
    int i;
    /* The full 64x64 map, so the field wraps every 512 pixels in both axes and
       the diagonal drift never shows a seam. */
    vdc_set_vram_write(BACK_CHIP, BG_BAT_BASE_WORD);
    for (i=0;i<BG_BAT_WORDS;++i) {
        /* Roughly one cell in five carries a star; the rest are the empty tile
           so the field reads as sparse rather than as noise. */
        int t=(staff_rand()%5)?0:(1+(int)(staff_rand()%(STAFF_STAR_TILE_COUNT-1)));
        vdc_vram_write(BACK_CHIP,(u16)(STAFF_STAR_TILE_INDEX+t));
    }
}

static void staff_cast_bounds(int c,int cols,int rows)
{
    int cx,cy,x0=cols,x1=-1,y0=rows,y1=-1;
    unsigned mask=(unsigned)(g_staff_cast_mask[c][0]|g_staff_cast_mask[c][1]);
    for (cy=0;cy<rows;++cy) for (cx=0;cx<cols;++cx) {
        if (!(mask&(1u<<(cy*cols+cx)))) continue;
        if (cx<x0) x0=cx;
        if (cx>x1) x1=cx;
        if (cy<y0) y0=cy;
        if (cy>y1) y1=cy;
    }
    if (x1<0) { x0=0; x1=cols-1; y0=0; y1=rows-1; }
    g_staff_cast_x0[c]=(unsigned char)x0; g_staff_cast_x1[c]=(unsigned char)x1;
    g_staff_cast_y0[c]=(unsigned char)y0; g_staff_cast_y1[c]=(unsigned char)y1;
}

static void staff_upload_cast(void)
{
    int c,i,j,uniq=0,top=STAFF_ENEMY_BASE_WORD;
    const unsigned short *seen[STAFF_ENEMY_PAL_COUNT];
    for (c=0;c<STAFF_CAST_MAX;++c) g_staff_cast_ready[c]=0;
    for (c=0;c<(int)g_staff_cast_count && c<STAFF_CAST_MAX;++c) {
        const StaffCast *sc=&g_staff_cast[c];
        const EnemyVariantData *v;
        const unsigned short *src0,*src1;
        int fw,cols,rows,line=-1;
        if (sc->variant>=g_enemy_variant_count) continue;
        v=&g_enemy_variants[sc->variant];
        cols=v->cols; rows=v->rows;
        /* Resolve the pair of cels this member is presented on, and where each
           of them lives, before anything is reserved for it. */
        switch (sc->pose) {
        case STAFF_POSE_FLY:
            /* Walk cel 0 is the pterodactyl hanging from a ceiling; the two
               flight cels are walk cel 1 and the alt bank. */
            if (!v->alt_words) continue;
            fw=enemy_frame_words(v);
            src0=v->words+fw; src1=v->alt_words;
            g_staff_cast_mask[c][0]=v->mask1; g_staff_cast_mask[c][1]=v->alt_mask;
            break;
        case STAFF_POSE_CRATE:
            /* The mimic's own cels are the closed box and the lid coming off.
               Walking is a separate 3x3 bank shared by every mimic. */
            cols=rows=3;
            fw=ENEMY_FRAME_WORDS;
            src0=g_crate_walk_words; src1=g_crate_walk_words+ENEMY_FRAME_WORDS;
            g_staff_cast_mask[c][0]=g_crate_walk_masks[0];
            g_staff_cast_mask[c][1]=g_crate_walk_masks[1];
            break;
        default:
            fw=enemy_frame_words(v);
            src0=v->words; src1=v->words+fw;
            g_staff_cast_mask[c][0]=v->mask0; g_staff_cast_mask[c][1]=v->mask1;
            break;
        }
        if (top+fw*2>STAFF_ENEMY_TOP_WORD) continue;
        /* Members that share a source palette share a line here too: sixteen
           lines have to cover the stars, three text colours and the whole
           cast at once. */
        for (j=0;j<uniq;++j)
            if (palettes_equal(seen[j],v->palette)) { line=STAFF_ENEMY_PAL0+j; break; }
        if (line<0) {
            if (uniq>=STAFF_ENEMY_PAL_COUNT) continue;
            seen[uniq]=v->palette;
            line=STAFF_ENEMY_PAL0+uniq;
            for (i=0;i<16;++i) g_staff_palette[line*16+i]=v->palette[i];
            ++uniq;
        }
        g_staff_cast_pal[c]=(unsigned char)line;
        g_staff_cast_vram[c]=(u16)top;
        g_staff_cast_frame_words[c]=(u16)fw;
        g_staff_cast_cols[c]=(unsigned char)cols;
        vdc_set_vram_write(FRONT_CHIP,(u16)top);
        for (i=0;i<fw;++i) vdc_vram_write(FRONT_CHIP,src0[i]);
        for (i=0;i<fw;++i) vdc_vram_write(FRONT_CHIP,src1[i]);
        top+=fw*2;
        staff_cast_bounds(c,cols,rows);
        g_staff_cast_ready[c]=1;
    }
}

static void staff_build_palette(void)
{
    int i;
    for (i=0;i<256;++i) g_staff_palette[i]=YUV_BLACK;
    /* Line 0 is the starfield on VDC0.  Entry 0 is the night sky itself, and
       1..3 are the three star brightnesses. */
    g_staff_palette[0]=0x0A98;
    g_staff_palette[1]=0x6298;
    g_staff_palette[2]=0xB298;
    g_staff_palette[3]=0xFF88;
    /* Text lines: entry 1 is the face and entry 2 the shadow. */
    g_staff_palette[STAFF_PAL_TITLE*16+1]=0xCA4B;  /* gold   */
    g_staff_palette[STAFF_PAL_TITLE*16+2]=YUV_BLACK;
    g_staff_palette[STAFF_PAL_NAME*16+1]=0xFF88;   /* white  */
    g_staff_palette[STAFF_PAL_NAME*16+2]=YUV_BLACK;
    g_staff_palette[STAFF_PAL_BODY*16+1]=0xBFA4;   /* cyan   */
    g_staff_palette[STAFF_PAL_BODY*16+2]=YUV_BLACK;
}

static void set_staff_palette_fade(int level)
{
    int i;
    for (i=0;i<256;++i) pal_set((u16)i,fade_yuv(g_staff_palette[i],level));
}

/* Twinkle by rotating the three star brightnesses through palette 1..3 rather
   than by rewriting any tile, so it costs three palette writes a second. */
static void staff_twinkle(int level)
{
    static const u16 ramp[3]={0x6298,0xB298,0xFF88};
    int phase=(int)((g_state_frame/20)%3),i;
    for (i=0;i<3;++i)
        pal_set((u16)(1+i),fade_yuv(ramp[(i+phase)%3],level));
}

static void staff_fill_row(int abs_row)
{
    u16 cells[STAFF_VISIBLE_COLS];
    int i,line,col,len;
    const StaffLine *sl;
    const char *s;
    for (i=0;i<STAFF_VISIBLE_COLS;++i) cells[i]=STAFF_BLANK_CELL;
    /* Text sits on the first BAT row of each 16-line slot; the second is the
       gap between lines. */
    if ((abs_row&1)==0) {
        line=abs_row>>1;
        if (line>=0 && line<(int)g_staff_line_count) {
            sl=&g_staff_lines[line];
            s=sl->text;
            if (s) {
                for (len=0;s[len];++len) {}
                col=(sl->col==STAFF_COL_CENTER)?((STAFF_VISIBLE_COLS-len)/2):(int)sl->col;
                if (col<0) col=0;
                for (i=0;i<len && col+i<STAFF_VISIBLE_COLS;++i) {
                    int c=(unsigned char)s[i]-STAFF_FONT_FIRST_CHAR;
                    if (c<0 || c>=STAFF_FONT_GLYPH_COUNT) c=0;
                    cells[col+i]=(u16)(((u16)sl->pal<<12)|(STAFF_FONT_TILE_INDEX+c));
                }
            }
        }
    }
    vdc_set_vram_write(FRONT_CHIP,
        (u16)(FG_BAT_BASE_WORD+(abs_row&(STAFF_TEXT_MAP_ROWS-1))*FG_BAT_W));
    for (i=0;i<STAFF_VISIBLE_COLS;++i) vdc_vram_write(FRONT_CHIP,cells[i]);
    /* The map is 64 cells wide against a 256-pixel screen; blank the offscreen
       half so a horizontal scroll never exposes stale glyphs. */
    for (;i<FG_BAT_W;++i) vdc_vram_write(FRONT_CHIP,STAFF_BLANK_CELL);
}

static void staff_fill_ahead(void)
{
    int scroll_px=(int)(g_staff_scroll>>FIX_SHIFT);
    /* Writing absolute row R replaces the cell row that is still showing row
       R-32, so R may only be written once that older row is entirely above the
       top edge -- R-32 < scroll/8, against the cell row the top line actually
       falls in and not against the raw pixel.  That still reaches one row past
       the bottom of the 240-line screen, so nothing scrolls in blank. */
    while (g_staff_filled_row*8 <= (scroll_px&~7)+248) {
        staff_fill_row(g_staff_filled_row);
        ++g_staff_filled_row;
    }
}

static void draw_staff_sprites(void)
{
    static u16 satb[64*4];
    int i,n=0,scroll_px=(int)(g_staff_scroll>>FIX_SHIFT);
    for (i=0;i<64*4;++i) satb[i]=0;
    for (i=0;i<(int)g_staff_line_count && n<62;++i) {
        const StaffLine *sl=&g_staff_lines[i];
        int c=sl->cast,cx,cy,cols,x0,x1,y0,y1,sx,sy,frame; unsigned mask; u16 base;
        if (c<0 || c>=STAFF_CAST_MAX || !g_staff_cast_ready[c]) continue;
        cols=g_staff_cast_cols[c];
        x0=g_staff_cast_x0[c]; x1=g_staff_cast_x1[c];
        y0=g_staff_cast_y0[c]; y1=g_staff_cast_y1[c];
        /* Centre the occupied cells on the name row and right-align them
           against the text column, so tall and short members share a line. */
        sy=i*STAFF_LINE_PITCH+4-((y1-y0+1)*16)/2-y0*16-scroll_px;
        sx=104-(x1+1)*16;
        if (sy+y0*16<-64 || sy+y0*16>240) continue;
        frame=(int)((g_state_frame/12)&1);
        mask=g_staff_cast_mask[c][frame];
        base=(u16)(g_staff_cast_vram[c]+frame*g_staff_cast_frame_words[c]);
        for (cy=y0;cy<=y1 && n<62;++cy) for (cx=x0;cx<=x1 && n<62;++cx) {
            int cell=cy*cols+cx;
            if (!(mask&(1u<<cell))) continue;
            set_sprite_entry(satb,n++,sx+cx*16,sy+cy*16,(u16)(base+cell*64),
                (u16)(VDC_SPR_PRIO_SP|g_staff_cast_pal[c]));
        }
    }
    submit_front_satb(satb);
}

static void enter_staff(void)
{
    int i;
    /* Both outputs are already black when the ending reaches here, and the
       uploads below span more than one frame. */
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    for (i=0;i<256;++i) pal_set((u16)i,YUV_BLACK);
    clear_satb();
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    /* VDC0 keeps its full 64x64 map for the star drift; VDC1 wraps every 256
       lines, which is what the row refill below is written against. */
    set_map_height(BACK_CHIP,0);
    set_map_height(FRONT_CHIP,1);
    g_staff_scroll_y=0; g_staff_star_x=0; g_staff_star_y=0;
    commit_staff_scroll();

    g_staff_rand=0x1F2E3D4Cu;
    staff_build_palette();
    staff_upload_font();
    staff_upload_stars();
    staff_fill_star_bat();
    staff_upload_cast();

    g_staff_scroll=0;
    g_staff_filled_row=0;
    g_staff_done=0;
    /* Run the last line clear of the top edge before the fade-out begins. */
    g_staff_total_px=(int)g_staff_line_count*STAFF_LINE_PITCH;
    staff_fill_ahead();
    draw_staff_sprites();

    g_in_boss=0;
    music_stop_for_transition();
    music_transition_to_staff();
    music_start_at_black();
    g_state=STATE_STAFF_LOAD;
    g_state_frame=0;
}

static void update_staff(u16 pressed)
{
    int level,scroll_px;
    if (g_state==STATE_STAFF_LOAD) {
        /* One complete black frame after the uploads before either plane is
           published, matching the stage loader. */
        if (g_state_frame>=2) {
            vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
            vdc_setreg(FRONT_CHIP,VDC_REG_CR,(u16)(VDC_CR_BB|VDC_CR_SB));
            g_state=STATE_STAFF; g_state_frame=0;
        }
        return;
    }
    if (g_state==STATE_STAFF_OUT) {
        level=FADE_FULL-(int)(g_state_frame*FADE_FULL/STAFF_FADE_FRAMES);
        if (level<0) level=0;
        set_staff_palette_fade(level);
        if (g_state_frame>=(unsigned)STAFF_FADE_FRAMES) {
            clear_front_sprites();
            set_front_sprites_enabled(0);
            music_stop_for_transition();
            enter_boot();
        }
        return;
    }

    if (g_state_frame<(unsigned)STAFF_FADE_FRAMES) {
        level=(int)(g_state_frame*FADE_FULL/STAFF_FADE_FRAMES);
        set_staff_palette_fade(level);
    } else {
        /* Past the fade the palette is static, so only the three star entries
           are rewritten each frame; a full 256-entry pass every frame is more
           VBlank than this screen has to spend. */
        level=16;
    }
    staff_twinkle(level);

    /* RUN skips the rest of the roll rather than dropping straight out of it,
       so the fade to the title is the same either way. */
    if (!g_staff_done && (pressed&(JOY_RUN|JOY_I|JOY_II))) {
        g_staff_done=1;
        g_state=STATE_STAFF_OUT; g_state_frame=0;
        return;
    }

    g_staff_scroll+=STAFF_SCROLL_STEP;
    scroll_px=(int)(g_staff_scroll>>FIX_SHIFT);
    staff_fill_ahead();
    g_staff_scroll_y=(u16)(scroll_px&255);
    /* The stars drift the other way and more slowly than the text, which reads
       as depth behind it. */
    g_staff_star_x=(u16)((g_state_frame/4)&511);
    g_staff_star_y=(u16)((512-((g_state_frame/6)&511))&511);
    draw_staff_sprites();

    if (scroll_px>g_staff_total_px) {
        g_staff_done=1;
        g_state=STATE_STAFF_OUT; g_state_frame=0;
    }
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
    g_menu_sprite_fade=FADE_FULL;
    show_menu_back(g_menu_choice);
    menu_overlay_prepare();
    set_asset_palette_fade(&g_menu_combo_asset,FADE_FULL);
    menu_overlay_enable();
    g_state=STATE_MENU;
    g_state_frame=0;
    draw_menu_people(0);
}

static void update_frontend(u16 pressed)
{
    if (g_state>=STATE_STAFF_LOAD) { update_staff(pressed); return; }
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
        /* Timings measured off the Mega Drive in BlastEm, START at frame 650:
           the title is black 80 frames later, holds black for 35, and the menu
           is fully bright 178 frames after START.  The port used to spend 90
           and 80 frames on the two ramps with only a 20-frame gap between
           them, which is where the transition read as sluggish. */
        if (g_state_frame<TITLE_FADE_OUT) {
            int level=FADE_FULL-(int)(g_state_frame*FADE_FULL/TITLE_FADE_OUT);
            set_title_exact_palette_fade(level);
            clear_front_sprites();
            set_front_sprites_enabled(0);
        } else if (g_state_frame<TITLE_FADE_IN_START) {
            if (g_state_frame==TITLE_FADE_OUT) {
                /* Give the output-disable write a complete frame to latch.
                   Starting the long upload here can otherwise retain one
                   bright scanline from the old screen at the frame boundary. */
                vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
                vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
                set_title_exact_palette_fade(0);
            } else if (g_state_frame==TITLE_FADE_OUT+1) {
                load_menu_asset();
                g_menu_choice=MENU_1P;
                g_menu_sprite_fade=0;
                vdc_set_scroll(BACK_CHIP,0,256);
                menu_overlay_prepare();
                set_asset_palette_fade(&g_menu_combo_asset,0);
            } else if (g_state_frame==TITLE_FADE_OUT+2) {
                menu_overlay_enable();
            }
            clear_front_sprites();
        } else if (g_state_frame<TITLE_FADE_DONE) {
            int level=(int)((g_state_frame-TITLE_FADE_IN_START)*FADE_FULL/
                            (TITLE_FADE_DONE-TITLE_FADE_IN_START));
            if (level>FADE_FULL) level=FADE_FULL;
            g_menu_sprite_fade=level;
            set_asset_palette_fade(&g_menu_combo_asset,level);
            draw_menu_people(0);
        } else {
            g_menu_choice=MENU_1P;
            g_menu_sprite_fade=FADE_FULL;
            set_asset_palette_fade(&g_menu_combo_asset,FADE_FULL);
            g_state=STATE_MENU;
            g_state_frame=0;
            draw_menu_people(0);
            return;
        }
    } else if (g_state==STATE_MENU) {
        if (pressed&(JOY_UP|JOY_DOWN|JOY_LEFT|JOY_RIGHT)) {
            g_menu_choice^=1;
            /* SATB DMA becomes visible at the next frame boundary.  Queue the
               matching character list first, then switch the preloaded BAT
               panel at that boundary so neither half of the choice changes
               a frame before the other. */
            draw_menu_people(0);
            wait_frame();
            show_menu_back(g_menu_choice);
        }
        draw_menu_people(0);
        if (pressed&(JOY_RUN|JOY_I|JOY_II)) {
            Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
            /* The HUD reserve begins at x4.  Continue credits are a separate
               pool; do not consume a reserve merely for entering gameplay. */
            g_lives=4; g_continues=5; g_current_level=0; g_in_boss=0;
            g_luigi.lives=4;g_luigi.hearts=PLAYER_MAX_HEARTS;g_luigi.out=0;
            g_luigi.mode=PMODE_NORMAL;g_luigi.prev_buttons=0;g_luigi.fireball_ammo=3;
            g_luigi.carry_index=-1;g_luigi.lift_timer=0;g_luigi.throw_timer=0;g_luigi.crate_hide=0;
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
            int level=FADE_FULL-(int)((g_state_frame-156)*FADE_FULL/47);
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
            vdc_setreg(BACK_CHIP,VDC_REG_CR,(u16)(VDC_CR_BB|VDC_CR_SB));
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
        int level=(elapsed<24)?(int)(elapsed*FADE_FULL/24):FADE_FULL;
        int d=9-(int)((elapsed>24?(elapsed-24):0)/64);
        if (d<0) d=0;
        if (d!=g_continue_digit) g_continue_digit=d;
        set_continue_palette_fade(level);
        draw_continue_digit();
        if ((pressed&(JOY_RUN|JOY_I|JOY_II)) && elapsed>=24 && g_continues>0) {
            --g_continues;
            g_lives=4;
            if(multiplayer_active()){g_luigi.lives=4;g_luigi.out=0;g_luigi.hearts=PLAYER_MAX_HEARTS;}
            Play_Sound_Sample(SFX_MENU_CONFIRM,0,0);
            music_stop_for_transition();
            g_continue_resume=1; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
        if (elapsed>=24+10*64) {
            music_stop_for_transition();
            g_continue_resume=0; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
    } else if (g_state==STATE_CONTINUE_FADE) {
        int level=FADE_FULL-(int)(g_state_frame*FADE_FULL/32); if (level<0) level=0;
        set_continue_palette_fade(level); clear_front_sprites(); set_front_sprites_enabled(0);
        if (g_state_frame>=32) {
            Stop_Sound_Sample(SFX_MENU_CONFIRM,0);
            if (g_continue_resume) {
                /* Continue resumes where the run ended: the current level
                   from its normal spawn, or the boss room it died in. */
                if (g_in_boss) start_boss(1); else start_gameplay(1);
            } else enter_boot();
            return;
        }
    } else if (g_state==STATE_LEVEL_FADE) {
        int level=FADE_FULL-(int)(g_state_frame*FADE_FULL/32); if(level<0)level=0;
        set_game_palette_fade(level); draw_game_sprites();
        if(g_state_frame>=32){
            /* Keep the live SATB visible through the fade.  Once both the
               background and sprite palettes have reached black, blank the
               sprite plane before replacing the scene underneath it. */
            clear_front_sprites(); set_front_sprites_enabled(0);
            g_keep_ammo=1;
            if(g_after_boss){
                ++g_current_level; g_in_boss=0; g_after_boss=0;
                if(g_current_level>=19){g_keep_ammo=0;enter_staff();return;}
                start_gameplay(1); return;
            }
            if(g_boss_level_flags[g_current_level]){start_boss(1);return;}
            ++g_current_level;
            if(g_current_level>=19){g_keep_ammo=0;enter_staff();return;}
            start_gameplay(1); return;
        }
    }
}

int main(void)
{
    contrlr_pad_init(0);
    contrlr_pad_init(1);
    init_video();
    init_parallax_irq();
    audio_init();
    music_init();
    enter_boot();
#ifdef FORCE_STAFF
    /* Build with EXTRA_CFLAGS=-DFORCE_STAFF to boot straight into the roll. */
    enter_staff();
#endif

    for (;;) {
        u16 btn, pressed, btn2=0, pressed2=0;
        wait_frame();
        /* First thing in the blanking interval, before the VRAM traffic: the
           colour palette RAM is the one target the HuC6261 forbids touching
           while the picture is being displayed. */
        pal_publish();
        /* Publish queued ring rows at the same frame boundary as their scroll,
           with the data write first.  This is the only safe ordering once the
           dual-VDC SATB uploads make the preceding frame's workload variable. */
        flush_level_rows();
        if (g_state==STATE_PLAY || g_state==STATE_LEVEL_FADE) {
            commit_stage_camera();
            backdrop_parallax_arm();
        }
        else if (g_state==STATE_STAFF)
            commit_staff_scroll();
        /* Every other screen owns the whole picture, and the cellophane layer
           is anchored to the stage's rasters, so retire it on the way out. */
        if (g_state!=STATE_PLAY && g_state!=STATE_LEVEL_FADE &&
            g_state!=STATE_STAGE_PREP && g_state!=STATE_STAGE_LOAD)
            overlay_disable();
        btn = read_buttons();
        pressed = (u16)(btn & (u16)~g_prev_buttons);
        g_prev_buttons = btn;

        if(multiplayer_active()){
            btn2=read_buttons_2();
            pressed2=(u16)(btn2&(u16)~g_luigi.prev_buttons);
            g_luigi.prev_buttons=btn2;
        }

        if (g_state == STATE_PLAY) {
            update_luigi(btn2,pressed2);
            if(g_state==STATE_PLAY)update_play(btn, pressed);
        }
        else update_frontend(pressed);
        music_update();
        ++g_parallax_clock;
        ++g_state_frame;
    }
    return 0;
}
