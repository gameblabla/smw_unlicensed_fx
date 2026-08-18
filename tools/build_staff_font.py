#!/usr/bin/env python3
"""Emit src/staff_font_generated.h: the 5x7 glyph bitmaps the staff roll uses.

The staff roll is the one screen in the port that needs arbitrary text, and the
Mega Drive source has no reusable font tiles for it (its own roll is baked
metasprite art).  Rather than add a font to the asset pipeline, keep the glyph
bitmaps here as ASCII art -- they are proof-read at a glance -- and let main.c
expand them into VDC tile words at load time.  Storing 1bpp costs 512 bytes of
ROM where the expanded 4bpp tiles would cost 2 KiB.

Covers ASCII 32..95, so the credit text is uppercase-only.
"""
import pathlib

# 5 wide, 7 tall, in ASCII order starting at ' ' (32).
GLYPHS = {
    ' ': "..... ..... ..... ..... ..... ..... .....",
    '!': "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..",
    '"': ".#.#. .#.#. ..... ..... ..... ..... .....",
    '#': ".#.#. .#.#. ##### .#.#. ##### .#.#. .#.#.",
    '$': "..#.. .#### #.#.. .###. ..#.# ####. ..#..",
    '%': "##..# ##.#. ...#. ..#.. .#.## #..## .....",
    '&': ".##.. #..#. #.#.. .#... #.#.# #..#. .##.#",
    "'": "..#.. ..#.. .#... ..... ..... ..... .....",
    '(': "...#. ..#.. .#... .#... .#... ..#.. ...#.",
    ')': ".#... ..#.. ...#. ...#. ...#. ..#.. .#...",
    '*': "..... #.#.# .###. ##### .###. #.#.# .....",
    '+': "..... ..#.. ..#.. ##### ..#.. ..#.. .....",
    ',': "..... ..... ..... ..... ..##. ..##. .#...",
    '-': "..... ..... ..... ##### ..... ..... .....",
    '.': "..... ..... ..... ..... ..... .##.. .##..",
    '/': "....# ...#. ...#. ..#.. .#... .#... #....",
    '0': ".###. #...# #..## #.#.# ##..# #...# .###.",
    '1': "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    '2': ".###. #...# ....# ...#. ..#.. .#... #####",
    '3': "##### ...#. ..#.. ...#. ....# #...# .###.",
    '4': "...#. ..##. .#.#. #..#. ##### ...#. ...#.",
    '5': "##### #.... ####. ....# ....# #...# .###.",
    '6': "..##. .#... #.... ####. #...# #...# .###.",
    '7': "##### ....# ...#. ..#.. .#... .#... .#...",
    '8': ".###. #...# #...# .###. #...# #...# .###.",
    '9': ".###. #...# #...# .#### ....# ...#. .##..",
    ':': "..... .##.. .##.. ..... .##.. .##.. .....",
    ';': "..... .##.. .##.. ..... .##.. .##.. .#...",
    '<': "...#. ..#.. .#... #.... .#... ..#.. ...#.",
    '=': "..... ..... ##### ..... ##### ..... .....",
    '>': ".#... ..#.. ...#. ....# ...#. ..#.. .#...",
    '?': ".###. #...# ....# ...#. ..#.. ..... ..#..",
    '@': ".###. #...# #.### #.#.# #.### #.... .###.",
    'A': ".###. #...# #...# ##### #...# #...# #...#",
    'B': "####. #...# ####. #...# #...# #...# ####.",
    'C': ".###. #...# #.... #.... #.... #...# .###.",
    'D': "####. #...# #...# #...# #...# #...# ####.",
    'E': "##### #.... ####. #.... #.... #.... #####",
    'F': "##### #.... ####. #.... #.... #.... #....",
    'G': ".###. #...# #.... #.### #...# #...# .####",
    'H': "#...# #...# #...# ##### #...# #...# #...#",
    'I': ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.",
    'J': "...## ....# ....# ....# ....# #...# .##..",
    'K': "#...# #..#. #.#.. ##... #.#.. #..#. #...#",
    'L': "#.... #.... #.... #.... #.... #.... #####",
    'M': "#...# ##.## #.#.# #.#.# #...# #...# #...#",
    'N': "#...# #...# ##..# #.#.# #..## #...# #...#",
    'O': ".###. #...# #...# #...# #...# #...# .###.",
    'P': "####. #...# #...# ####. #.... #.... #....",
    'Q': ".###. #...# #...# #...# #.#.# #..#. .##.#",
    'R': "####. #...# #...# ####. #.#.. #..#. #...#",
    'S': ".#### #.... #.... .###. ....# ....# ####.",
    'T': "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    'U': "#...# #...# #...# #...# #...# #...# .###.",
    'V': "#...# #...# #...# #...# #...# .#.#. ..#..",
    'W': "#...# #...# #...# #.#.# #.#.# ##.## #...#",
    'X': "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    'Y': "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..",
    'Z': "##### ....# ...#. ..#.. .#... #.... #####",
    '[': ".###. .#... .#... .#... .#... .#... .###.",
    '\\': "#.... .#... .#... ..#.. ...#. ...#. ....#",
    ']': ".###. ...#. ...#. ...#. ...#. ...#. .###.",
    '^': "..#.. .#.#. #...# ..... ..... ..... .....",
    '_': "..... ..... ..... ..... ..... ..... #####",
}

FIRST_CHAR = 32
LAST_CHAR = 95


def glyph_rows(ch):
    art = GLYPHS[ch].split()
    assert len(art) == 7, ch
    rows = []
    for line in art:
        assert len(line) == 5, (ch, line)
        # Bit 7 is the leftmost pixel of the 8-pixel cell; the 5-pixel glyph
        # sits one pixel in, which leaves room for the drop shadow main.c adds
        # at +1,+1 without spilling out of the cell.
        bits = 0
        for i, c in enumerate(line):
            if c == '#':
                bits |= 1 << (6 - i)
        rows.append(bits)
    return rows


def main():
    out = pathlib.Path(__file__).resolve().parent.parent / 'src' / 'staff_font_generated.h'
    body = []
    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = chr(code)
        if ch not in GLYPHS:
            raise SystemExit('missing glyph for %r' % ch)
        rows = glyph_rows(ch)
        name = 'space' if ch == ' ' else ch
        body.append('    ' + ','.join('0x%02X' % r for r in rows) +
                    ',0x00, /* %s */' % name)
    text = ('/* Generated by tools/build_staff_font.py -- do not edit. */\n'
            '#ifndef SMB_PCFX_STAFF_FONT_GENERATED_H\n'
            '#define SMB_PCFX_STAFF_FONT_GENERATED_H\n\n'
            '#define STAFF_FONT_FIRST_CHAR %d\n' % FIRST_CHAR +
            '#define STAFF_FONT_GLYPH_COUNT %d\n\n' % (LAST_CHAR - FIRST_CHAR + 1) +
            '/* Eight rows per glyph, one bit per pixel, bit 7 leftmost. */\n'
            'static const unsigned char g_staff_font[STAFF_FONT_GLYPH_COUNT*8] = {\n' +
            '\n'.join(body) + '\n};\n\n#endif\n')
    out.write_text(text)
    print('wrote', out)


if __name__ == '__main__':
    main()
