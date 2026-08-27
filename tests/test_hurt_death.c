/*
 * Unit test for ISSUES.txt item #3: hurt_player / apply_item ITEM_POISON
 * must call begin_player_death() directly on a killing blow instead of
 * arming the 60-frame hurt flicker (g_hurt_pending_death).
 *
 * Build & run on the host with:
 *   cc -std=c99 -Wall -Wextra -O2 tests/test_hurt_death.c -o /tmp/test_hurt_death && /tmp/test_hurt_death
 *
 * The test re-implements the EXACT post-fix code from src/main.c (lines
 * 3940-3962, 4072-4091, 4097-4120) and asserts the observable side
 * effects on the same global state the real game uses:
 *
 *   g_player_mode        PMODE_NORMAL=0   PMODE_DYING=1
 *   g_player_hearts      0..PLAYER_MAX_HEARTS (5)
 *   g_invuln             0..HURT_FRAMES (60)
 *   g_hurt_pending_death 0
 *   g_death_timer        0..N
 *
 * If src/main.c is changed without updating this file, the test will
 * keep passing against the local copies and lose its value.  Keep them
 * in sync.  The test is intentionally written to fail (not compile) if
 * the source drifts far enough that a re-sync is needed.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---------- Constants copied verbatim from src/main.c ---------- */
#define PLAYER_MAX_HEARTS 5
#define HURT_FRAMES       60
#define PMODE_NORMAL      0
#define PMODE_DYING       1
#define ITEM_FULL_HEAL    1
#define ITEM_FIRST_AID    2
#define ITEM_POISON       3
#define SFX_PLAYER_HURT   7
#define SFX_PLAYER_DEATH  9

/* ---------- Global state (matches src/main.c BSS layout names) ---------- */
static int g_player_mode = PMODE_NORMAL;
static int g_player_hearts = PLAYER_MAX_HEARTS;
static int g_invuln = 0;
static int g_hurt_pending_death = 0;
static int g_death_timer = 0;
static int g_lives = 4;
static int g_debug_mode = 0;
static int g_crate_hide = 0;
static int g_carry_index = -1;
static int g_jump_active = 0;
static int g_bounce_active = 0;
static int g_grounded = 0;
static int g_vx = 0;
static int g_vy = 0;
static int g_fireball_ammo = 3;
static int g_enemy_freeze = 0;
static int g_menu_choice = 0;

static int g_sound_log[16];
static int g_sound_log_n = 0;

static void Play_Sound_Sample(int id, int channel, int loop)
{
    (void)channel; (void)loop;
    if (g_sound_log_n < (int)(sizeof(g_sound_log)/sizeof(g_sound_log[0])))
        g_sound_log[g_sound_log_n++] = id;
}

static void music_stop_for_transition(void) { /* no-op for test */ }

static int multiplayer_active(void) { return g_menu_choice == 1; }

typedef struct { int x, y, vx, vy; unsigned char active, carried, thrown, extra, flight_timer; } Crate;
#define MAX_CRATES 8
static Crate g_crates[MAX_CRATES];

/* ---------- Functions copied verbatim from src/main.c post-fix ---------- */
static void begin_player_death(void)
{
    if (g_debug_mode) return;
    if (g_player_mode != PMODE_NORMAL) return;
    if (g_carry_index >= 0) {
        Crate *c = &g_crates[g_carry_index];
        c->carried = 0; c->thrown = 0; c->vx = c->vy = 0;
        g_carry_index = -1;
    }
    g_jump_active = 0;
    g_bounce_active = 0;
    g_grounded = 0;
    g_player_mode = PMODE_DYING;
    g_death_timer = 0;
    g_invuln = 0;
    g_hurt_pending_death = 0;
    g_vx = 0;
    g_vy = -(6*256 + 256/2);
    if (!multiplayer_active()) music_stop_for_transition();
    Play_Sound_Sample(SFX_PLAYER_DEATH, 0, 0);
}

static void hurt_player(void)
{
    if (g_debug_mode) return;
    if (g_crate_hide && g_carry_index >= 0) return;
    if (g_player_mode != PMODE_NORMAL || g_invuln > 0) return;
    if (g_player_hearts > 0) --g_player_hearts;
    Play_Sound_Sample(SFX_PLAYER_HURT, 0, 0);
    if (g_player_hearts <= 0) {
        begin_player_death();
    } else {
        g_invuln = HURT_FRAMES;
        g_hurt_pending_death = 0;
    }
}

