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
 *   KING/RAINBOW unused; audio is stubbed.
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
#include "game_levels_generated.h"

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

#define FG_TILE_BASE_WORD 0x0800
#define BOSS_BG_TILE_BASE_WORD 0x1000
#define BOSS_BG_TILE_BASE_INDEX (BOSS_BG_TILE_BASE_WORD/TILE_WORDS)
#define FG_TILE_BASE_INDEX (FG_TILE_BASE_WORD / TILE_WORDS)
#define BG_TILE_BASE_WORD 0x1000
#define BG_TILE_BASE_INDEX (BG_TILE_BASE_WORD / TILE_WORDS)
#define BOOT_TILE_BASE_WORD 0x1000
#define BOOT_TILE_BASE_INDEX (BOOT_TILE_BASE_WORD / TILE_WORDS)
#define FRONT_TILE_BASE_WORD 0xD000

#define PLAYER_BASE_WORD 0x4800
/* Boot-only character banks live below the dual-title CHR area and do not
   overlap each other.  r12k-r12m incorrectly reused PLAYER_BASE_WORD and
   LUIGI_BASE_WORD; Luigi's 0x1E00-word upload at 0x4000 overwrote Mario at
   0x4800, which made early Mario frames fetch Luigi patterns. */
#define BOOT_MARIO_BASE_WORD 0x1000
#define BOOT_LUIGI_BASE_WORD 0x3000
#define THROW_BASE_WORD  0x6600
#define MENU_MARIO_BASE_WORD 0x8400
#define MENU_LUIGI_BASE_WORD 0x8A00
#define PUSH_BASE_WORD   0x5E00
#define STAR_BASE_WORD   0x5F80
#define COIN_BASE_WORD   0x6000
#define HUD_BASE_WORD    0x6A00
#define CRATE_BASE_WORD  0x6800
#define GEM_BASE_WORD    0x6900
#define ENEMY_BASE_WORD  0x7400
#define FIREBALL_BASE_WORD 0x6E00
#define HUD_HEART_BASE_WORD 0x7000
#define HUD_DIGIT_BASE_WORD 0x7040
/* Level-2 moving platform pattern: 192 words at 0x7300..0x73bf,
 * deliberately between HUD digits (ending 0x72bf) and enemies (0x7400). */
#define PLATFORM_BASE_WORD 0x7300
#define CONTINUE_TILE_BASE_WORD 0x2000
#define CONTINUE_TILE_BASE_INDEX (CONTINUE_TILE_BASE_WORD / TILE_WORDS)
#define CONTINUE_DIGIT_BASE_WORD 0xA000
#define SATB_BASE0       0xF000
#define SATB_BASE1       0xF200

#define PLAYER_FRAME_WORDS_LOCAL 384
#define PLAYER_STRIP_WORDS 128
#define BOOT_PLAYER_BANK_WORDS (PLAYER_FRAME_COUNT * PLAYER_FRAME_WORDS_LOCAL)
#if (BOOT_MARIO_BASE_WORD + BOOT_PLAYER_BANK_WORDS) > BOOT_LUIGI_BASE_WORD
#error "boot Mario/Luigi VRAM banks overlap"
#endif
#if (BOOT_LUIGI_BASE_WORD + BOOT_PLAYER_BANK_WORDS) > PUSH_BASE_WORD
#error "boot Luigi bank overlaps title sprite assets"
#endif
#define HUD_CELL_WORDS 64
#define ENEMY_VARIANT_WORDS ENEMY_VARIANT_VRAM_WORDS
#define BOSS_BASE_WORD 0x7400
#define BOSS_FLAME_BASE_WORD (BOSS_BASE_WORD + BOWSER_FRAME_WORDS * BOWSER_FRAME_COUNT)
#define BOSS_AUX_BASE_WORD 0xC500
#define BOSS_RING_BASE_WORD BOSS_AUX_BASE_WORD
#define BOSS_HAMMER_BASE_WORD (BOSS_AUX_BASE_WORD+256)
#define BOSS_POT_BASE_WORD (BOSS_AUX_BASE_WORD+512)
#define BOSS_BELL_BASE_WORD (BOSS_AUX_BASE_WORD+768)
#define BOSS_AUX_SPR_PAL 12
#define BOSS_ITEM_SPR_PAL 11
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
#define MAX_MOVING_PLATFORMS 3
#define MOVING_PLATFORM_W 48
#define MOVING_PLATFORM_H 16
#define PLAYER_MAX_HEARTS 5
#define HURT_FRAMES 60
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
    unsigned char flags;
    unsigned char defeated;
    unsigned char flying;
    unsigned char flight_phase;
} Enemy;

typedef struct {
    s32 x, y, vx;
    unsigned char active;
    unsigned char life;
} Fireball;

