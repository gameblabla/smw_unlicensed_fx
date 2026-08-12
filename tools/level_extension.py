"""Build the authored 240-line PC-FX foreground from a V28 Mega Drive map."""

MAP_WIDTH = 32
MD_VISIBLE_ROWS = 28
PCFX_ROWS = 30


def _periodic_extension(out, first_row):
    """Continue the established vertical tile phase per map column.

    A whole-row copy is insufficient: walls, pits and terrain boundaries can
    have different periods in different columns.  Score periods 1..8 over the
    last ten available cells in each column and continue the strongest one.
    This preserves the alternating masonry weave and the outdoor cliff fill.
    """
    for y in range(first_row, PCFX_ROWS):
        for x in range(MAP_WIDTH):
            best_score = -1
            best_period = 1
            last_is_empty = (out[(y - 1) * MAP_WIDTH + x] & 0x7FF) == 0
            for period in range(1, min(8, y) + 1):
                candidate = out[(y - period) * MAP_WIDTH + x]
                # Do not let a strong older background period punch holes in
                # terrain that reaches the bottom edge (or fill a real pit).
                if ((candidate & 0x7FF) == 0) != last_is_empty:
                    continue
                score = sum(
                    out[k * MAP_WIDTH + x] == out[(k - period) * MAP_WIDTH + x]
                    for k in range(max(period, y - 10), y)
                )
                if score > best_score:
                    best_score = score
                    best_period = period
            out[y * MAP_WIDTH + x] = out[(y - best_period) * MAP_WIDTH + x]


def foreground_words_240(set_id, source_words):
    """Return 32x30 raw Genesis tile words for the final PC-FX foreground.

    Rows stored below the Mega Drive's 224-line V28 picture are workspace data,
    not extra scenery.  The extension is therefore derived from visible art.
    The rules below are based on the actual bottom-edge construction of each
    stage family rather than exposing hidden rows or cloning one arbitrary row.
    """
    source_rows = max(1, len(source_words) // MAP_WIDTH)
    out = []

    # Level 3 has one stored blank row above the active picture.  Its source
    # row 28 is another blank workspace row, so active art ends at PC-FX row
    # 26 and the two-row masonry weave must continue from row 27 onward.
    crop = 1 if set_id == 3 else 0
    authored_rows = 27 if set_id == 3 else MD_VISIBLE_ROWS
    for y in range(authored_rows):
        sy = min(y + crop, source_rows - 1)
        out.extend(source_words[sy * MAP_WIDTH:(sy + 1) * MAP_WIDTH])

    # Seed remaining cells before applying the family-specific continuation.
    last = out[-MAP_WIDTH:] if out else [0] * MAP_WIDTH
    while len(out) < MAP_WIDTH * PCFX_ROWS:
        out.extend(last)

    if set_id in (3, 6, 8):
        _periodic_extension(out, authored_rows)
    elif set_id == 7:
        # The jungle's final row contains occasional vertical root/leaf cap
        # cells. Repeating them creates conspicuous straight streaks. The
        # user's authored sample establishes 0x49AC as the matching interior
        # soil texture for those four orientations.
        interior = {0x4237: 0x49AC, 0x4238: 0x49AC,
                    0x4A37: 0x49AC, 0x4A38: 0x49AC}
        base = [interior.get(word, word) for word in out[27 * MAP_WIDTH:28 * MAP_WIDTH]]
        out[28 * MAP_WIDTH:29 * MAP_WIDTH] = base
        out[29 * MAP_WIDTH:30 * MAP_WIDTH] = base
    elif set_id >= 9:
        # Later foregrounds are object/cap layers over a level background.
        # Extending the last row repeats crates, wall ornaments, lava edges and
        # floor lips. Transparent cells correctly reveal the background plane.
        out[28 * MAP_WIDTH:30 * MAP_WIDTH] = [0] * (2 * MAP_WIDTH)

    return out
