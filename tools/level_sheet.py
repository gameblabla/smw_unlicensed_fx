"""Rebuild a level's foreground plane from a flattened reference sheet.

`assets/reference/world3/*.png` are the world 3 foreground planes as they are
meant to look: BlastEm's render of plane A with the Mega Drive's dither
resolved into the solid translucent shapes it stands for, and with the hand
edits that the port is supposed to ship (world 3-1 drops one of the middle
trunk's two leaf clusters, and the rows below the Mega Drive's 224-line
picture are the backdrop colour rather than repeated scenery).

A sheet is exact, not approximate.  BlastEm renders a Mega Drive colour
component through the ramp 0/49/87/119/146/174/206/255 where the kit's
palette.csv uses 0/36/73/109/146/182/219/255, so every pixel of the sheet is
either one palette colour or the 50% mix of two -- which is precisely the
split the port needs: the mix's front colour is what KING BG0 draws and
blends, the back colour is what stays on the VDC plane behind it.  Anything
that fails to resolve that way would be a colour the hardware cannot make, so
it is reported rather than quietly approximated.
"""

import csv

KIT_RAMP = (0, 36, 73, 109, 146, 182, 219, 255)
BLASTEM_RAMP = (0, 49, 87, 119, 146, 174, 206, 255)
_TO_BLASTEM = dict(zip(KIT_RAMP, BLASTEM_RAMP))

PLANE_W = 64          # cells
SHEET_W = PLANE_W * 8
SHEET_H = 240


def sheet_palette(path):
    """The level's palette.csv as {entry: rgb}, in the sheet's colour space."""
    out = {}
    with open(path) as fh:
        for row in csv.DictReader(fh):
            rgb = tuple(_TO_BLASTEM[int(row[c])] for c in 'rgb')
            out[int(row['index'])] = rgb
    return out


def _ray_distance(rgb, colour):
    """How far `colour` sits from the line black -> `rgb`, squared."""
    n = sum(c * c for c in rgb)
    if not n:
        return sum(c * c for c in colour)
    t = min(1.0, max(0.0, sum(rgb[i] * colour[i] for i in range(3)) / n))
    return sum((colour[i] - t * rgb[i]) ** 2 for i in range(3))


def _resolve_colours(colours, palette, tolerance=1):
    """Map every sheet colour to (front, back) palette entries.

    `front` is the translucent colour KING draws, `back` the opaque one the
    VDC keeps; either may be None for "nothing here".  A colour that is a
    palette entry outright is pure back, and one that is the mix of a colour
    with black is pure front.  For a mix of two real colours the front is the
    one that is already known to be a front colour elsewhere in the sheet --
    the canopy shadow crossing the backdrop band, say -- and the brighter of
    the two if neither is.
    """
    entries = sorted(palette)
    black = {e for e in entries if palette[e] == (0, 0, 0)}
    exact = {}
    for e in entries:
        exact.setdefault(palette[e], e)
    mixes = {}
    for a in entries:
        for b in entries:
            mix = tuple((palette[a][i] + palette[b][i]) // 2 for i in range(3))
            mixes.setdefault(mix, (a, b))

    def nearest(colour, table):
        best = None
        for key, value in table.items():
            d = max(abs(key[i] - colour[i]) for i in range(3))
            if best is None or d < best[0]:
                best = (d, value)
        return best

    plain, mixed, fronts = {}, {}, set()
    for colour in colours:
        if colour == (0, 0, 0):
            plain[colour] = (None, None)
            continue
        de, entry = nearest(colour, exact)
        dm, pair = nearest(colour, mixes)
        if de <= dm and de <= 1:
            plain[colour] = (None, entry)
        else:
            mixed[colour] = (dm, pair)
    for colour, (d, (a, b)) in mixed.items():
        if a in black or b in black:
            front = b if a in black else a
            fronts.add(front)
    resolved = dict(plain)
    for colour, (d, (a, b)) in mixed.items():
        if d > tolerance:
            # Not a colour the hardware can mix -- the sheet's own antialiasing
            # along a shadow edge, a colour part way down the line from black
            # to the shade it is edging.  The honest reading of a partly shaded
            # pixel is a shaded pixel, so take the colour whose own darkening
            # line passes closest and let the hardware shade it by half.
            resolved[colour] = (min(entries, key=lambda e: _ray_distance(
                palette[e], colour)), None)
        elif a in black or b in black:
            resolved[colour] = ((b if a in black else a), None)
        else:
            front = a if a in fronts and b not in fronts else \
                    b if b in fronts and a not in fronts else \
                    max((a, b), key=lambda e: sum(palette[e]))
            resolved[colour] = (front, a if front == b else b)
    return resolved, mixed


def resolve_sheet(image, palette, tolerance=1):
    """Split a sheet into an opaque plane and a translucent one.

    Returns (back, front, warnings): two 240x512 grids of palette entries or
    None, and the sheet colours that did not resolve cleanly.
    """
    px = image.convert('RGB').load()
    w, h = image.size
    colours = {px[x, y] for y in range(h) for x in range(w)}
    resolved, mixed = _resolve_colours(colours, palette, tolerance)
    warnings = sorted(c for c, (d, _) in mixed.items() if d > tolerance)
    back = [[None] * SHEET_W for _ in range(SHEET_H)]
    front = [[None] * SHEET_W for _ in range(SHEET_H)]
    for y in range(SHEET_H):
        for x in range(SHEET_W):
            f, b = resolved[px[x % w, y % h]]
            front[y][x] = f
            back[y][x] = b
    return back, front, warnings


def entry_pairs(grid):
    """The grid's entries as the (palette_line, colour_index) pairs the
    overlay packer and the palette lookup both speak."""
    return [[None if e is None else (e // 16, e % 16) for e in row]
            for row in grid]
