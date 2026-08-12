#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import argparse, sys

# Reuse the exact PC-FX tile/YUV encoder used by the main asset generator.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_assets import encode_bg_tile, rgb_to_yuv

W,H=256,240
TW,TH=32,30
# From this source frame on, the title is driven by the resident tile library in
# boot_dual.c (BAT deltas only).  The runtime skips this stream's CHR payload
# there, so it is not emitted at all.
DUAL_FIRST_MD_FRAME=433
CELL_COUNT=TW*TH
MAX_LINES=16


def popcount(v):
    return v.bit_count()


def emit_u16(f,name,vals,cols=12):
    f.write(f'const unsigned short {name}[{len(vals)}] = {{\n')
    for i in range(0,len(vals),cols):
        f.write('  '+', '.join(f'0x{v & 0xFFFF:04X}' for v in vals[i:i+cols])+',\n')
    f.write('};\n\n')


def emit_u8(f,name,vals,cols=24):
    f.write(f'const unsigned char {name}[{len(vals)}] = {{\n')
    for i in range(0,len(vals),cols):
        f.write('  '+', '.join(str(int(v)) for v in vals[i:i+cols])+',\n')
    f.write('};\n\n')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--frames',required=True)
    ap.add_argument('--project',required=True)
    ap.add_argument('--first',type=int,default=12)
    ap.add_argument('--last',type=int,default=600)
    a=ap.parse_args()
    root=Path(a.frames); project=Path(a.project)
    paths=[root/f'f{n:04d}.png' for n in range(a.first,a.last+1)]
    for p in paths:
        if not p.exists(): raise SystemExit(f'missing {p}')
    print('reading',len(paths),'exact MD frames')

    # Global RGB IDs. Exact source screenshots only have a small palette union.
    rgb_to_id={}; id_to_rgb=[]
    frame_pixels=[]
    for pi,p in enumerate(paths):
        im=Image.open(p).convert('RGB')
        if im.size!=(W,H): raise SystemExit(f'{p}: expected 256x240, got {im.size}')
        ids=[]
        for rgb in im.getdata():
            if rgb not in rgb_to_id:
                rgb_to_id[rgb]=len(id_to_rgb); id_to_rgb.append(rgb)
            ids.append(rgb_to_id[rgb])
        frame_pixels.append(ids)
    print('global colors',len(id_to_rgb))

    # For every tile cell and frame, retain a color bitset. A static palette-line
    # assignment per screen cell means the BAT never needs to be rewritten while
    # the intro runs; only CHR patterns and palette entries change during VBlank.
    cell_bits=[[0]*len(paths) for _ in range(CELL_COUNT)]
    cell_max=[0]*CELL_COUNT
    cell_sum=[0]*CELL_COUNT
    for fi,pix in enumerate(frame_pixels):
        for cy in range(TH):
            y0=cy*8
            for cx in range(TW):
                x0=cx*8; bits=0
                for yy in range(8):
                    off=(y0+yy)*W+x0
                    for xx in range(8): bits |= 1 << pix[off+xx]
                ci=cy*TW+cx
                cell_bits[ci][fi]=bits
                n=popcount(bits)
                if n>cell_max[ci]: cell_max[ci]=n
                cell_sum[ci]+=n

    # Hard cells first. For each candidate line, all frames must remain <=16 colors.
    # The score favors the smallest cumulative palette growth and the smallest peak.
    order=sorted(range(CELL_COUNT),key=lambda c:(cell_max[c],cell_sum[c]),reverse=True)
    line_frames=[]
    assignment=[-1]*CELL_COUNT
    for pos,ci in enumerate(order):
        best=None
        for li,lf in enumerate(line_frames):
            growth=0; peak=0; ok=True
            cb=cell_bits[ci]
            for fi,b in enumerate(cb):
                old=lf[fi]; merged=old|b; n=popcount(merged)
                if n>16: ok=False; break
                growth += n-popcount(old)
                if n>peak: peak=n
            if ok:
                cand=(growth,peak,li)
                if best is None or cand<best: best=cand
        if best is None:
            if len(line_frames)>=MAX_LINES:
                raise RuntimeError(f'static intro assignment requires >{MAX_LINES} palette lines at cell {ci}')
            li=len(line_frames)
            line_frames.append(cell_bits[ci][:])
        else: li=best[2]
        assignment[ci]=li
        lf=line_frames[li]
        cb=cell_bits[ci]
        for fi,b in enumerate(cb): lf[fi] |= b
    print('static palette lines',len(line_frames),'max colors',[max(popcount(b) for b in lf) for lf in line_frames])

    # Encode temporal palette changes and fixed-address tile changes. Color indices
    # are preserved frame-to-frame whenever possible, minimizing TETSU writes.
    slots=[[None]*16 for _ in range(len(line_frames))]
    prev_tile_words=[None]*CELL_COUNT
    stream=[]; offsets=[0]
    total_tile_updates=0; total_pal_updates=0; max_tiles=0; max_pals=0
    for fi,pix in enumerate(frame_pixels):
        pal_updates=[]
        # Select slot maps for this frame, retaining colors already resident.
        slot_maps=[]
        for li in range(len(line_frames)):
            needed_bits=line_frames[li][fi]
            needed=[cid for cid in range(len(id_to_rgb)) if (needed_bits>>cid)&1]
            cur=slots[li]
            present={cid:i for i,cid in enumerate(cur) if cid is not None and cid in needed}
            free=[i for i,cid in enumerate(cur) if cid not in needed]
            for cid in needed:
                if cid in present: continue
                si=free.pop(0); cur[si]=cid; present[cid]=si
                pal_updates.extend((li*16+si,rgb_to_yuv(*id_to_rgb[cid])))
            slot_maps.append(present)

        tile_updates=[]
        for cy in range(TH):
            for cx in range(TW):
                ci=cy*TW+cx; li=assignment[ci]; sm=slot_maps[li]
                idx=[]; x0=cx*8; y0=cy*8
                for yy in range(8):
                    off=(y0+yy)*W+x0
                    for xx in range(8): idx.append(sm[pix[off+xx]])
                words=tuple(encode_bg_tile(idx))
                if words!=prev_tile_words[ci]:
                    tile_updates.append(ci); tile_updates.extend(words)
                    prev_tile_words[ci]=words
        if a.first+fi>=DUAL_FIRST_MD_FRAME: tile_updates=[]
        npal=len(pal_updates)//2; ntile=len(tile_updates)//17
        stream.append(npal); stream.extend(pal_updates)
        stream.append(ntile); stream.extend(tile_updates)
        total_pal_updates+=npal; total_tile_updates+=ntile
        max_pals=max(max_pals,npal); max_tiles=max(max_tiles,ntile)
        offsets.append(len(stream))
    print('palette updates',total_pal_updates,'tile updates',total_tile_updates,'max/frame',max_pals,max_tiles,'stream words',len(stream))

    h=project/'src/boot_exact.h'; c=project/'src/boot_exact.c'
    h.write_text(f'''#ifndef SMB_PCFX_BOOT_EXACT_H\n#define SMB_PCFX_BOOT_EXACT_H\n#define BOOT_EXACT_FIRST_MD_FRAME {a.first}\n#define BOOT_EXACT_LAST_MD_FRAME {a.last}\n#define BOOT_EXACT_FRAME_COUNT {len(paths)}\n#define BOOT_EXACT_CELL_COUNT {CELL_COUNT}\nextern const unsigned char g_boot_exact_cell_palette[BOOT_EXACT_CELL_COUNT];\nextern const unsigned int g_boot_exact_offsets[BOOT_EXACT_FRAME_COUNT+1];\nextern const unsigned short g_boot_exact_stream[{len(stream)}];\n#endif\n''')
    with c.open('w') as f:
        f.write('#include "boot_exact.h"\n\n')
        emit_u8(f,'g_boot_exact_cell_palette',assignment)
        f.write(f'const unsigned int g_boot_exact_offsets[{len(offsets)}] = {{\n')
        for i in range(0,len(offsets),10): f.write('  '+', '.join(str(v)+'u' for v in offsets[i:i+10])+',\n')
        f.write('};\n\n')
        emit_u16(f,'g_boot_exact_stream',stream)
    print('wrote',h,c)

if __name__=='__main__': main()
