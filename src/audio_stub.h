#ifndef SMB_PCFX_AUDIO_STUB_H
#define SMB_PCFX_AUDIO_STUB_H

enum {
    SFX_NONE,
    SFX_JUMP,
    SFX_CRATE_THROW,
    SFX_BLUE_CRYSTAL,
    SFX_BOSS_HIT,
    SFX_PLAYER_DEATH,
    SFX_FIREBALL,
    SFX_PLAYER_HURT,
    SFX_MENU_CONFIRM,
    SFX_BOSS_DEFEAT,
    SFX_COUNT
};

void audio_init(void);
void Play_Sound_Sample(int id, int channel, int loop);
void Stop_Sound_Sample(int id, int channel);

#endif
