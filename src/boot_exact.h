#ifndef SMB_PCFX_BOOT_EXACT_H
#define SMB_PCFX_BOOT_EXACT_H
#define BOOT_EXACT_FIRST_MD_FRAME 12
#define BOOT_EXACT_LAST_MD_FRAME 600
#define BOOT_EXACT_FRAME_COUNT 589
#define BOOT_EXACT_CELL_COUNT 960
extern const unsigned char g_boot_exact_cell_palette[BOOT_EXACT_CELL_COUNT];
extern const unsigned int g_boot_exact_offsets[BOOT_EXACT_FRAME_COUNT+1];
extern const unsigned short g_boot_exact_stream[34609];
#endif
