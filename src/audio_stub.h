#ifndef SMB_PCFX_AUDIO_STUB_H
#define SMB_PCFX_AUDIO_STUB_H

/* Audio is intentionally stubbed for this reconstruction pass.  Keep the API
   identical to the intended sample player so gameplay code does not need to
   change when the real mixer is introduced. */
void Play_Sound_Sample(int id, int channel, int loop);
void Stop_Sound_Sample(int id, int channel);

#endif
