#!/usr/bin/env python3
from pathlib import Path
from PIL import Image

root=Path(__file__).resolve().parents[1]
imgs=[Image.open(root/n).convert('RGB') for n in ('mario.png','luigi.png')]
final=Image.open(root/'assets/reference/front_timeline/world_nopush.png').convert('RGB')
push=Image.open(root/'assets/reference/front_timeline/world_push.png').convert('RGB')
atlas=Image.new('RGB',(256,562)); atlas.paste(final,(0,0));atlas.paste(push,(0,240));atlas.paste(imgs[0],(0,480));atlas.paste(imgs[1],(41,480))
q=atlas.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
pal=(q.getpalette() or [])[:768];pal+=([0]*(768-len(pal)))
def yuv(r,g,b):
 y0=(77*r+150*g+29*b)>>8;u0=8+(b-y0)//32;v0=8+(r-y0)//18
 best=None
 for u4 in range(max(0,u0-3),min(15,u0+3)+1):
  for v4 in range(max(0,v0-3),min(15,v0+3)+1):
   if max(r,g,b)-min(r,g,b)>=8 and u4==8 and v4==8:continue
   u=(u4<<4)-128;v=(v4<<4)-128;ro=1167*v//1024;go=(-404*u-594*v)//1024;bo=(2081*u-v)//1024
   y=max(0,min(255,(2*(r-ro)+4*(g-go)+(b-bo)+3)//7));rr=max(0,min(255,y+ro));gg=max(0,min(255,y+go));bb=max(0,min(255,y+bo));err=2*(rr-r)**2+4*(gg-g)**2+(bb-b)**2
   if best is None or err<best[0]:best=(err,max(1,y),u4,v4)
 return (best[1]<<8)|(best[2]<<4)|best[3]
palette=[yuv(*pal[i:i+3]) for i in range(0,768,3)]
black=min(range(256),key=lambda i:sum(pal[i*3:i*3+3]))
pix=list(q.getdata()); screen=pix[:256*240];pushscreen=pix[256*240:256*480]
mario=[];luigi=[]
for y in range(41):
 row=(480+y)*256;mario+=pix[row:row+41];luigi+=pix[row+41:row+82]
def emit(f,ctype,name,data,cols=16,fmt=lambda x:str(x)):
 f.write(f'const {ctype} {name}[{len(data)}]={{\n')
 for i in range(0,len(data),cols):f.write(' '+','.join(fmt(x) for x in data[i:i+cols])+',\n')
 f.write('};\n')
with (root/'src/king_title.h').open('w') as f:
 f.write(f'#ifndef KING_TITLE_H\n#define KING_TITLE_H\n#define KING_TITLE_BLACK {black}\nextern const unsigned short g_king_title_palette[256];\nextern const unsigned char g_king_title_final[61440],g_king_title_push[61440];\nextern const unsigned char g_king_mario_block[1681],g_king_luigi_block[1681];\n#endif\n')
with (root/'src/king_title.c').open('w') as f:
 f.write('#include "king_title.h"\n');emit(f,'unsigned short','g_king_title_palette',palette,12,lambda x:f'0x{x:04x}');emit(f,'unsigned char','g_king_title_final',screen);emit(f,'unsigned char','g_king_title_push',pushscreen);emit(f,'unsigned char','g_king_mario_block',mario);emit(f,'unsigned char','g_king_luigi_block',luigi)
