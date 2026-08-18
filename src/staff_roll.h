#ifndef SMB_PCFX_STAFF_ROLL_H
#define SMB_PCFX_STAFF_ROLL_H

/* Palette lines the staff roll reserves for text.  Line 0 is the starfield on
 * VDC0 and lines 4..15 are handed out to the cast's enemy palettes. */
#define STAFF_PAL_TITLE 1
#define STAFF_PAL_NAME  2
#define STAFF_PAL_BODY  3

/* col == STAFF_COL_CENTER centres the line in the 32-cell visible width. */
#define STAFF_COL_CENTER 0xFF

/* Which pair of cels a cast member is presented on.  Several families keep
 * their real pose outside the two walk cels: the crate mimic walks out of a
 * separate shared bank and only sits in its box in the variant's own, and the
 * pterodactyl's walk cel 0 is the pose it hangs from a ceiling in. */
#define STAFF_POSE_WALK  0  /* the variant's own two walk cels */
#define STAFF_POSE_FLY   1  /* walk cel 1 and the alt bank: the flight pair */
#define STAFF_POSE_CRATE 2  /* the shared walking-crate bank */

typedef struct {
    unsigned char variant;  /* index into g_enemy_variants */
    unsigned char pose;
} StaffCast;

/* One entry per 16-pixel row of the scroll.  A NULL text is a spacer, and a
 * cast other than -1 draws that member's metasprite to the left of the text,
 * centred on it. */
typedef struct {
    const char *text;
    unsigned char pal;
    unsigned char col;
    signed char cast;
} StaffLine;

extern const StaffCast g_staff_cast[];
extern const unsigned short g_staff_cast_count;
extern const StaffLine g_staff_lines[];
extern const unsigned short g_staff_line_count;

#endif
