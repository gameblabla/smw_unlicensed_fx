#ifndef SMB_PCFX_ASSETS_GENERATED_H
#define SMB_PCFX_ASSETS_GENERATED_H

typedef struct {
    unsigned short width_tiles, height_tiles, tile_count;
    const unsigned short *palette;
    const unsigned short *tile_words;
    const unsigned short *bat;
} BgAsset;

extern const BgAsset g_boot_120_asset;
extern const BgAsset g_boot_180_asset;
extern const BgAsset g_boot_240_asset;
extern const BgAsset g_boot_300_asset;
extern const BgAsset g_boot_360_asset;
extern const BgAsset g_boot_420_asset;
extern const BgAsset g_world_480_asset;
extern const BgAsset g_world_push_asset;
extern const BgAsset g_world_nopush_asset;
extern const BgAsset g_trans_650_asset;
extern const BgAsset g_trans_670_asset;
extern const BgAsset g_trans_690_asset;
extern const BgAsset g_trans_720_asset;
extern const BgAsset g_trans_740_asset;
extern const BgAsset g_trans_760_asset;
extern const BgAsset g_trans_780_asset;
extern const BgAsset g_trans_800_asset;
extern const BgAsset g_trans_820_asset;
extern const BgAsset g_trans_840_asset;
extern const BgAsset g_menu_1p_asset;
extern const BgAsset g_menu_2p_asset;
extern const BgAsset g_menu_1p_base_asset;
extern const BgAsset g_menu_2p_base_asset;
extern const BgAsset g_menu_common_asset;
extern const BgAsset g_menu_combo_asset;
extern const BgAsset g_front_transition_asset;
extern const BgAsset g_continue_base_asset;

extern const BgAsset g_level_fg_asset, g_level_bg_asset;
extern const unsigned char g_level_collision[];
extern const unsigned short g_player_palette[16];
extern const unsigned short g_player_frame_words[7680];
extern const unsigned short g_luigi_frame_words[7680];
extern const unsigned short g_player_death_words[384];
extern const unsigned short g_coin_palette[16];
extern const unsigned short g_coin_sprite_words[4096];
extern const unsigned short g_push_palette[16];
extern const unsigned short g_push_sprite_words[384];
extern const unsigned short g_menu_star_palette[16];
extern const unsigned short g_menu_star_sprite_words[128];
extern const unsigned short g_menu_mario_palette[16];
extern const unsigned short g_menu_mario_words[1536];
extern const unsigned short g_menu_luigi_palette[16];
extern const unsigned short g_menu_luigi_words[384];
extern const unsigned short g_hud_life_words[128];
extern const unsigned short g_hud_fire_words[64];
extern const unsigned short g_hud_heart_words[64];
extern const unsigned short g_hud_digit_words[640];
extern const unsigned short g_continue_digit_palette[16];
extern const unsigned short g_continue_digit_words[2560];
extern const unsigned short g_continue_player_palette[16];
extern const unsigned short g_player_down_words[384];
extern const unsigned short g_luigi_down_words[384];
extern const unsigned short g_continue_mario_words[768];
extern const unsigned short g_continue_luigi_words[768];
extern const unsigned short g_crate_sprite_words[256];
extern const unsigned short g_gem_sprite_words[192];
extern const unsigned short g_fireball_sprite_words[256];
extern const unsigned short g_item_sprite_words[448];
#define ITEM_KIND_COUNT 7
#define ITEM_CELL_WORDS 64
extern const unsigned short g_enemy_palette[16];
extern const unsigned short g_enemy_sprite_words[15360];
extern const short g_crate_spawns[];
extern const short g_gem_spawns[];
extern const short g_enemy_spawns[];
#define PLAYER_FRAME_COUNT 20
#define PLAYER_FRAME_WORDS 384
#define CRATE_SPAWN_COUNT 17
#define GEM_SPAWN_COUNT 63
#define ENEMY_SPAWN_COUNT 15
#define LEVEL_WIDTH_TILES 416
#define LEVEL_HEIGHT_TILES 32
#define LEVEL_COLLISION_HEIGHT 32
#define LEVEL_BG_WIDTH_TILES 64
#define LEVEL_BG_HEIGHT_TILES 64
#endif
