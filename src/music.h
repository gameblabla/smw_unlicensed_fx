#ifndef SMB_PCFX_MUSIC_H
#define SMB_PCFX_MUSIC_H

/* Red Book CD-DA score control.  These calls are deliberately transition based:
 * a replacement always fades the old track down, stops the drive, waits for it
 * to settle, then starts the new track from its beginning and fades it in. */
void music_init(void);
void music_update(void);
void music_transition_to_title(void);
void music_transition_to_level(int level_index);
void music_transition_to_boss(void);
void music_transition_to_continue(void);
void music_stop_for_transition(void);

#endif
