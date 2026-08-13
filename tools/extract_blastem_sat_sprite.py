#!/usr/bin/env python3
"""Extract an exact RGBA sprite from a BlastEm native save state's live SAT.

The Mega Drive game streams boss animation graphics into VRAM, so the static
level VRAM image is not sufficient for later bosses.  This utility decodes a
contiguous range of live sprite-table entries, including whole-sprite flips,
and crops their combined image without including either VDP plane.
"""
from argparse import ArgumentParser
from pathlib import Path

from PIL import Image


DAC = (0, 49, 87, 119, 146, 174, 206, 255)


def read_vdp(path):
    data = Path(path).read_bytes()
    if data[:8] != b"BLSTSZ\x01\x07":
        raise SystemExit(f"{path}: not a BlastEm native state")
    pos, sections = 8, {}
    while pos < len(data):
        sid = int.from_bytes(data[pos:pos + 2], "big")
        size = int.from_bytes(data[pos + 2:pos + 6], "big")
        pos += 6
        sections[sid] = data[pos:pos + size]
        pos += size
    vdp = sections[3]
    vram_size = vdp[1] * 1024
    vram = vdp[2:2 + vram_size]
    pos = 2 + vram_size
    cram = [int.from_bytes(vdp[pos + n:pos + n + 2], "big")
            for n in range(0, 128, 2)]
    pos += 128 + 128 + 320
    return vram, cram, list(vdp[pos:pos + 24])


def color(word):
    return DAC[(word >> 1) & 7], DAC[(word >> 5) & 7], DAC[(word >> 9) & 7]


def main():
    ap = ArgumentParser()
    ap.add_argument("state")
    ap.add_argument("output")
    ap.add_argument("--first", type=int, required=True,
                    help="first linked SAT entry (zero based)")
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--tile-offset", type=int, default=0,
                    help="inspect the matching DMA double buffer (usually +/-240 tiles)")
    ap.add_argument("--offset-count", type=int, default=None,
                    help="apply --tile-offset only to this many extracted entries")
    args = ap.parse_args()
    vram, cram, regs = read_vdp(args.state)
    sat_base = (regs[5] & 0x7f) << 9
    records, link = [], 0
    for order in range(args.first + args.count):
        off = (sat_base + link * 8) & 0xffff
        y = (((vram[off] << 8) | vram[off + 1]) & 0x3ff) - 128
        size, next_link = vram[off + 2], vram[off + 3] & 0x7f
        attr = (vram[off + 4] << 8) | vram[off + 5]
        x = (((vram[off + 6] << 8) | vram[off + 7]) & 0x1ff) - 128
        width, height = ((size >> 2) & 3) + 1, (size & 3) + 1
        if order >= args.first:
            records.append((x, y, width, height, attr))
        link = next_link
    left = min(x for x, _, _, _, _ in records)
    top = min(y for _, y, _, _, _ in records)
    right = max(x + w * 8 for x, _, w, _, _ in records)
    bottom = max(y + h * 8 for _, y, _, h, _ in records)
    image = Image.new("RGBA", (right - left, bottom - top))
    pixels = image.load()
    for record_index, (x, y, width, height, attr) in enumerate(records):
        offset = args.tile_offset if args.offset_count is None or record_index < args.offset_count else 0
        base_tile = ((attr & 0x7ff) + offset) & 0x7ff
        pal = (attr >> 13) & 3
        for tx in range(width):
            for ty in range(height):
                tile = base_tile + tx * height + ty
                dx = (width - 1 - tx) * 8 if attr & 0x0800 else tx * 8
                dy = (height - 1 - ty) * 8 if attr & 0x1000 else ty * 8
                for py in range(8):
                    sy = 7 - py if attr & 0x1000 else py
                    for px in range(8):
                        sx = 7 - px if attr & 0x0800 else px
                        byte = vram[(tile * 32 + sy * 4 + (sx >> 1)) & 0xffff]
                        ci = (byte & 15) if sx & 1 else byte >> 4
                        if ci:
                            pixels[x - left + dx + px, y - top + dy + py] = (*color(cram[pal * 16 + ci]), 255)
    image.save(args.output)
    print(f"wrote {args.output}: {image.width}x{image.height}, SAT {args.first}..{args.first + args.count - 1}")


if __name__ == "__main__":
    main()
