#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import sys,re
sys.path.insert(0,str(Path(__file__).resolve().parent))
from build_assets import encode_bg_tile
W,H=256,240;TW,TH=32,30;FIRST,LAST=12,600
proj=Path(__file__).resolve().parents[1];root=proj/'assets/derived/boot_exact_fixed' if (proj/'assets/derived/boot_exact_fixed').exists() else proj/'assets/reference/boot_exact'
c=(proj/'src/boot_exact.c').read_text();m=re.search(r'g_boot_exact_cell_palette\[960\] = \{(.*?)\};',c,re.S)
assignment=[int(x) for x in re.findall(r'\b\d+\b',m.group(1))];assert len(assignment)==960
rgb_to_id={};id_to_rgb=[];pixels={}
for n in range(FIRST,LAST+1):
 im=Image.open(root/f'f{n:04d}.png').convert('RGB');ids=[]
 for rgb in im.getdata():
  if rgb not in rgb_to_id:rgb_to_id[rgb]=len(id_to_rgb);id_to_rgb.append(rgb)
  ids.append(rgb_to_id[rgb])
 pixels[n]=ids
lines=max(assignment)+1;slots=[[None]*16 for _ in range(lines)];frame_tiles={}
for n in range(FIRST,LAST+1):
 pix=pixels[n];needed=[set() for _ in range(lines)]
 for cy in range(TH):
  for cx in range(TW):
   ci=cy*TW+cx;li=assignment[ci];x0=cx*8;y0=cy*8
   for yy in range(8):
    off=(y0+yy)*W+x0;needed[li].update(pix[off:off+8])
 maps=[]
 for li in range(lines):
  cur=slots[li];need=needed[li];present={cid:i for i,cid in enumerate(cur) if cid is not None and cid in need};free=[i for i,cid in enumerate(cur) if cid not in need]
  for cid in sorted(need):
   if cid in present:continue
   si=free.pop(0);cur[si]=cid;present[cid]=si
  maps.append(present)
 if n>=421:
  out=[]
  for cy in range(TH):
   for cx in range(TW):
    ci=cy*TW+cx;sm=maps[assignment[ci]];x0=cx*8;y0=cy*8;idx=[]
    for yy in range(8):
     off=(y0+yy)*W+x0;idx.extend(sm[v] for v in pix[off:off+8])
    out.append(tuple(encode_bg_tile(idx)))
  frame_tiles[n]=out

# Frame 600 is now the approved completed no-PUSH title.  Encode the prompt-on
# idle variant from world_push so blinking only changes the relevant BAT cells.
npix=[]
im=Image.open(proj/'assets/reference/front_timeline/world_push.png').convert('RGB')
for rgb in im.getdata():
 if rgb in rgb_to_id: npix.append(rgb_to_id[rgb])
 else:
  # Stable no-PUSH capture can contain a handful of quantized colors absent
  # from the exact 12..600 stream. Snap those to the nearest resident source
  # color so the blink delta never requires a new palette assignment.
  npix.append(min(range(len(id_to_rgb)),key=lambda i:sum((id_to_rgb[i][k]-rgb[k])**2 for k in range(3))))
out=[]
for cy in range(TH):
 for cx in range(TW):
  ci=cy*TW+cx;sm=maps[assignment[ci]];x0=cx*8;y0=cy*8;idx=[]
  for yy in range(8):
   off=(y0+yy)*W+x0
   for v in npix[off:off+8]:
    if v in sm: idx.append(sm[v])
    else:
     nearest=min(sm,key=lambda q:sum((id_to_rgb[q][k]-id_to_rgb[v][k])**2 for k in range(3)))
     idx.append(sm[nearest])
  out.append(tuple(encode_bg_tile(idx)))
frame_tiles[601]=out