typedef struct {
    s32 x, y;
    short min_y, max_y;
    signed char dir;
    unsigned char speed;
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
static int g_camera_tile;
static int g_satb_page;
static int g_player_anim_clock;
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
static int g_in_boss=0;
static int g_after_boss=0;
static s32 g_boss_x,g_boss_y,g_boss_vx,g_boss_vy;
static int g_boss_hp,g_boss_timer,g_boss_invuln,g_boss_defeated;
static int g_boss_class,g_boss_phase,g_boss_face=1,g_boss_ground_y;
static int g_boss_action,g_boss_action_timer,g_boss_action_dir;
static s32 g_boss_shot_x,g_boss_shot_y,g_boss_shot_vx,g_boss_shot_vy;
static int g_boss_shot_active;

static Crate g_crates[MAX_CRATES];
static Gem g_gems[MAX_GEMS];
static Enemy g_enemies[MAX_ENEMIES];
static Fireball g_fireballs[MAX_FIREBALLS];
static MovingPlatform g_moving_platforms[MAX_MOVING_PLATFORMS];
static int g_moving_platform_count;
static int g_on_moving_platform=-1;
static int g_drop_platform_timer;
static int g_debug_mode;
static int g_crate_hide;

static void enter_menu(void);
static void enter_boot(void);
static void start_boss(int fade_in);
static void submit_front_satb(u16 *satb);
static void clear_front_sprites(void);
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

static void show_black(void)
{
    int i;
    clear_bat(BACK_CHIP, BG_BAT_WORDS);
    clear_bat(FRONT_CHIP, FG_BAT_WORDS);
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
    int i,j,uniq=0;
    const unsigned short *seen[12];
    /* Keep a dedicated copy of the player palette for boss rooms. Normal
       stages use source palette line 0 directly; every generated level carries
       the same Mario/object colors there. */
    for (i=0;i<16;++i) tetsu_set_palette((u16)(PLAYER_SPR_PAL*16+i),g_player_palette[i]);

    vdc_set_vram_write(FRONT_CHIP, PLAYER_BASE_WORD);
    for (i=0;i<PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS_LOCAL;++i) vdc_vram_write(FRONT_CHIP,g_player_frame_words[i]);
    vdc_set_vram_write(FRONT_CHIP,THROW_BASE_WORD);
    for (i=0;i<256;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,(u16)(THROW_BASE_WORD+256));
    for (i=256;i<320;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,(u16)(THROW_BASE_WORD+384));
    for (i=320;i<384;++i) vdc_vram_write(FRONT_CHIP,g_player_death_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_BASE_WORD); for(i=0;i<896;++i)vdc_vram_write(FRONT_CHIP,g_hud_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,CRATE_BASE_WORD); for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_crate_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,GEM_BASE_WORD); for(i=0;i<192;++i)vdc_vram_write(FRONT_CHIP,g_gem_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,FIREBALL_BASE_WORD); for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_fireball_sprite_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_HEART_BASE_WORD); for(i=0;i<64;++i)vdc_vram_write(FRONT_CHIP,g_hud_heart_words[i]);
    vdc_set_vram_write(FRONT_CHIP,HUD_DIGIT_BASE_WORD); for(i=0;i<640;++i)vdc_vram_write(FRONT_CHIP,g_hud_digit_words[i]);
    if ((!g_in_boss && (g_current_level==1 || g_current_level==5)) || (g_in_boss&&g_boss_class==3)) {
        const u16 *platform_words=(g_current_level==5||g_in_boss)?g_level6_platform_words:g_level2_platform_words;
        vdc_set_vram_write(FRONT_CHIP,PLATFORM_BASE_WORD);
        for(i=0;i<192;++i)vdc_vram_write(FRONT_CHIP,platform_words[i]);
    }

    /* F3AE/F3E4 in the 68000 disassembly load an enemy palette from F426 ->
       72338 and a 0x180-byte graphics chunk selected through 7246C. The
       generated table reproduces those combinations. Only variants referenced
       by the current level are copied to VDC1. */
    if (!g_in_boss && g_level) {
        for(i=0;i<g_level->variant_count;++i) {
            int gi=g_level->variant_ids[i]; const EnemyVariantData *v=&g_enemy_variants[gi]; int line=-1;
            for(j=0;j<uniq;++j) if(palettes_equal(seen[j],v->palette)){ line=4+j; break; }
            if(line<0){ seen[uniq]=v->palette; line=4+uniq; for(j=0;j<16;++j)tetsu_set_palette((u16)(line*16+j),v->palette[j]); ++uniq; }
            g_enemy_palette_line[i]=(unsigned char)line;
            vdc_set_vram_write(FRONT_CHIP,(u16)(ENEMY_BASE_WORD+i*ENEMY_VARIANT_VRAM_WORDS));
            for(j=0;j<ENEMY_VARIANT_VRAM_WORDS;++j)vdc_vram_write(FRONT_CHIP,v->words[j]);
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
    if(g_in_boss){ const unsigned short *sp=bv?bv->sprite_palette:g_bowser_palette;for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_SPR_PAL*16+i),fade_yuv(sp[i],level)); }
    if(g_in_boss&&g_boss_class>0)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_AUX_SPR_PAL*16+i),fade_yuv(g_final_aux_palette[i],level));
    if(g_in_boss&&g_boss_class>0)for(i=0;i<16;++i)tetsu_set_palette((u16)(BOSS_ITEM_SPR_PAL*16+i),fade_yuv(g_boss_item_palette[i],level));
    else if(g_level){
        for(i=0;i<g_level->variant_count;++i){
            const EnemyVariantData *v=&g_enemy_variants[g_level->variant_ids[i]]; int line=-1;
            for(j=0;j<uniq;++j)if(palettes_equal(seen[j],v->palette)){line=4+j;break;}
            if(line<0){seen[uniq]=v->palette;line=4+uniq;++uniq;}
            g_enemy_palette_line[i]=(unsigned char)line;
            for(j=0;j<16;++j)tetsu_set_palette((u16)(line*16+j),fade_yuv(v->palette[j],level));
        }
    }
    g_game_fade_level=level;
}

static u16 level_fg_bat_entry(int world_col,int row)
{
    int screen,seg,lx;
    if(!g_level||!g_stage_set||world_col<0||world_col>=g_level_width_tiles||row<0||row>=30)return FG_TILE_BASE_INDEX;
    screen=world_col>>5; lx=world_col&31; if(screen>=g_level->route_len)return FG_TILE_BASE_INDEX;
    seg=g_level->route[screen]; if(seg==255||seg>=g_stage_set->segment_count)return FG_TILE_BASE_INDEX;
    return relocate_bat(g_stage_set->segment_bat[(seg*30+row)*32+lx],FG_TILE_BASE_INDEX);
}

static void stream_level_column(int world_col)
{
    int row,map_col=world_col&(FG_BAT_W-1); if(world_col<0||world_col>=g_level_width_tiles)return;
    for(row=0;row<30;++row){vdc_set_vram_write(FRONT_CHIP,(u16)(row*FG_BAT_W+map_col));vdc_vram_write(FRONT_CHIP,level_fg_bat_entry(world_col,row));}
}

static void upload_level_back(void)
{
    int x,y;
    vdc_set_vram_write(BACK_CHIP,FG_TILE_BASE_WORD);
    for(x=0;x<g_stage_set->tile_count*TILE_WORDS;++x)vdc_vram_write(BACK_CHIP,g_stage_set->tile_words[x]);
    for(y=0;y<30;++y){
        vdc_set_vram_write(BACK_CHIP,(u16)(y*BG_BAT_W));
        /* The background plane is the Mega Drive's full 64-cell width; it
           wraps every 512 pixels, not every 256. */
        for(x=0;x<64;++x){u16 e=g_level->bg_bat[y*BG_BAT_W+x];vdc_vram_write(BACK_CHIP,relocate_bat(e,FG_TILE_BASE_INDEX));}
    }
}

