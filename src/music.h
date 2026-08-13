#ifndef SMB_PCFX_MUSIC_H
#define SMB_PCFX_MUSIC_H

/* Red Book CD-DA score control. Track replacements fade/seek/restart, while a
 * request for the currently playing track leaves its disc position intact. */
void music_init(void);
void music_update(void);
void music_transition_to_title(void);
void music_transition_to_level(int level_index);
void music_prepare_level_transition(int next_level_index);
void music_transition_to_boss(void);
void music_transition_to_continue(void);
void music_stop_for_transition(void);

#endif
