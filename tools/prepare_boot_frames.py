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


for path in sorted(SRC.glob('f*.png')):
    n=int(path.stem[1:])
    if n==432:
        im=Image.open(SRC/'f0431.png').convert('RGB')
    else:
        im=Image.open(path).convert('RGB')

    # 60..347 contains only the moving character/coin composition over black.
    # Render those objects with VDC sprites instead of repeatedly rewriting CHR.
    if 60 <= n <= 347:
        im=Image.new('RGB',(256,240),(0,0,0))
    else:
        im=clean_generic_overscan(im)

    # Once the head wall begins, the character pair remains in a clean black
    # central opening until the dual-VDC title sequence takes over at frame 421.
    # Remove only that baked pair so runtime sprites can occupy it cleanly.
    if 348 <= n <= 420:
        px=im.load()
        for y in range(100,208):
            for x in range(40,220):
                px[x,y]=(0,0,0)

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
