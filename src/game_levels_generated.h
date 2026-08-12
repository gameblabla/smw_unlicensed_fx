#ifndef SMB_PCFX_GAME_LEVELS_GENERATED_H
#define SMB_PCFX_GAME_LEVELS_GENERATED_H

typedef struct { const unsigned short *tile_words; const unsigned short *segment_bat; const unsigned char *collision; unsigned short tile_count; unsigned char segment_count; } StageSetData;
typedef struct { const unsigned short *words; const unsigned short *palette; unsigned short mask0,mask1; unsigned char width,height; } EnemyVariantData;
typedef struct { unsigned char set_index, route_len; const unsigned char *route,*route_modes,*route_quadrants; const unsigned short *palette,*bg_bat; short spawn_x,spawn_y; unsigned short crate_count,gem_count,enemy_count; const short *crates,*gems,*enemies; unsigned char variant_count; const unsigned char *variant_ids; } GameLevelData;
extern const StageSetData g_stage_sets[12];
extern const GameLevelData g_game_levels[19];
extern const EnemyVariantData g_enemy_variants[];
extern const unsigned short g_enemy_variant_count;
extern const unsigned char g_boss_level_flags[19];
extern const unsigned short g_boss_bg_palette[256],g_boss_bg_tile_words[] ,g_boss_bg_bat[];
extern const unsigned short g_boss_bg_tile_count;
extern const unsigned short g_bowser_words[],g_bowser_palette[16];
extern const unsigned long long g_bowser_masks[2];
extern const unsigned short g_boss_hp_1p[19],g_boss_hp_2p[19];
extern const unsigned short g_boss_params[57],g_boss_alt_spawn[38];
extern const short g_boss_hit_offsets[38];
extern const unsigned long g_boss_handler_address[19];
#define ENEMY_FRAME_WORDS 576
#define ENEMY_VARIANT_VRAM_WORDS 1152
#define BOWSER_FRAME_WORDS 2304
#endif
