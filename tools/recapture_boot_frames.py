#!/usr/bin/env python3
"""Re-capture dropped frames in assets/reference/boot_exact from the MD ROM.

The original capture of the Mega Drive opening lost a handful of frames: the
saved PNG is (near) black even though the emulated VDP was showing the face
wall.  Frames 421, 423, 426, 428 and 432 were affected.  Those black frames are
real data as far as the converter is concerned, so build_boot_exact.py encoded
them as full-screen 800-tile CHR rewrites, which the PC-FX cannot upload inside
one VBlank.  The result was the flicker/tearing burst at the end of the wall
reveal.

This tool replays the ROM in the bundled headless BlastEm build (with the
protection bypass IPS applied in memory) and rewrites only the frames whose
stored capture is a drop-out.  Everything else is left untouched.

Usage: python3 tools/recapture_boot_frames.py [--check]
"""
from pathlib import Path
from PIL import Image
import argparse, subprocess, tempfile

PROJ = Path(__file__).resolve().parents[1]
REF = PROJ / 'assets/reference/boot_exact'
ROM = PROJ / 'Super Mario Bros. (Taiwan) (En) (Unl).md'
IPS = PROJ / 'Super_Mario_Bros_MD_reconstruction_kit/patch/protection_bypass.ips'
BLASTEM = PROJ / 'kit_blastem/blastem_headless_patched_v3'
FIRST, LAST = 12, 600


def apply_ips(rom_bytes, ips_bytes):
    data = bytearray(rom_bytes)
    assert ips_bytes[:5] == b'PATCH'
    i = 5
    while ips_bytes[i:i + 3] != b'EOF':
        off = int.from_bytes(ips_bytes[i:i + 3], 'big'); i += 3
        ln = int.from_bytes(ips_bytes[i:i + 2], 'big'); i += 2
        if ln:
            data[off:off + ln] = ips_bytes[i:i + ln]; i += ln
        else:
            rl = int.from_bytes(ips_bytes[i:i + 2], 'big'); i += 2
            data[off:off + rl] = ips_bytes[i:i + 1] * rl; i += 1
    return bytes(data)


def frame_mean(path):
    im = Image.open(path).convert('L')
    return sum(im.getdata()) / (im.width * im.height)


def find_dropouts():
    """A drop-out is a frame far darker than both of its neighbours."""
    means = {n: frame_mean(REF / f'f{n:04d}.png') for n in range(FIRST, LAST + 1)}
    return [n for n in range(FIRST + 1, LAST)
            if means[n] < min(means[n - 1], means[n + 1]) - 20]


def capture(rom_path, frame, out_png):
    subprocess.run([str(BLASTEM), '--headless', '-b', str(frame),
                    '-p', str(out_png), str(rom_path)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true',
                    help='only report drop-outs, do not rewrite anything')
    a = ap.parse_args()
    bad = find_dropouts()
    print('drop-out frames:', bad if bad else 'none')
    if a.check or not bad:
        return
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        rom = td / 'smb_md_patched.md'
        rom.write_bytes(apply_ips(ROM.read_bytes(), IPS.read_bytes()))
        for n in bad:
            shot = td / f'f{n:04d}.png'
            capture(rom, n, shot)
            im = Image.open(shot).convert('RGB')
            if im.size != (256, 240):
                raise SystemExit(f'unexpected capture size {im.size} for frame {n}')
            im.save(REF / f'f{n:04d}.png')
            print(f'recaptured f{n:04d}.png')
    left = find_dropouts()
    print('remaining drop-outs:', left if left else 'none')


if __name__ == '__main__':
    main()