static void init_level_ring(void)
{
    int col,i;
    g_level=&g_game_levels[g_current_level]; g_stage_set=&g_stage_sets[g_level->set_index]; g_level_width_tiles=g_level->route_len*32;
    clear_bat(BACK_CHIP,BG_BAT_WORDS); clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    upload_level_back();
    vdc_set_vram_write(FRONT_CHIP,FG_TILE_BASE_WORD);
    for(i=0;i<g_stage_set->tile_count*TILE_WORDS;++i)vdc_vram_write(FRONT_CHIP,g_stage_set->tile_words[i]);
    for(col=0;col<FG_BAT_W;++col)stream_level_column(col);
    g_camera_x=0;g_camera_tile=0;vdc_set_scroll(BACK_CHIP,0,0);vdc_set_scroll(FRONT_CHIP,0,0);
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
    if(!g_level||tx<0||tx>=g_level_width_tiles)return 1;if(ty<0)return 0;if(ty>=32)return 0;
    screen=tx>>5;lx=tx&31;if(screen>=g_level->route_len)return 1;seg=g_level->route[screen];if(seg==255||seg>=g_stage_set->segment_count)return 0;
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
            int px=(int)(p->x>>FIX_SHIFT), top=(int)(p->y>>FIX_SHIFT);
            if (bx+PLAYER_BODY_W-3<=px || bx+3>=px+MOVING_PLATFORM_W) continue;
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
 * Full hold rises 75 pixels; releasing I during the rising half mirrors into
 * the falling half, reproducing the original short-jump behaviour. */
static const signed char g_md_jump_dy[45]={
    -6,-6,-5,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
    -2,-2,-2,-2,-2,-2,-2,0,0,0,2,2,2,2,2,2,2,
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
        g_camera_x=0;g_camera_tile=0;
        vdc_set_scroll(FRONT_CHIP,0,0);
        /* Handler $940E continuously advances the final room's wall plane;
           its collision/platform plane remains fixed. */
        vdc_set_scroll(BACK_CHIP,0,(g_boss_class==5)?(u16)((g_boss_timer>>2)%224):0);
        return;
    }
    int px=(int)(g_player_x>>FIX_SHIFT);
    int max_camera=g_level_width_tiles*8-SCREEN_W;
    int new_tile;

    /* The MD stage camera does not re-center on Mario every frame. Keep Mario
       inside a forward dead-zone and never pull the map backwards because an
       action temporarily stops him. This also prevents world-object snapping
       on throw/reversal frames. */
    if (!g_camera_hold && px-g_camera_x>112)
        g_camera_x=px-112;
    if (g_camera_x<0) g_camera_x=0;
    if (g_camera_x>max_camera) g_camera_x=max_camera;

    new_tile=g_camera_x>>3;
    while (g_camera_tile<new_tile) {
        ++g_camera_tile;
        stream_level_column(g_camera_tile+FG_BAT_W-1);
    }
    while (g_camera_tile>new_tile) {
        --g_camera_tile;
        stream_level_column(g_camera_tile);
    }
    vdc_set_scroll(FRONT_CHIP,(u16)(g_camera_x&0x01FF),0);
    vdc_set_scroll(BACK_CHIP,(u16)((g_camera_x>>1)&0x01FF),0);
}


static void init_moving_platforms(void)
{
    MovingPlatform *p;
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
    }
}

static void update_moving_platforms(void)
{
    int i;
    for (i=0;i<g_moving_platform_count;++i) {
        MovingPlatform *p=&g_moving_platforms[i];
        s32 old_y=p->y;
        p->y+=(s32)p->dir*(s32)p->speed*FIX_ONE;
        if ((int)(p->y>>FIX_SHIFT)<=p->min_y) {
            p->y=(s32)p->min_y<<FIX_SHIFT; p->dir=1;
        } else if ((int)(p->y>>FIX_SHIFT)>=p->max_y) {
            p->y=(s32)p->max_y<<FIX_SHIFT; p->dir=-1;
        }
        if (g_on_moving_platform==i && g_grounded) g_player_y+=p->y-old_y;
    }
}

static void init_objects(void)
{
    int i,ycrop=(!g_in_boss&&g_current_level==2)?8:0;
    for(i=0;i<MAX_CRATES;++i){g_crates[i].active=0;g_crates[i].carried=0;g_crates[i].thrown=0;}
    for(i=0;i<MAX_GEMS;++i)g_gems[i].active=0;
    for(i=0;i<MAX_ENEMIES;++i)g_enemies[i].active=0;
    if(!g_in_boss && g_level){
        for(i=0;i<g_level->crate_count && i<MAX_CRATES;++i){
            g_crates[i].x=(s32)g_level->crates[i*3]<<FIX_SHIFT;g_crates[i].y=(s32)(g_level->crates[i*3+1]-ycrop)<<FIX_SHIFT;
            g_crates[i].vx=g_crates[i].vy=0;g_crates[i].extra=(unsigned char)g_level->crates[i*3+2];g_crates[i].active=1;g_crates[i].flight_timer=0;
        }
        for(i=0;i<g_level->gem_count && i<MAX_GEMS;++i){g_gems[i].x=g_level->gems[i*2];g_gems[i].y=g_level->gems[i*2+1]-ycrop;g_gems[i].active=1;}
        for(i=0;i<g_level->enemy_count && i<MAX_ENEMIES;++i){
            const short *e=&g_level->enemies[i*7];
            g_enemies[i].x=(s32)e[0]<<FIX_SHIFT;g_enemies[i].y=(s32)(e[1]-ycrop)<<FIX_SHIFT;g_enemies[i].vx=-FIX_ONE;g_enemies[i].vy=0;
            g_enemies[i].variant=(unsigned char)e[2];g_enemies[i].state=(unsigned char)e[3];g_enemies[i].flags=(unsigned char)e[4];
            g_enemies[i].width=(unsigned char)e[5];g_enemies[i].height=(unsigned char)e[6];g_enemies[i].active=1;g_enemies[i].spawned=0;g_enemies[i].defeated=0;
            /* Record word 9 is the original handler state. State 18 dispatches
               to $B794 (flying Koopas); state 17 dispatches to $B7D2 and is
               used by grounded enemies, including every Goomba variant. */
            g_enemies[i].flying=(e[3]==18);
            g_enemies[i].flight_phase=0;
        }
    }
    init_moving_platforms();
    g_carry_index=-1;g_lift_timer=0;g_gem_count=0;g_invuln=0;
    g_crate_hide=0;
    for(i=0;i<MAX_FIREBALLS;++i)g_fireballs[i].active=0;
}

