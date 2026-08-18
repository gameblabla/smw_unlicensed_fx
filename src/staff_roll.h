#ifndef SMB_PCFX_STAFF_ROLL_H
#define SMB_PCFX_STAFF_ROLL_H

/* Palette lines the staff roll reserves for text.  Line 0 is the starfield on
 * VDC0 and lines 4..15 are handed out to the cast's enemy palettes. */
#define STAFF_PAL_TITLE 1
#define STAFF_PAL_NAME  2
#define STAFF_PAL_BODY  3

/* col == STAFF_COL_CENTER centres the line in the 32-cell visible width. */
#define STAFF_COL_CENTER 0xFF

/* One entry per 16-pixel row of the scroll.  A NULL text is a spacer, and an
 * enemy_kind other than -1 draws that family's first level variant as a
 * metasprite to the left of the text, vertically centred on it. */
typedef struct {
    const char *text;
    unsigned char pal;
    unsigned char col;
    signed char enemy_kind;
} StaffLine;

extern const StaffLine g_staff_lines[];
extern const unsigned short g_staff_line_count;

#endif