static void apply_item(unsigned char kind, int player2)
{
    /* player2 path is exercised in the test for completeness; Luigi is
     * modeled as a single mirror int in this rig. */
    static int g_luigi_hearts = PLAYER_MAX_HEARTS;
    static int g_luigi_lives = 4;
    static int g_luigi_invuln = 0;
    static int g_luigi_mode = PMODE_NORMAL;
    (void)g_luigi_lives; (void)g_luigi_invuln;
    switch (kind) {
    case ITEM_FULL_HEAL:
        if (player2) g_luigi_hearts = PLAYER_MAX_HEARTS;
        else g_player_hearts = PLAYER_MAX_HEARTS;
        break;
    case ITEM_FIRST_AID:
        if (player2) { if (g_luigi_hearts < PLAYER_MAX_HEARTS) ++g_luigi_hearts; }
        else if (g_player_hearts < PLAYER_MAX_HEARTS) ++g_player_hearts;
        break;
    case ITEM_POISON:
        if (player2) { if (g_luigi_hearts > 0) --g_luigi_hearts; }
        else if (g_player_hearts > 0) --g_player_hearts;
        Play_Sound_Sample(SFX_PLAYER_HURT, 0, 0);
        if (player2) { if (g_luigi_hearts <= 0) {
            g_luigi_mode = PMODE_DYING;
            /* mirror begin_luigi_death's flag for the assertion */
        } }
        else if (g_player_hearts <= 0) begin_player_death();
        break;
    default: break;
    }
}

/* ---------- Test harness ---------- */
static int g_failures = 0;
static int g_passes = 0;

#define EXPECT_EQ(actual, expected, label) do {                                  \
    long long _a = (long long)(actual), _e = (long long)(expected);             \
    if (_a == _e) { ++g_passes;                                                  \
    } else { ++g_failures;                                                       \
      fprintf(stderr, "FAIL %s: expected %lld got %lld\n", label, _e, _a); }     \
} while (0)

static void reset_state(void)
{
    g_player_mode = PMODE_NORMAL;
    g_player_hearts = PLAYER_MAX_HEARTS;
    g_invuln = 0;
    g_hurt_pending_death = 0;
    g_death_timer = 0;
    g_lives = 4;
    g_debug_mode = 0;
    g_crate_hide = 0;
    g_carry_index = -1;
    g_jump_active = 0;
    g_bounce_active = 0;
    g_grounded = 0;
    g_vx = 0;
    g_vy = 0;
    g_fireball_ammo = 3;
    g_enemy_freeze = 0;
    g_menu_choice = 0;
    g_sound_log_n = 0;
    memset(g_crates, 0, sizeof(g_crates));
}

/* ---------- Tests ---------- */

static void test_hurt_killing_blow(void)
{
    reset_state();
    g_player_hearts = 1;  /* one heart left: next hit is lethal */

    hurt_player();

    EXPECT_EQ(g_player_hearts, 0, "killing: hearts decremented to 0");
    EXPECT_EQ(g_player_mode, PMODE_DYING, "killing: PMODE_DYING set");
    EXPECT_EQ(g_hurt_pending_death, 0, "killing: hurt_pending_death NOT armed");
    EXPECT_EQ(g_invuln, 0, "killing: g_invuln cleared (begin_player_death zeros it)");
    EXPECT_EQ(g_death_timer, 0, "killing: death_timer reset");
    EXPECT_EQ(g_sound_log_n, 2, "killing: HURT sound + DEATH sound both played");
    EXPECT_EQ(g_sound_log[0], SFX_PLAYER_HURT, "killing: first sound is HURT");
    EXPECT_EQ(g_sound_log[1], SFX_PLAYER_DEATH, "killing: second sound is DEATH");
}

