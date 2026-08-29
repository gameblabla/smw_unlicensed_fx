/*
 * Unit test for ISSUES.txt item #4: the pterodactyl's wing-flap must keep
 * cycling once it breaks off and climbs away (behavior_phase >= 3).
 *
 * Build & run on the host with:
 *   cc -std=c99 -Wall -Wextra -O2 tests/test_ptero_flap.c -o /tmp/test_ptero_flap && /tmp/test_ptero_flap
 *
 * The test re-implements the EXACT post-fix code from src/main.c: the
 * ENEMY_KIND_PTERODACTYL arm of the behaviour dispatch (state 5, ROM $C472)
 * and the cel selector in draw_game_sprites() that reads behavior_timer.
 * The escape leg used to leave behavior_timer parked at 0, so the selector
 * returned cel 1 for every frame of the climb -- the wings froze while the
 * bird kept moving, which is what the report describes.
 *
 * If src/main.c is changed without updating this file, the test will keep
 * passing against the local copies and lose its value.  Keep them in sync.
 */

#include <stdio.h>

#define FIX_SHIFT 8
#define FIX_ONE   (1 << FIX_SHIFT)

typedef struct {
    int x, y;                        /* fixed point, as in the game */
    unsigned char behavior_phase;
    unsigned char behavior_timer;
    unsigned char face_right;
} Ptero;

/* ---------- Copied from src/main.c: the state-5 behaviour ---------- */
static void ptero_update(Ptero *e, int px, int py)
{
    int x = e->x >> FIX_SHIFT, y = e->y >> FIX_SHIFT;
    if (e->behavior_phase == 0 && px >= x - 70) { e->behavior_phase = 1; e->behavior_timer = 0; }
    if (e->behavior_phase == 1 || e->behavior_phase == 2) {
        if (px < x) { e->x -= FIX_ONE; e->face_right = 0; }
        else if (px > x) { e->x += FIX_ONE; e->face_right = 1; }
        if (py < y) e->y -= FIX_ONE; else if (py > y) e->y += FIX_ONE;
        if (++e->behavior_timer >= 144) { e->behavior_timer = 0; ++e->behavior_phase; }
    } else if (e->behavior_phase >= 3) {
        e->x -= FIX_ONE; e->y -= FIX_ONE;
        ++e->behavior_timer;
    }
}

/* ---------- Copied from src/main.c: the cel selector ---------- */
static int ptero_anim(const Ptero *e)
{
    return e->behavior_phase ? (1 + ((e->behavior_timer / 8) & 1)) : 0;
}

static int failures, passes;
static void check(int cond, const char *what)
{
    if (cond) ++passes;
    else { ++failures; printf("FAIL: %s\n", what); }
}

int main(void)
{
    Ptero e;
    int f, seen1, seen2, a, w1, w2;

    /* Asleep on the ceiling: cel 0, and nothing moves. */
    e.x = 200 * FIX_ONE; e.y = 40 * FIX_ONE;
    e.behavior_phase = 0; e.behavior_timer = 0; e.face_right = 0;
    for (f = 0; f < 60; ++f) ptero_update(&e, 0, 100);
    check(e.behavior_phase == 0, "stays asleep until the player is within 70px");
    check(ptero_anim(&e) == 0, "sleeping pterodactyl shows the hanging cel");

    /* Wake it and run the two 144-frame homing legs plus a long escape. */
    e.x = 200 * FIX_ONE; e.y = 40 * FIX_ONE;
    e.behavior_phase = 0; e.behavior_timer = 0; e.face_right = 0;

    seen1 = seen2 = 0;
    for (f = 0; f < 288; ++f) {
        ptero_update(&e, 150, 120);
        a = ptero_anim(&e);
        check(a == 1 || a == 2, "chasing pterodactyl uses a flight cel");
        if (a == 1) seen1 = 1;
        if (a == 2) seen2 = 1;
    }
    check(seen1 && seen2, "both flight cels are used while chasing");
    check(e.behavior_phase == 3, "two 144-frame legs hand over to the escape");

    /* The escape leg: this is the regression.  Over 600 frames the selector
       must alternate on the same 8-frame cadence, never stick on one cel. */
    seen1 = seen2 = w1 = w2 = 0;
    for (f = 0; f < 600; ++f) {
        int before_x = e.x, before_y = e.y;
        ptero_update(&e, 150, 120);
        a = ptero_anim(&e);
        check(a == 1 || a == 2, "escaping pterodactyl uses a flight cel");
        check(e.x == before_x - FIX_ONE && e.y == before_y - FIX_ONE,
              "escape still climbs one pixel per axis per frame");
        if (a == 1) { seen1 = 1; w1 = 1; }
        if (a == 2) { seen2 = 1; w2 = 1; }
        /* No sixteen-frame window may show only one cel: that is exactly what
           a frozen behavior_timer produced. */
        if ((f % 16) == 15) {
            check(w1 && w2, "both flight cels appear in every 16-frame window");
            w1 = w2 = 0;
        }
    }
    check(seen1 && seen2, "both flight cels are used while escaping");

    /* The byte counter wraps at 256, which is a whole number of 8-frame
       cels, so the flap survives the wrap without a stalled cel. */
    e.behavior_phase = 3; e.behavior_timer = 248;
    w1 = w2 = 0;
    for (f = 0; f < 16; ++f) {
        ptero_update(&e, 150, 120);
        a = ptero_anim(&e);
        if (a == 1) w1 = 1;
        if (a == 2) w2 = 1;
    }
    check(w1 && w2, "the flap keeps alternating across the byte wrap");

    printf("Passes:   %d\nFailures: %d\n", passes, failures);
    return failures ? 1 : 0;
}
