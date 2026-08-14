#include <pcfx/king.h>
#include <pcfx/sound.h>
#include <pcfx/types.h>

#include "audio_stub.h"
#include "sfx_adpcm_generated.h"

/* The game does not use KING graphics, so its second KRAM page is a dedicated
 * 32 kHz ADPCM bank.  Each source sample is 256-word aligned because SAL is
 * addressed in 256-word units. */
#define SFX_KRAM_PAGE       0x80000000u
#define SFX_ADPCM_RATE      ADPCM_RATE_32000
#define ADPCM_CTRL          0x50u
#define ADPCM_CHCFG         0x51u
#define ADPCM_START         0x58u
#define ADPCM_END           0x59u
#define ADPCM_ONESHOT       0x0002u

static void king_reg16(u16 reg, u16 value)
{
    __asm__ volatile ("out.h %0, 0x600[r0]\n\tout.h %1, 0x604[r0]"
                      : : "r"(reg), "r"(value));
}

static void king_reg32(u16 reg, u32 value)
{
    __asm__ volatile ("out.h %0, 0x600[r0]\n\tout.w %1, 0x604[r0]"
                      : : "r"(reg), "r"(value));
}

static void select_sample_channel(int channel, u16 start_word, u16 word_count)
{
    u16 end_word = (u16)(start_word + word_count - 1u);

    /* Stop before reprogramming: CTRL play bits are edge-triggered. */
    king_reg16(ADPCM_CTRL, (u16)(SFX_ADPCM_RATE << 2));
    adpcm_set_volume((u8)channel, 0, 0);
    adpcm_set_control(SFX_ADPCM_RATE, 1, 1, channel == 0, channel == 1);
    adpcm_set_control(SFX_ADPCM_RATE, 1, 1, 0, 0);
    king_reg16((u16)(ADPCM_CHCFG + channel), ADPCM_ONESHOT);
    king_reg16((u16)(ADPCM_START + channel * 4), (u16)(start_word >> 8));
    king_reg32((u16)(ADPCM_END + channel * 4), end_word);
    adpcm_set_volume((u8)channel, 63, 63);
    king_reg16(ADPCM_CTRL, (u16)((SFX_ADPCM_RATE << 2) | (1u << channel)));
}

void audio_init(void)
{
    unsigned i;

    /* 4-Mbit mode exposes both 512 KiB KRAM pages; ADPCM reads page 1. */
    king_set_kram_mode(1);
    king_set_kram_pages(0, 0, 0, 1);
    king_set_kram_write(SFX_KRAM_PAGE, 1);
    for (i = 0; i < SFX_ADPCM_BANK_BYTES; i += 2)
        king_kram_write((u16)(sfx_adpcm_bank[i] | ((u16)sfx_adpcm_bank[i + 1] << 8)));

    adpcm_set_control(SFX_ADPCM_RATE, 1, 1, 1, 1);
    adpcm_set_volume(0, 0, 0);
    adpcm_set_volume(1, 0, 0);
    king_reg16(ADPCM_CTRL, (u16)(SFX_ADPCM_RATE << 2));
    adpcm_set_control(SFX_ADPCM_RATE, 1, 1, 0, 0);
}

void Play_Sound_Sample(int id, int channel, int loop)
{
    const sfx_adpcm_meta_t *sample;
    int hw_channel;

    (void)loop; /* All captured effects are one-shots. */
    if (id <= SFX_NONE || id >= SFX_COUNT)
        return;
    sample = &sfx_adpcm_meta[id];
    if (sample->word_count == 0)
        return;

    /* The existing gameplay API carries a logical channel; reserve both KING
     * voices for it, which preserves explicit Stop_Sound_Sample calls. */
    hw_channel = channel & 1;
    select_sample_channel(hw_channel, sample->start_word, sample->word_count);
}

void Stop_Sound_Sample(int id, int channel)
{
    (void)id;
    adpcm_set_volume((u8)(channel & 1), 0, 0);
    king_reg16(ADPCM_CTRL, (u16)(SFX_ADPCM_RATE << 2));
}
