#!/usr/bin/env python3
"""Build derived front-end frames used by the PC-FX converter.

Revision 12k removes the moving Mario/Luigi/coin graphics from the background
stream.  They are drawn by the clean resident VDC sprite data at runtime, so a
savestate can no longer catch half-updated character tiles.

The title-wall reveal is also stabilized from the instant any wall cell appears:
every visible 8x8 wall cell during frames 368..389 comes directly from the
user-approved final no-PUSH frame, and the paired overscan rows at Y 224..239
are forced on as soon as their upper matching face cells appear. Rows 208..239
remain locked from frames 390..600 onward. This removes the transient black
square placeholders in the PC-FX-only bottom rows.
"""
from pathlib import Path
from PIL import Image
from collections import Counter

PROJ=Path(__file__).resolve().parents[1]
SRC=PROJ/'assets/reference/boot_exact'
OUT=PROJ/'assets/derived/boot_exact_fixed'
OUT.mkdir(parents=True,exist_ok=True)


def dominant_row_color(im,y):
    row=[im.getpixel((x,y)) for x in range(im.width)]
    return Counter(row).most_common(1)[0]


def clean_generic_overscan(im):
    out=im.copy(); px=out.load()
    top_color,top_count=dominant_row_color(out,min(8,out.height-1))
    bot_color,bot_count=dominant_row_color(out,min(223,out.height-17))
    if top_count >= out.width*3//4:
        for y in range(0,8):
            for x in range(out.width): px[x,y]=top_color
    if bot_count >= out.width*3//4:
        for y in range(224,min(240,out.height)):
            for x in range(out.width): px[x,y]=bot_color
    return out


def clean_title_top(im):
    wall_path=PROJ/'assets/reference/title_wall_extension.png'
    if not wall_path.exists(): return im
    wall=Image.open(wall_path).convert('RGB')
    out=im.copy()
    # Correct phase used by the settled 256x240 title; only replaces the MD
    # black overscan row at the top.
    out.paste(wall.crop((0,32,256,40)),(0,0))
    return out


def stabilize_partial_title(im):
    target=PROJ/'assets/reference/front_timeline/world_nopush.png'
    if not target.exists(): return im
    ref=Image.open(target).convert('RGB')
    out=im.copy()
    # Any visible wall cell should already use its final settled graphic.
    for ty in range(16,30):
        for tx in range(32):
            if (tx,ty) in PAIR_CELLS: continue
            box=(tx*8,ty*8,tx*8+8,ty*8+8)
            cell=im.crop(box)
            if any(px!=(0,0,0) for px in cell.getdata()):
                out.paste(ref.crop(box),box)
    # Special case for the PC-FX-only overscan rows 224..239: if the matching
    # upper 8x8 cell of the same face tile is already visible, reveal the lower
    # overscan 8x8 cell immediately too instead of leaving a temporary black
    # square until a later frame. Rows 28/29 pair with rows 26/27.
    for ty in (28,29):
        src_ty=ty-2
        for tx in range(32):
            upper_box=(tx*8,src_ty*8,tx*8+8,src_ty*8+8)
            lower_box=(tx*8,ty*8,tx*8+8,ty*8+8)
            upper=list(out.crop(upper_box).getdata())
            final_lower=list(ref.crop(lower_box).getdata())
            if any(px!=(0,0,0) for px in upper) and any(px!=(0,0,0) for px in final_lower):
                out.paste(ref.crop(lower_box), lower_box)
    return out

def stabilize_bottom(im):
    target=PROJ/'assets/reference/front_timeline/world_nopush.png'
    if not target.exists(): return im
    ref=Image.open(target).convert('RGB')
    out=im.copy()
    out.paste(ref.crop((0,208,256,240)),(0,208))
    return out


# 8x8 cells covered by the baked Mario/Luigi pair once it has come to rest
# (source bounding box x 102..157, y 162..200).  From frame 348 the pair no
# longer moves, so it is kept exactly as the Mega Drive drew it instead of
# being cut out and redrawn with sprites: the wall closes over and behind it
# cell for cell, which no reconstruction of the hidden cells can reproduce.
PAIR_CELLS={(tx,ty) for ty in range(20,26) for tx in range(12,20)}

def mean_luma(im):
    g=im.convert('L')
    return sum(g.getdata())/(g.width*g.height)


# A capture drop-out (a black frame between two lit ones) is encoded by
# build_boot_exact.py as a full-screen 800-tile CHR rewrite, which does not fit
# in one PC-FX VBlank and shows up as a flicker/tearing burst.  Refuse to build
# from such a capture; tools/recapture_boot_frames.py repairs it from the ROM.
frames={int(p.stem[1:]):Image.open(p).convert('RGB') for p in sorted(SRC.glob('f*.png'))}
order=sorted(frames)
dropouts=[n for n in order[1:-1]
          if mean_luma(frames[n])<min(mean_luma(frames[n-1]),mean_luma(frames[n+1]))-20]
if dropouts:
    raise SystemExit('capture drop-outs at %s; run tools/recapture_boot_frames.py' % dropouts)

for n in order:
    path=SRC/f'f{n:04d}.png'
    im=frames[n]

    # 60..347 contains only the moving character/coin composition over black.
    # Render those objects with VDC sprites instead of repeatedly rewriting CHR.
    if 60 <= n <= 347:
        im=Image.new('RGB',(256,240),(0,0,0))
    else:
        im=clean_generic_overscan(im)


    if 400 <= n <= 600:
        im=clean_title_top(im)

    # Use the final screen's exact wall cells during the partial reveal, then
    # lock the full bottom strip once it is completely present.
    if 368 <= n < 390:
        im=stabilize_partial_title(im)
    elif 390 <= n <= 600:
        im=stabilize_bottom(im)

    # The user-approved final frame is authoritative.
    if n==600:
        target=PROJ/'assets/reference/front_timeline/world_nopush.png'
        if target.exists(): im=Image.open(target).convert('RGB')

    im.save(OUT/path.name)

print('prepared',len(list(OUT.glob('f*.png'))),'derived boot frames in',OUT)
