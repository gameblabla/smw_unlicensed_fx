#ifndef SMB_PCFX_BOOT_DUAL_H
#define SMB_PCFX_BOOT_DUAL_H
#define BOOT_DUAL_BACK_BASE_WORD 0x5000
#define BOOT_DUAL_BACK_TILE_WORDS 17264
#define BOOT_DUAL_BACK_PRE_WORDS 2880
#define BOOT_DUAL_FRONT_BASE_WORD 0x9000
#define BOOT_DUAL_FRONT_TILE_WORDS 1856
#define BOOT_DUAL_FRONT_PRE_WORDS 3840
extern const unsigned short g_boot_dual_back_tiles[17264];
extern const unsigned short g_boot_dual_back_pre[2880];
extern const unsigned short g_boot_dual_front_tiles[1856];
extern const unsigned short g_boot_dual_front_pre[3840];
extern const unsigned short g_boot_dual_frame429[960];
extern const unsigned int g_boot_dual_title_offsets[168];
extern const unsigned short g_boot_dual_title_stream[6414];
extern const unsigned short g_boot_dual_blink_off[40];
extern const unsigned short g_boot_dual_blink_on[40];
#define BOOT_DUAL_BLINK_OFF_WORDS 40
#define BOOT_DUAL_BLINK_ON_WORDS 40
#endif
