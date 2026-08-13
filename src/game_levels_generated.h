#ifndef SMB_PCFX_GAME_LEVELS_GENERATED_H
#define SMB_PCFX_GAME_LEVELS_GENERATED_H

typedef struct { const unsigned short *tile_words; const unsigned short *segment_bat; const unsigned char *collision; unsigned short tile_count; unsigned char segment_count; } StageSetData;
typedef struct { const unsigned short *words; const unsigned short *palette; unsigned short mask0,mask1,mask2; unsigned char width,height,has_hit_frame; } EnemyVariantData;
typedef struct { const unsigned short *palette,*tile_words,*bg_bat,*fg_bat; unsigned short tile_count; } BossRoomData;
typedef struct { const unsigned short *bg_palette,*bg_tile_words,*bg_bat,*fg_tile_words,*fg_bat,*sprite_words,*sprite_palettes; const unsigned long long *sprite_masks; const unsigned char *sprite_cell_palettes; unsigned short bg_tile_count,fg_tile_count; unsigned char palette_count,frame_count,cols,rows; short spawn_x,spawn_y; } BossVisualData;
typedef struct { unsigned char set_index, route_len; const unsigned char *route,*route_modes,*route_quadrants; const unsigned short *palette,*bg_bat; short spawn_x,spawn_y; unsigned short crate_count,gem_count,enemy_count; const short *crates,*gems,*enemies; unsigned char variant_count; const unsigned char *variant_ids; } GameLevelData;
extern const StageSetData g_stage_sets[12];
extern const GameLevelData g_game_levels[19];
extern const EnemyVariantData g_enemy_variants[];
extern const unsigned short g_enemy_variant_count;
extern const unsigned char g_boss_level_flags[19];
extern const unsigned short g_boss_bg_palette[256],g_boss_bg_tile_words[] ,g_boss_bg_bat[];
extern const unsigned short g_boss_bg_tile_count;
extern const unsigned short g_bowser_flame_words[],g_bowser_special_words[],g_bowser_palette[16];
extern const unsigned short g_level6_platform_words[192];
extern const BossRoomData g_bowser_rooms[2];
extern const unsigned short g_final_ring_words[256],g_final_hammer_words[256],g_final_aux_palette[16],g_boss_pot_words[256],g_boss_pot_palette[16],g_boss_bomb_words[512],g_boss_bomb_palette[16],g_boss_crate4_words[256],g_boss_crate4_palette[16],g_boss_bell_words[256],g_boss_bell_palette[16],g_boss_water_words[64],g_boss_water_palette[16],g_boss13_platform_words[512],g_boss13_platform_palette[16];
extern const unsigned char g_bowser_flame_mask;
extern const unsigned long long g_bowser_special_masks[10];
extern const unsigned short g_monkey_banana_words[64];
extern const unsigned short g_monkey_special_words[1152],g_monkey_special_masks[2];
extern const unsigned short g_crate_walk_words[1152],g_crate_walk_masks[2];
extern const unsigned short g_level8_snake_words[3072],g_level8_snake_palette[16],g_level8_snake_masks[3];
extern const unsigned short g_boss_hp_1p[19],g_boss_hp_2p[19];
extern const unsigned short g_boss_params[57],g_boss_alt_spawn[38];
extern const short g_boss_hit_offsets[38];
extern const unsigned long g_boss_handler_address[19];
extern const BossVisualData g_later_boss_visuals[5];
extern const unsigned char g_boss_collision[6144];
#define ENEMY_RECORD_WORDS 8
#define ENEMY_KIND_GENERIC 0
#define ENEMY_KIND_MONKEY 1
#define ENEMY_KIND_PTERODACTYL 2
#define ENEMY_KIND_STRIKE_SNAKE 3
#define ENEMY_KIND_CRATE_MIMIC 4
#define ENEMY_KIND_FLOOR_SNAKE 5
#define ENEMY_FRAME_WORDS 576
#define ENEMY_VARIANT_VRAM_WORDS 1152
#define BOWSER_SPECIAL_FRAME_COUNT 10
#endif
