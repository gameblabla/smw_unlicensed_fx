#include <pcfx/types.h>
#include <eris/cdda.h>

#include "cdda_tracks.h"
#include "music.h"

/* The Mega Drive loader reads the 19-entry table at ROM $65C0 and sends its
 * command through the Z80 mailbox before each level:
 *
 *   $89 world, $83 forest, $85 street, $84 graveyard, $86 nuclear dump.
 *
 * The remaining cues are separate source commands/screens.  Keeping the
 * source mapping here makes it independent of the order of CD audio files. */
static const u8 g_level_tracks[19] = {
    CDDA_TRACK_WORLD, CDDA_TRACK_WORLD, CDDA_TRACK_WORLD,
    CDDA_TRACK_WORLD, CDDA_TRACK_WORLD, CDDA_TRACK_WORLD,
    CDDA_TRACK_FOREST, CDDA_TRACK_FOREST,
    CDDA_TRACK_STREET, CDDA_TRACK_STREET,
    CDDA_TRACK_GRAVEYARD, CDDA_TRACK_GRAVEYARD, CDDA_TRACK_GRAVEYARD,
    CDDA_TRACK_NUCLEAR_DUMP, CDDA_TRACK_NUCLEAR_DUMP, CDDA_TRACK_NUCLEAR_DUMP,
    CDDA_TRACK_FOREST, CDDA_TRACK_GRAVEYARD, CDDA_TRACK_STREET,
};

enum {
    MUSIC_IDLE,
    MUSIC_FADING_OUT,
    MUSIC_WAITING,
    MUSIC_FADING_IN,
    MUSIC_PLAYING,
};

#define MUSIC_VOLUME CDDA_VOLUME_MAX
#define MUSIC_FADE_STEP 4
/* CD-DA seeks take several frames on real drives.  Starting after this quiet
 * interval prevents the first play command after a level load from vanishing. */
#define MUSIC_START_DELAY 30

static u8 g_state;
static u8 g_volume;
static u8 g_next_track;
static u8 g_next_loop;
static unsigned g_wait_frames;

static void music_set_volume(unsigned volume)
{
    if (volume > MUSIC_VOLUME) volume = MUSIC_VOLUME;
    g_volume = (u8)volume;
    eris_cdda_music_set_volume(g_volume);
}

static void music_transition_to(u8 track, int loop)
{
    g_next_track = track;
    g_next_loop = loop ? 1 : 0;
    g_wait_frames = MUSIC_START_DELAY;

    if (g_state == MUSIC_PLAYING || g_state == MUSIC_FADING_IN) {
        g_state = MUSIC_FADING_OUT;
    } else if (g_state == MUSIC_FADING_OUT) {
        /* Preserve the fade already under way; only replace its destination. */
    } else {
        eris_cdda_music_stop();
        music_set_volume(0);
        g_state = MUSIC_WAITING;
    }
}

void music_init(void)
{
    eris_cdda_music_init();
    g_state = MUSIC_IDLE;
    g_next_track = CDDA_TRACK_NONE;
    music_set_volume(0);
}

void music_stop_for_transition(void)
{
    g_next_track = CDDA_TRACK_NONE;
    g_wait_frames = 0;
    if (g_state == MUSIC_PLAYING || g_state == MUSIC_FADING_IN) {
        g_state = MUSIC_FADING_OUT;
    } else if (g_state == MUSIC_WAITING) {
        eris_cdda_music_stop();
        music_set_volume(0);
        g_state = MUSIC_IDLE;
    }
}

void music_transition_to_title(void)
{
    music_transition_to(CDDA_TRACK_TITLESCREEN, 1);
}

void music_transition_to_level(int level_index)
{
    if (level_index < 0 || level_index >= 19) return;
    music_transition_to(g_level_tracks[level_index], 1);
}

void music_transition_to_boss(void)
{
    music_transition_to(CDDA_TRACK_BOSS, 1);
}

void music_transition_to_continue(void)
{
    music_transition_to(CDDA_TRACK_CONTINUE, 0);
}

void music_update(void)
{
    switch (g_state) {
    case MUSIC_FADING_OUT:
        if (g_volume > MUSIC_FADE_STEP) {
            music_set_volume(g_volume - MUSIC_FADE_STEP);
        } else {
            music_set_volume(0);
            eris_cdda_music_stop();
            g_state = g_next_track ? MUSIC_WAITING : MUSIC_IDLE;
        }
        break;

    case MUSIC_WAITING:
        if (g_wait_frames > 0) {
            --g_wait_frames;
        } else if (g_next_track != CDDA_TRACK_NONE) {
            /* A stopped track is always restarted at index 00, including after
             * death/retry, rather than resuming the previous disc position. */
            eris_cdda_music_play(g_next_track, g_next_loop);
            music_set_volume(0);
            g_state = MUSIC_FADING_IN;
        }
        break;

    case MUSIC_FADING_IN:
        if (g_volume + MUSIC_FADE_STEP < MUSIC_VOLUME) {
            music_set_volume(g_volume + MUSIC_FADE_STEP);
        } else {
            music_set_volume(MUSIC_VOLUME);
            g_state = MUSIC_PLAYING;
        }
        break;

    default:
        break;
    }

    /* The manager owns retries after a drive seek or a CD-data read. */
    eris_cdda_music_pump();
}