def lib(frames,base):
 pats=[];lut={};maps={}
 for n in frames:
  bat=[]
  for ci,t in enumerate(frame_tiles[n]):
   if t not in lut:lut[t]=len(pats);pats.append(t)
   bat.append((assignment[ci]<<12)|(base//16+lut[t]))
  maps[n]=bat
 return [w for p in pats for w in p],maps
BACK_BASE=0x5000;FRONT_BASE=0x9000
back_frames=[422,424,427,429,433]+list(range(434,602));seen=set();back_frames=[n for n in back_frames if not(n in seen or seen.add(n))]
front_frames=[421,423,426,428]
back_words,bmaps=lib(back_frames,BACK_BASE);front_words,fmaps=lib(front_frames,FRONT_BASE)
def delta(a,b):
 out=[]
 for ci,(x,y) in enumerate(zip(a,b)):
  if x!=y:out.extend((ci,y))
 return out
# After frame 433, VDC0 top-left contains f429 == f433 and receives cheap BAT deltas.
title_targets=list(range(434,601));to=[0];ts=[];prev=bmaps[433]
for n in title_targets:ts+=delta(prev,bmaps[n]);to.append(len(ts));prev=bmaps[n]
# bmaps[600] is the settled no-PUSH title. frame 601 is the PUSH START variant.
blink_on=delta(bmaps[600],bmaps[601])
blink_off=delta(bmaps[601],bmaps[600])
# Three preloaded VDC0 alternate quadrants and all four VDC1 quadrants.
back_pre=[]
for n in [422,424,427]:back_pre+=bmaps[n]
front_pre=[]
for n in [421,423,426,428]:front_pre+=fmaps[n]
print('back patterns',len(back_words)//16,'front patterns',len(front_words)//16,'pre BAT',len(back_pre)+len(front_pre),'title max',max((to[i+1]-to[i])//2 for i in range(len(title_targets))))

def e16(f,n,v,cols=12):
 f.write(f'const unsigned short {n}[{len(v)}] = {{\n');
 for i in range(0,len(v),cols):f.write('  '+', '.join(f'0x{x&65535:04X}' for x in v[i:i+cols])+',\n')
 f.write('};\n\n')
def e32(f,n,v,cols=10):
 f.write(f'const unsigned int {n}[{len(v)}] = {{\n');
 for i in range(0,len(v),cols):f.write('  '+', '.join(str(x)+'u' for x in v[i:i+cols])+',\n')
 f.write('};\n\n')
h=proj/'src/boot_dual.h';cc=proj/'src/boot_dual.c'
h.write_text(f'''#ifndef SMB_PCFX_BOOT_DUAL_H\n#define SMB_PCFX_BOOT_DUAL_H\n#define BOOT_DUAL_BACK_BASE_WORD 0x{BACK_BASE:04X}\n#define BOOT_DUAL_BACK_TILE_WORDS {len(back_words)}\n#define BOOT_DUAL_BACK_PRE_WORDS {len(back_pre)}\n#define BOOT_DUAL_FRONT_BASE_WORD 0x{FRONT_BASE:04X}\n#define BOOT_DUAL_FRONT_TILE_WORDS {len(front_words)}\n#define BOOT_DUAL_FRONT_PRE_WORDS {len(front_pre)}\nextern const unsigned short g_boot_dual_back_tiles[{len(back_words)}];\nextern const unsigned short g_boot_dual_back_pre[{len(back_pre)}];\nextern const unsigned short g_boot_dual_front_tiles[{len(front_words)}];\nextern const unsigned short g_boot_dual_front_pre[{len(front_pre)}];\nextern const unsigned short g_boot_dual_frame429[960];\nextern const unsigned int g_boot_dual_title_offsets[{len(to)}];\nextern const unsigned short g_boot_dual_title_stream[{len(ts)}];\nextern const unsigned short g_boot_dual_blink_off[{len(blink_off)}];\nextern const unsigned short g_boot_dual_blink_on[{len(blink_on)}];\n#define BOOT_DUAL_BLINK_OFF_WORDS {len(blink_off)}\n#define BOOT_DUAL_BLINK_ON_WORDS {len(blink_on)}\n#endif\n''')
with cc.open('w') as f:
 f.write('#include "boot_dual.h"\n\n');e16(f,'g_boot_dual_back_tiles',back_words);e16(f,'g_boot_dual_back_pre',back_pre);e16(f,'g_boot_dual_front_tiles',front_words);e16(f,'g_boot_dual_front_pre',front_pre);e16(f,'g_boot_dual_frame429',bmaps[429]);e32(f,'g_boot_dual_title_offsets',to);e16(f,'g_boot_dual_title_stream',ts);e16(f,'g_boot_dual_blink_off',blink_off);e16(f,'g_boot_dual_blink_on',blink_on)
