"""Split a Mega Drive foreground plane into an opaque layer and a translucent one.

The Mega Drive fakes translucency in world 3 by dithering the foreground plane
against the stage plane behind it: the canopy shadow and level 8's water are
1-pixel checkerboards of a single colour.  The PC-FX has real hardware
translucency, so those regions are lifted out of the VDC plane and rebuilt as a
solid KING BG0 image that the HuC6261 blends at 50%.

`translucent_region` finds the checkerboard: an opaque pixel whose four
orthogonal neighbours are all transparent, confirmed by at least two diagonal
neighbours of the same colour that pass the same test.  The transparent halves
of the pattern are then filled in, which is what turns the checkerboard into
the solid shape the effect actually wants.

That test is deliberately strict, so it drops the pattern where it runs into
something solid -- a trunk crossing the canopy, the top row of the flower bed --
and those leftovers stayed behind in the VDC plane as visible speckle.
`complete_region` finishes the job: a cell that already carries the pattern
absorbs the rest of it, which is what makes the band come out solid.

The layer is a 16-colour (4bpp) KING BG0 image.  The band uses six colours in
world 3-1 alone, so the old four-colour layer could not carry it; 4bpp is the
smallest KING mode that can, and it keeps the image at half the KRAM of the
256-colour mode.
"""

PLANE_W = 64          # cells
OVERLAY_W = PLANE_W * 8
OVERLAY_ROW_WORDS = OVERLAY_W // 4   # 16-colour KING BG0: 4 pixels per word
OVERLAY_CELL_WORDS = 16              # 8x8 cell: two words per pixel row
MAX_OVERLAY_COLORS = 15              # index 0 is the transparent one


def _isolated(plane, x, y, w, h):
    if plane[y][x] is None:
        return False
    seen = False
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        yy, xx = y + dy, x + dx
        if 0 <= yy < h and 0 <= xx < w:
            seen = True
            if plane[yy][xx] is not None:
                return False
    return seen


def translucent_region(plane):
    """Return a grid of (palette,index) for every pixel of a dithered area."""
    h = len(plane)
    w = len(plane[0])
    iso = [[_isolated(plane, x, y, w, h) for x in range(w)] for y in range(h)]
    core = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if not iso[y][x]:
                continue
            same = 0
            for dy, dx in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and iso[yy][xx] and plane[yy][xx] == plane[y][x]:
                    same += 1
            if same >= 2:
                core[y][x] = True
    region = [[plane[y][x] if core[y][x] else None for x in range(w)] for y in range(h)]
    # The other half of the checkerboard is transparent in the source but is
    # part of the same shape; adopt it when it is enclosed by the pattern.
    for y in range(h):
        for x in range(w):
            if plane[y][x] is not None or region[y][x] is not None:
                continue
            votes = {}
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and core[yy][xx]:
                    votes[plane[yy][xx]] = votes.get(plane[yy][xx], 0) + 1
            if votes:
                colour, n = max(votes.items(), key=lambda kv: kv[1])
                if n >= 2:
                    region[y][x] = colour
    return region


def overlay_colors(region):
    counts = {}
    for row in region:
        for c in row:
            if c is not None:
                counts[c] = counts.get(c, 0) + 1
    order = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
    return [c for c, _ in order[:MAX_OVERLAY_COLORS]]


def complete_region(plane, region, passes=3):
    """Absorb the rest of the checkerboard into cells that already carry it.

    `translucent_region` only keeps pixels whose neighbourhood is unambiguous,
    which leaves the pattern frayed wherever it meets something solid.  A cell
    that is already a quarter pattern is pattern all the way through, so the
    leftovers join it: an opaque pixel with two empty-or-lifted neighbours is
    one of the dither's dots, and a transparent pixel with two lifted
    neighbours is one of its holes.
    """
    h = len(plane)
    w = len(plane[0])

    def lifted(y, x):
        return 0 <= y < h and 0 <= x < w and region[y][x] is not None

    def empty(y, x):
        return not (0 <= y < h and 0 <= x < w) or plane[y][x] is None

    for _ in range(passes):
        add = []
        for cy in range(h // 8):
            for cx in range(w // 8):
                seen = 0
                for j in range(8):
                    for i in range(8):
                        if region[cy * 8 + j][cx * 8 + i] is not None:
                            seen += 1
                if seen < 16:
                    continue
                for j in range(8):
                    for i in range(8):
                        y, x = cy * 8 + j, cx * 8 + i
                        if region[y][x] is not None:
                            continue
                        near = ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1))
                        if plane[y][x] is not None:
                            if sum(1 for p in near if empty(*p) or lifted(*p)) >= 2:
                                add.append((y, x, plane[y][x]))
                        else:
                            votes = {}
                            for p in near:
                                if lifted(*p):
                                    votes[region[p[0]][p[1]]] = votes.get(region[p[0]][p[1]], 0) + 1
                            if sum(votes.values()) >= 2:
                                add.append((y, x, max(votes.items(), key=lambda kv: kv[1])[0]))
        if not add:
            break
        for y, x, colour in add:
            region[y][x] = colour
    return region


def pack_overlay_cells(region, colors, tiles):
    """Cut a region into 8x8 KING BG0 cells, pooling them in `tiles`.

    Returns [(plane_row, [64 tile indices])] for the rows that carry anything.
    Each cell is 16 words: two per pixel row, four 4-bit pixels to a word, in
    the order KING reads them (CG offset 0 then 1).  Index zero is the
    transparent colour and an all-zero cell is never stored.  The whole stage
    family shares one pool, so `colors` must be the family's list, not one
    level's.
    """
    index = {c: i + 1 for i, c in enumerate(colors)}
    out = []
    for cy in range(len(region) // 8):
        cells = []
        for cx in range(len(region[0]) // 8):
            cell = []
            for j in range(8):
                line = region[cy * 8 + j]
                for half in (0, 4):
                    word = 0
                    for i in range(4):
                        word |= index.get(line[cx * 8 + half + i], 0) << (12 - 4 * i)
                    cell.append(word)
            cell = tuple(cell)
            if any(cell):
                if cell not in tiles:
                    tiles[cell] = len(tiles) + 1
                cells.append(tiles[cell])
            else:
                cells.append(0)
        if any(cells):
            out.append((cy, cells))
    return out