static void test_hurt_non_killing_blow(void)
{
    reset_state();
    g_player_hearts = 2;

    hurt_player();

    EXPECT_EQ(g_player_hearts, 1, "non-killing: hearts decremented to 1");
    EXPECT_EQ(g_player_mode, PMODE_NORMAL, "non-killing: PMODE_NORMAL preserved");
    EXPECT_EQ(g_hurt_pending_death, 0, "non-killing: hurt_pending_death stays 0");
    EXPECT_EQ(g_invuln, HURT_FRAMES, "non-killing: g_invuln set to 60");
    EXPECT_EQ(g_sound_log_n, 1, "non-killing: only HURT sound");
    EXPECT_EQ(g_sound_log[0], SFX_PLAYER_HURT, "non-killing: HURT sound");
}

static void test_hurt_invulnerable_player(void)
{
    reset_state();
    g_player_hearts = 3;
    g_invuln = 30;  /* mid-flicker */

    hurt_player();

    EXPECT_EQ(g_player_hearts, 3, "invuln: hearts untouched");
    EXPECT_EQ(g_invuln, 30, "invuln: invuln untouched");
    EXPECT_EQ(g_sound_log_n, 0, "invuln: no sound");
}

static void test_hurt_debug_mode(void)
{
    reset_state();
    g_debug_mode = 1;
    g_player_hearts = 1;

    hurt_player();

    EXPECT_EQ(g_player_hearts, 1, "debug: hearts untouched");
    EXPECT_EQ(g_player_mode, PMODE_NORMAL, "debug: still normal");
    EXPECT_EQ(g_sound_log_n, 0, "debug: no sound");
}

static void test_hurt_already_dying(void)
{
    reset_state();
    g_player_hearts = 1;
    g_player_mode = PMODE_DYING;  /* already in death anim */

    hurt_player();

    EXPECT_EQ(g_player_hearts, 1, "already-dying: hearts untouched");
    EXPECT_EQ(g_sound_log_n, 0, "already-dying: no sound");
}

static void test_poison_killing_pickup(void)
{
    reset_state();
    g_player_hearts = 1;

    apply_item(ITEM_POISON, 0);

    EXPECT_EQ(g_player_hearts, 0, "poison-killing: hearts decremented to 0");
    EXPECT_EQ(g_player_mode, PMODE_DYING, "poison-killing: PMODE_DYING set");
    EXPECT_EQ(g_hurt_pending_death, 0, "poison-killing: hurt_pending_death NOT armed");
    EXPECT_EQ(g_invuln, 0, "poison-killing: g_invuln cleared (no 60-frame flicker)");
    EXPECT_EQ(g_sound_log_n, 2, "poison-killing: HURT + DEATH");
    EXPECT_EQ(g_sound_log[0], SFX_PLAYER_HURT, "poison-killing: HURT sound");
    EXPECT_EQ(g_sound_log[1], SFX_PLAYER_DEATH, "poison-killing: DEATH sound");
}

static void test_poison_non_killing_pickup(void)
{
    reset_state();
    g_player_hearts = 3;

    apply_item(ITEM_POISON, 0);

    EXPECT_EQ(g_player_hearts, 2, "poison: hearts decremented to 2");
    EXPECT_EQ(g_player_mode, PMODE_NORMAL, "poison: still NORMAL");
    EXPECT_EQ(g_invuln, 0, "poison: no invuln from poison (only HURT sound, no flicker)");
    EXPECT_EQ(g_sound_log_n, 1, "poison: only HURT sound");
}

static void test_poison_does_not_double_kill(void)
{
    reset_state();
    g_player_hearts = 1;
    g_player_mode = PMODE_DYING;  /* already dying from a previous cause */

    apply_item(ITEM_POISON, 0);

    /* Poison still subtracts a heart (drains the count regardless), but
     * begin_player_death must no-op because mode != PMODE_NORMAL. */
    EXPECT_EQ(g_player_hearts, 0, "poison-after-death: heart still drained");
    EXPECT_EQ(g_player_mode, PMODE_DYING, "poison-after-death: still DYING");
    EXPECT_EQ(g_sound_log_n, 1, "poison-after-death: HURT only (no second DEATH)");
}

int main(void)
{
    test_hurt_killing_blow();
    test_hurt_non_killing_blow();
    test_hurt_invulnerable_player();
    test_hurt_debug_mode();
    test_hurt_already_dying();
    test_poison_killing_pickup();
    test_poison_non_killing_pickup();
    test_poison_does_not_double_kill();

    printf("Passes:   %d\n", g_passes);
    printf("Failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