static void update_crates(void)
{
    int i;
    for (i=0;i<MAX_CRATES;++i) {
        Crate *c=&g_crates[i];
        int x,y,bottom;
        if (!c->active) continue;
        if (c->carried) {
            c->x=g_player_x+(s32)4*FIX_ONE;
            c->y=g_player_y+(s32)(g_crate_hide?18:-13)*FIX_ONE;
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
            sy=(int)(c->y>>FIX_SHIFT);
            if ((c->vx>0 && sx>256) || (c->vx<0 && sx+CRATE_W<0) ||
                sy+CRATE_H<0 || sy>240)
                c->active=0;
            continue;
        }

        x=(int)(c->x>>FIX_SHIFT);
        if (x<g_camera_x-48 || x>g_camera_x+320) continue;
        c->vx=0;
        c->x=(c->x>>FIX_SHIFT)<<FIX_SHIFT;
        c->vy+=80;
        if (c->vy>5*FIX_ONE) c->vy=5*FIX_ONE;
        c->y+=c->vy;
        x=(int)(c->x>>FIX_SHIFT); y=(int)(c->y>>FIX_SHIFT);
        bottom=y+CRATE_H-1;
        if (c->vy>=0 && (solid_v(x+3,bottom)||solid_v(x+CRATE_W-4,bottom))) {
            int top=(bottom>>3)<<3;
            c->y=(s32)(top-CRATE_H)<<FIX_SHIFT;
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

static void do_crate_action(void)
{
    if (g_carry_index >= 0) {
        Crate *c = &g_crates[g_carry_index];
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
        Play_Sound_Sample(2,0,0);
    } else {
        int i = find_pickup_crate();
        if (i >= 0) {
            g_carry_index = i;
            g_crates[i].carried = 1;
            g_crates[i].thrown = 0;
            g_lift_timer = 18;
            g_vx = 0;
            Play_Sound_Sample(2,0,0);
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
    Play_Sound_Sample(6,0,0);
}

static void defeat_enemy(Enemy *e)
{
    if (!e->active || e->defeated) return;
    e->defeated=1;
    e->vx=0;
    e->vy=FIX_ONE;
    Play_Sound_Sample(4,0,0);
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
            Play_Sound_Sample(3,0,0);
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
    Play_Sound_Sample(5,0,0);
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
       death.  The real sample is intentionally still stubbed in this port. */
    Play_Sound_Sample(7,0,0);
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
        h=e->height;

        if (e->defeated) {
            /* Defeated enemies remain visible and simply drop out of the
               raster. They no longer collide with terrain/player. */
            e->vy+=48;
            if (e->vy>6*FIX_ONE) e->vy=6*FIX_ONE;
            e->y+=e->vy;
            if ((int)(e->y>>FIX_SHIFT)>256) e->active=0;
            continue;
        }

        /* The source only advances encounter objects close to the raster.
           Keep the same generous pre-roll: 48 px left, 64 px right. */
        if (!e->spawned) {
            if (x>g_camera_x+320) continue;
            e->spawned=1;
        }
        if (x<g_camera_x-48 || x>g_camera_x+320) continue;

        if (e->flying) {
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
            if (solid_h(side,y+8)||solid_h(side,y+h-3))
                e->vx=-e->vx;
            else
                e->x=nx;
        }

        e->y+=e->vy;
        x=(int)(e->x>>FIX_SHIFT);
        y=(int)(e->y>>FIX_SHIFT);
        bottom=y+h-1;
        if (e->vy>=0 && (solid_v(x+3,bottom)||solid_v(x+e->width-4,bottom))) {
            int top=(bottom>>3)<<3;
            e->y=(s32)(top-h)<<FIX_SHIFT;
            e->vy=0;
            y=top-h;
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
            } else if (g_invuln==0 &&
                       aabb(px+4,py+5,8,PLAYER_BODY_H-10,
                            x+4,y+5,e->width-8,h-10)) {
                hurt_player();
            }
        }
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

static void draw_game_sprites(void)
{
    static u16 satb[64 * 4];
    int i, n = 0;
    int frame = current_player_frame();
    int px = (int)(g_player_x >> FIX_SHIFT) - g_camera_x;
    int py = (int)(g_player_y >> FIX_SHIFT);
    u16 base = (u16)(g_satb_page ? SATB_BASE1 : SATB_BASE0);
    /* Every normal-level palette carries the original Mario/object colors in
       source line 0 (identical across all 19 level palettes).  r11 moved
       normal sprites to line 13 and produced the observed yellow/green Mario
       regression.  Keep the separate line only for boss rooms, where the
       arena uses lines 0..2. */
    u16 player_pal=(u16)(g_in_boss?PLAYER_SPR_PAL:0);
    u16 pctrl = (u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_1 | player_pal);
    u16 paddr = (u16)(PLAYER_BASE_WORD + (frame-1) * PLAYER_FRAME_WORDS_LOCAL);
    int player_visible=1;
    int hud_yoff=(g_in_boss||(!g_in_boss&&g_current_level==2))?-16:0;

    for (i = 0; i < 64 * 4; ++i) satb[i] = 0;

    /* Keep the HUD at the front of SATB so object-heavy screens cannot make
       the live hearts/ammo counter disappear.  The static cells now contain
       only the life icon/x4 and fireball icon; health/ammo are independent. */
    for (i = 0; i < 14 && n < 64; ++i) {
        if (g_hud_cell_mask[i]) {
            int cx = i % 7, cy = i / 7;
            set_sprite_entry(satb,n++,cx*16,209+hud_yoff+cy*16,
                (u16)(HUD_BASE_WORD + i*HUD_CELL_WORDS),(u16)(VDC_SPR_PRIO_SP|player_pal));
        }
    }
    /* Source HUD life icon and multiplication sign are static; the digit is
       now live instead of the baked x4 from the runtime capture. */
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
            sx = g_gems[i].x - g_camera_x; sy = g_gems[i].y;
            if (sx < -16 || sx > 255 || sy < -16 || sy > 239) continue;
            set_sprite_entry(satb,n++,sx,sy,(u16)(GEM_BASE_WORD + gf*64),(u16)(VDC_SPR_PRIO_SP|player_pal));
        }
    }

    for (i = 0; i < MAX_CRATES && n < 54; ++i) {
        int sx, sy;
        if (!g_crates[i].active) continue;
        sx = (int)(g_crates[i].x >> FIX_SHIFT) - g_camera_x;
        sy = (int)(g_crates[i].y >> FIX_SHIFT);
        if (sx < -32 || sx > 255 || sy < -32 || sy > 239) continue;
        {u16 addr=CRATE_BASE_WORD,pal=player_pal;if(g_in_boss){if(g_crates[i].extra==1){addr=BOSS_POT_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;}else if(g_crates[i].extra==2){addr=BOSS_BELL_BASE_WORD;pal=BOSS_ITEM_SPR_PAL;}else if(g_crates[i].extra==3){addr=BOSS_HAMMER_BASE_WORD;pal=BOSS_AUX_SPR_PAL;}}
        set_sprite_entry(satb,n++,sx,sy,addr,(u16)(VDC_SPR_PRIO_SP | VDC_SPR_X_WIDTH_2 | VDC_SPR_Y_HEIGHT_2 | pal));}
    }

    /* Traversal-critical Level-2 moving platforms get SATB priority over
       enemies so a busy screen cannot make the floor disappear. */
    for (i=0;i<g_moving_platform_count && n<58;++i) {
        int sx=(int)(g_moving_platforms[i].x>>FIX_SHIFT)-g_camera_x;
        int sy=(int)(g_moving_platforms[i].y>>FIX_SHIFT);
        if (sx<-MOVING_PLATFORM_W || sx>255 || sy<-MOVING_PLATFORM_H || sy>239) continue;
        set_sprite_entry(satb,n++,sx,sy,PLATFORM_BASE_WORD,
            (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2));
        if(n<58)set_sprite_entry(satb,n++,sx+32,sy,(u16)(PLATFORM_BASE_WORD+128),VDC_SPR_PRIO_SP);
    }

    for (i=0;i<MAX_ENEMIES && n<58;++i) {
        int sx,sy,anim,li,gi,cx,cy,cols,rows; unsigned mask; u16 flip;
        Enemy *e=&g_enemies[i]; const EnemyVariantData *v;
        if(!e->active||!e->spawned)continue;
        sx=(int)(e->x>>FIX_SHIFT)-g_camera_x;sy=(int)(e->y>>FIX_SHIFT);
        if(sx<-56||sx>255||sy<-56||sy>239)continue;
        li=e->variant;if(li>=g_level->variant_count)continue;gi=g_level->variant_ids[li];v=&g_enemy_variants[gi];
        anim=(int)((g_state_frame/8)&1);mask=anim?v->mask1:v->mask0;cols=(e->width+15)>>4;rows=(e->height+15)>>4;flip=(e->vx>0)?VDC_SPR_X_INVERT:0;
        for(cy=0;cy<rows && n<61;++cy)for(cx=0;cx<cols && n<61;++cx){
            int cell=cy*3+cx,dx;
            if(!(mask&(1u<<cell)))continue;dx=(e->vx>0)?(cols-1-cx)*16:cx*16;
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(ENEMY_BASE_WORD+li*ENEMY_VARIANT_VRAM_WORDS+anim*ENEMY_FRAME_WORDS+cell*64),(u16)(VDC_SPR_PRIO_SP|flip|g_enemy_palette_line[li]));
        }
    }

    if(g_in_boss && (g_boss_hp>0||g_boss_defeated) && n<58){
        /* Hurt includes an impact puff four pixels above the body, so its
           capture origin needs +4 relative to the old value to keep Bowser's
           feet aligned with the neighboring walk poses. */
        static const signed char pose_y_offset[9]={18,22,22,18,16,22,22,16,12};
        int bx=(int)(g_boss_x>>FIX_SHIFT),by=(int)(g_boss_y>>FIX_SHIFT),anim,cx,cy;
        unsigned long long mask;
        u16 flip=(g_boss_face<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            const BossVisualData *v=&g_later_boss_visuals[g_boss_class-1];
            mask=v->sprite_mask;
            for(cy=0;cy<v->rows && n<62;++cy)for(cx=0;cx<v->cols && n<62;++cx){
                int cell=cy*v->cols+cx,dx,dy=by+cy*16;if(!(mask&(1ULL<<cell)))continue;
                dx=(g_boss_face<0)?(v->cols-1-cx)*16:cx*16;
                /* Slot 13 is two source objects: the frog jumps away while
                   the treaded vehicle remains on the middle carrier. */
                if(g_boss_class==3&&cy>=4){dx+=(96-bx);dy=112+(cy-4)*16;}
                set_sprite_entry(satb,n++,bx+dx,dy,
                    (u16)(BOSS_BASE_WORD+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));
            }
        }else{
        /* The real capture has four distinct walk cells and a separate
           forward-leaning breath cell. State 3 is a solid fixed hurt pose;
           the Mega Drive does not flicker the boss during this pause. */
        if(g_boss_invuln>0)anim=8;
        else if(g_boss_class==0&&(g_boss_action==1||g_boss_action==7||g_boss_action==9))
            anim=(g_boss_action_timer<8)?4:(5+(((g_boss_action_timer-8)/8)&1));
        else anim=(g_boss_timer/10)&3;
        by+=pose_y_offset[anim]-16;
        mask=g_bowser_masks[anim];
        for(cy=0;cy<6 && n<62;++cy)for(cx=0;cx<6 && n<62;++cx){int cell=cy*6+cx,dx;if(!(mask&(1ULL<<cell)))continue;dx=(g_boss_face<0)?(5-cx)*16:cx*16;set_sprite_entry(satb,n++,bx+dx,by+cy*16,(u16)(BOSS_BASE_WORD+anim*BOWSER_FRAME_WORDS+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));}
        }
    }

    if(g_in_boss && g_boss_shot_active && n<63){
        int sx=(int)(g_boss_shot_x>>FIX_SHIFT),sy=(int)(g_boss_shot_y>>FIX_SHIFT),cx,cy;
        u16 flip=(g_boss_shot_vx<0)?VDC_SPR_X_INVERT:0;
        if(g_boss_class>0){
            if(g_boss_class==4||g_boss_class==5){
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
            set_sprite_entry(satb,n++,sx+dx,sy+cy*16,(u16)(BOSS_FLAME_BASE_WORD+cell*64),(u16)(VDC_SPR_PRIO_SP|BOSS_SPR_PAL|flip));
        }
        }
    }

    for (i=0;i<MAX_FIREBALLS && n<64;++i) {
        int sx,sy;
        u16 ctrl=(u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|player_pal);
        if (!g_fireballs[i].active) continue;
        sx=(int)(g_fireballs[i].x>>FIX_SHIFT)-g_camera_x;
        sy=(int)(g_fireballs[i].y>>FIX_SHIFT);
        if (sx<-32 || sx>255 || sy<-32 || sy>239) continue;
        if (g_fireballs[i].vx<0) ctrl|=VDC_SPR_X_INVERT;
        set_sprite_entry(satb,n++,sx,sy,FIREBALL_BASE_WORD,ctrl);
    }

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
    /* Large countdown digit uses dedicated sprite palette line 15. */
    for (i=0;i<16;++i)
        tetsu_set_palette((u16)(240+i),fade_yuv(g_continue_digit_palette[i],level));
}

static void draw_continue_digit(void)
{
    static u16 satb[64*4];
    int i,d=g_continue_digit;
    for (i=0;i<64*4;++i) satb[i]=0;
    if (d<0) d=0; if (d>9) d=9;
    set_sprite_entry(satb,0,116,152,(u16)(CONTINUE_DIGIT_BASE_WORD+d*256),
        (u16)(VDC_SPR_PRIO_SP|VDC_SPR_X_WIDTH_2|VDC_SPR_Y_HEIGHT_2|15));
    submit_front_satb(satb);
}

static void load_continue_hidden(void)
{
    int x,y,i;
    /* Never expose a half-loaded BAT/CHR set. Both VDC outputs remain disabled
       until the following TETSU frame boundary. */
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_bat(BACK_CHIP,BG_BAT_WORDS);
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
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
    g_in_boss=1; g_level=&g_game_levels[g_current_level]; g_level_width_tiles=32;
    g_boss_class=boss_class_for_level(g_current_level);bv=(g_boss_class>0)?&g_later_boss_visuals[g_boss_class-1]:0;br=bv?0:&g_bowser_rooms[(g_current_level==5)?1:0];
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    clear_bat(BACK_CHIP,BG_BAT_WORDS);clear_bat(FRONT_CHIP,FG_BAT_WORDS);
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
    vdc_set_vram_write(FRONT_CHIP,BOSS_BASE_WORD);
    if(bv)for(i=0;i<bv->cols*bv->rows*64;++i)vdc_vram_write(FRONT_CHIP,bv->sprite_words[i]);
    else{for(i=0;i<BOWSER_FRAME_WORDS*BOWSER_FRAME_COUNT;++i)vdc_vram_write(FRONT_CHIP,g_bowser_words[i]);for(i=0;i<384;++i)vdc_vram_write(FRONT_CHIP,g_bowser_flame_words[i]);}
    if(bv){vdc_set_vram_write(FRONT_CHIP,BOSS_AUX_BASE_WORD);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_final_ring_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_final_hammer_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_pot_words[i]);for(i=0;i<256;++i)vdc_vram_write(FRONT_CHIP,g_boss_bell_words[i]);}
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
    g_boss_x=(s32)x<<FIX_SHIFT;g_boss_y=(s32)y<<FIX_SHIFT;
    g_boss_ground_y=y;g_boss_vx=-2*FIX_ONE;g_boss_vy=0;
    g_boss_phase=0;g_boss_face=-1;
    g_boss_action=0;g_boss_action_timer=0;g_boss_action_dir=0;

    /* Exact 1P HP table $6D6E: 8,8,10,10,12,12,12 for the seven bosses.
       $6D94 is also exported for the later two-player implementation. */
    g_boss_hp=(int)g_boss_hp_1p[g_current_level];
    if(g_boss_hp<=0)g_boss_hp=8;
    g_boss_timer=0;g_boss_invuln=0;g_boss_defeated=0;g_boss_shot_active=0;g_boss_shot_vy=0;g_after_boss=0;
    g_player_mode=fade_in?PMODE_FADE_IN:PMODE_NORMAL;set_game_palette_fade(fade_in?0:16);draw_game_sprites();
    g_state=STATE_STAGE_LOAD;g_state_frame=0;
}

static void boss_take_hit(void)
{
    if(g_boss_invuln||g_boss_defeated||g_boss_hp<=0)return;
    --g_boss_hp;Play_Sound_Sample(4,0,0);
    if(g_boss_hp<=0){g_boss_defeated=1;g_boss_vx=0;g_boss_vy=-5*FIX_ONE;g_boss_shot_active=0;}
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
        static const unsigned char action[12]={1,5,2,6,3,4,1,5,2,5,3,4};
        static const unsigned char direction[12]={0,1,1,1,0,1,0,1,1,0,0,1};
        int a;
        if(g_boss_action_timer==0){
            a=action[g_boss_action];g_boss_action_dir=direction[g_boss_action];
            g_boss_face=g_boss_action_dir?1:-1;
        }
        a=action[g_boss_action];++g_boss_action_timer;
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
    case 1: /* level 8: diagonal 44-frame traversals plus faster 4px dives */
        if((t%44)==0){g_boss_phase=(g_boss_phase+1)&3;g_boss_vx=(g_boss_phase&1)?2*FIX_ONE:-2*FIX_ONE;g_boss_vy=(g_boss_phase&2)?FIX_ONE:-FIX_ONE;}
        if((t%176)>=132){g_boss_vx=(g_boss_vx<0)?-4*FIX_ONE:4*FIX_ONE;g_boss_vy=4*FIX_ONE;}
        g_boss_x+=g_boss_vx;g_boss_y+=g_boss_vy;bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT);
        if(bx<40){g_boss_x=(s32)40<<FIX_SHIFT;g_boss_vx=2*FIX_ONE;}if(bx>216){g_boss_x=(s32)216<<FIX_SHIFT;g_boss_vx=-2*FIX_ONE;}
        if(by<72){g_boss_y=(s32)72<<FIX_SHIFT;g_boss_vy=FIX_ONE;}if(by>176){g_boss_y=(s32)176<<FIX_SHIFT;g_boss_vy=-FIX_ONE;}
        g_boss_face=(g_boss_vx<0)?-1:1;if((t%104)==0)boss_fire_at_player(4);
        break;
    case 2: /* level 10: whole-screen sweeps visible in $886E/$88A2 */
        if((t%160)==0){g_boss_phase^=1;g_boss_vx=g_boss_phase?2*FIX_ONE:-2*FIX_ONE;}
        g_boss_x+=g_boss_vx;bx=(int)(g_boss_x>>FIX_SHIFT);
        if(bx<-88){g_boss_x=(s32)-88*FIX_ONE;g_boss_vx=2*FIX_ONE;}if(bx>332){g_boss_x=(s32)332*FIX_ONE;g_boss_vx=-2*FIX_ONE;}
        g_boss_face=(g_boss_vx<0)?-1:1;if((t%80)==0)boss_fire_at_player(4);
        break;
    case 3: { /* level 13/$8CEC: wait on either carrier, then cross by arc */
        int q=t%160,from=(t/160)&1,to=from^1,step;
        int fx=(int)(g_moving_platforms[from].x>>FIX_SHIFT),fy=(int)(g_moving_platforms[from].y>>FIX_SHIFT)-64;
        int tx=(int)(g_moving_platforms[to].x>>FIX_SHIFT),ty=(int)(g_moving_platforms[to].y>>FIX_SHIFT)-64;
        if(q<48){bx=fx;by=fy;}
        else if(q<112){step=q-48;bx=fx+(tx-fx)*step/64;by=fy+(ty-fy)*step/64-(64-(iabs_i(step-32)*2));}
        else{bx=tx;by=ty;if(q==120)boss_fire_at_player(4);}
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
        g_boss_y=(s32)49<<FIX_SHIFT;g_boss_face=(g_player_x<g_boss_x)?-1:1;
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
        if((int)(g_boss_y>>FIX_SHIFT)>256){g_after_boss=1;g_state=STATE_LEVEL_FADE;g_state_frame=0;}
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
    /* The source dispenser ($7958) waits while its previous crate object is
       live.  Do the same so unused ammunition cannot carpet the catwalk. */
    if(g_boss_invuln==0&&(g_boss_timer%120)==1){
        int any_crate=0;
        for(i=0;i<MAX_CRATES;++i)if(g_crates[i].active){any_crate=1;break;}
        if(!any_crate)for(i=0;i<MAX_CRATES;++i)if(!g_crates[i].active){
                g_crates[i].active=1;g_crates[i].carried=0;g_crates[i].thrown=0;
                g_crates[i].extra=(g_boss_class==1)?1:(g_boss_class==4)?2:(g_boss_class==5)?3:0;
                g_crates[i].x=(s32)(32+((g_boss_timer/120)%6)*32)<<FIX_SHIFT;
                g_crates[i].y=(s32)-16*FIX_ONE;g_crates[i].vx=0;g_crates[i].vy=FIX_ONE;break;
        }
    }
    if(g_boss_shot_active){int sx,sy;g_boss_shot_x+=g_boss_shot_vx;g_boss_shot_y+=g_boss_shot_vy;sx=(int)(g_boss_shot_x>>FIX_SHIFT);sy=(int)(g_boss_shot_y>>FIX_SHIFT)-16;if(sx<-40||sx>272||sy>240)g_boss_shot_active=0;else if(g_invuln==0&&aabb(player_body_x()+4,player_body_y()+5,8,PLAYER_BODY_H-10,sx+4,sy+4,24,24)){hurt_player();g_boss_shot_active=0;}}
    bx=(int)(g_boss_x>>FIX_SHIFT);by=(int)(g_boss_y>>FIX_SHIFT);
    /* $15D20 is used by the source boss collision path. Values vary sharply by
       boss class, so use them to tune the broad collision body rather than the
       old universal 56x68 rectangle. */
    if(g_boss_class>0){const BossVisualData *v=&g_later_boss_visuals[g_boss_class-1];hitx=bx+8;hity=by+8;hitw=v->cols*16-16;hith=(g_boss_class==3)?56:v->rows*16-16;}
    else{hitx=bx+12;hity=by-8;hitw=72;hith=72;
        if(g_boss_hit_offsets[g_current_level*2]>0)hitx=bx+(g_boss_hit_offsets[g_current_level*2]>>1);
        if(g_boss_hit_offsets[g_current_level*2+1]>0)hith=g_boss_hit_offsets[g_current_level*2+1];
        if(hith<48)hith=64;if(hith>88)hith=88;}
    if(g_boss_invuln==0){
        for(i=0;i<MAX_FIREBALLS&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_fireballs[i].active){int fx=(int)(g_fireballs[i].x>>FIX_SHIFT),fy=(int)(g_fireballs[i].y>>FIX_SHIFT);if(aabb(fx+4,fy+4,16,16,hitx,hity,hitw,hith)){boss_take_hit();g_fireballs[i].active=0;}}
        for(i=0;i<MAX_CRATES&&g_boss_invuln==0&&!g_boss_defeated;++i)if(g_crates[i].active&&g_crates[i].thrown){int cx=(int)(g_crates[i].x>>FIX_SHIFT),cy=(int)(g_crates[i].y>>FIX_SHIFT);if(aabb(cx,cy,24,24,hitx,hity,hitw,hith)){boss_take_hit();g_crates[i].active=0;}}
        if(!g_boss_defeated&&g_invuln==0&&aabb(player_body_x()+4,player_body_y()+5,8,PLAYER_BODY_H-10,hitx+4,hity+4,hitw-8,hith-8))hurt_player();
    }
}

static void start_gameplay(int fade_in)
{
    int ycrop;
    /* Every normal level is selected through the 19-entry reconstruction table. */
    g_in_boss=0;
    vdc_setreg(BACK_CHIP,VDC_REG_CR,0);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,0);
    init_level_ring();
    init_objects();
    ycrop=(g_current_level==2)?8:0;
    g_player_x=(s32)g_level->spawn_x<<FIX_SHIFT;
    g_player_y=(s32)(g_level->spawn_y-ycrop)<<FIX_SHIFT;
    g_vx=g_vy=0;
    g_grounded=1;
    g_facing=1;
    g_player_anim_clock=0;
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
    g_crate_hide=(g_carry_index>=0&&g_grounded&&(btn&JOY_DOWN))?1:0;
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
            Stop_Sound_Sample(7,0);
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
            Stop_Sound_Sample(7,0);
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

    if(g_crate_hide){g_vx=0;g_camera_hold=1;}
    else if (g_lift_timer>0) {
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
        g_drop_platform_timer=12;g_grounded=0;g_on_moving_platform=-1;g_vy=FIX_ONE;
    } else if (g_grounded && (pressed&JOY_I) && g_lift_timer==0 && g_throw_timer==0) {
        g_jump_active=1; g_jump_index=0; g_bounce_active=0; g_grounded=0;
        Play_Sound_Sample(1,0,0);
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
       source playfield. The visual extension rows remain non-colliding. */
    if ((int)(g_player_y>>FIX_SHIFT)>224) {
        if(g_debug_mode){
            g_player_y=(s32)96<<FIX_SHIFT;g_vy=0;g_jump_active=0;g_bounce_active=0;
            draw_game_sprites();return;
        }
        begin_player_death();
        draw_game_sprites();
        return;
    }

    update_crates();
    update_fireballs();
    if(g_in_boss) update_boss_actor();
    else { update_gems(); update_enemies(); }
    if (g_state==STATE_LEVEL_FADE) { draw_game_sprites(); return; }
    if (g_player_mode!=PMODE_NORMAL) { draw_game_sprites(); return; }
    update_camera();
    ++g_player_anim_clock;
    draw_game_sprites();

    /* $16928-$1699A in the 68000 program advances ordinary stages, but for
       level slots 3,6,8,10,13,16,19 it sets FF00A8=1 and re-enters the level
       loader in boss mode. The PC-FX port keeps exactly that boss-bearing list. */
    if(!g_in_boss && (int)(g_player_x>>FIX_SHIFT)>=g_level_width_tiles*8-48){
        g_after_boss=0; g_state=STATE_LEVEL_FADE; g_state_frame=0; return;
    }

    if (pressed&JOY_SELECT) {
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

static void menu_overlay_setup(void)
{
    /* The complete 256x240 menu is on VDC0. VDC1 is sprite-only here. */
    clear_bat(FRONT_CHIP,FG_BAT_WORDS);
    vdc_set_scroll(FRONT_CHIP,0,0);
    vdc_setreg(BACK_CHIP,VDC_REG_CR,VDC_CR_BB);
    vdc_setreg(FRONT_CHIP,VDC_REG_CR,VDC_CR_SB);
    clear_front_sprites();
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
    show_black();
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
    g_state=STATE_TITLE_FADE;
    g_state_frame=0;
}

static void enter_menu(void)
{
    g_menu_choice=MENU_1P;
    g_menu_sprite_fade=16;
    show_menu_back(g_menu_choice);
    menu_overlay_setup();
    set_asset_palette_fade(&g_menu_combo_asset,16);
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
        } else if (g_state_frame<110) {
            if (g_state_frame==90) {
                /* Stage menu CHR/BAT only after the title has reached black.
                   Writing the high VDC0 pattern area during the live reveal
                   caused the reported one-frame title corruption. */
                load_menu_asset();
                g_menu_choice=MENU_1P;
                g_menu_sprite_fade=0;
                vdc_set_scroll(BACK_CHIP,0,256);
                menu_overlay_setup();
                set_asset_palette_fade(&g_menu_combo_asset,0);
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
            /* The HUD reserve begins at x4.  Continue credits are a separate
               pool; do not consume a reserve merely for entering gameplay. */
            g_lives=4; g_continues=5; g_current_level=0; g_in_boss=0;
#ifdef FORCE_LEVEL
            g_current_level=FORCE_LEVEL;
#endif
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
        } else if (g_state_frame<204) {
            int level=16-(int)((g_state_frame-156)*16/48);
            if (level<0) level=0;
            set_asset_palette_fade(&g_menu_combo_asset,level);
            clear_front_sprites();
        } else {
#ifdef FORCE_BOSS
            start_boss(1);
#else
            start_gameplay(1);
#endif
            return;
        }
    } else if (g_state==STATE_STAGE_LOAD) {
        /* CHR/BAT/SATB were completed while outputs were hidden last frame. */
        if (g_state_frame>=1) {
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
            Play_Sound_Sample(8,0,0);
            g_continue_resume=1; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
        if (elapsed>=24+10*64) {
            g_continue_resume=0; g_state=STATE_CONTINUE_FADE; g_state_frame=0; return;
        }
    } else if (g_state==STATE_CONTINUE_FADE) {
        int level=16-(int)(g_state_frame*16/32); if (level<0) level=0;
        set_continue_palette_fade(level); draw_continue_digit();
        if (g_state_frame>=32) {
            Stop_Sound_Sample(8,0);
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
    enter_boot();

    for (;;) {
        u16 btn, pressed;
        wait_frame();
        btn = read_buttons();
        pressed = (u16)(btn & (u16)~g_prev_buttons);
        g_prev_buttons = btn;

        if (g_state == STATE_PLAY) update_play(btn, pressed);
        else update_frontend(pressed);
        ++g_state_frame;
    }
    return 0;
}
