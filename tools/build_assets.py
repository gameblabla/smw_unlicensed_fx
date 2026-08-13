#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import argparse, csv, json
import numpy as np
from functools import lru_cache

BLACK=(0,0,0)
TILE_WORDS=16
PLAYER_FRAME_WORDS=384
OBJECT_CELL_WORDS=64


def pcfxemu_decode_yuv444(word):
    y=(word>>8)&0xFF
    u=(word&0x00F0)-128
    v=((word&0x000F)<<4)-128
    r=int(y + (-0.000039457070707*u + 1.139827967171717*v))
    g=int(y + (-0.394610164141414*u - 0.580500315656566*v))
    b=int(y + (2.031999684343434*u - 0.000481376262626*v))
    return (max(0,min(255,r)), max(0,min(255,g)), max(0,min(255,b)))

# Match pcfxemu's HuC6261 decode and choose the nearest representable YUV444
# entry.  This is important for the Stage-1 enemy palette: the MD ROM really
# does select palette 7, but the old approximate RGB->YUV encoder shifted the
# greens enough to make Koopas look like the wrong palette.
_PC_WORDS=np.arange(65536,dtype=np.uint32)
_PC_Y=(_PC_WORDS>>8)&255
_PC_U=(_PC_WORDS&0xF0).astype(np.int32)-128
_PC_V=((_PC_WORDS&0x0F)<<4).astype(np.int32)-128
_PC_R=np.clip((_PC_Y + (-0.000039457070707*_PC_U + 1.139827967171717*_PC_V)).astype(np.int32),0,255)
_PC_G=np.clip((_PC_Y + (-0.394610164141414*_PC_U - 0.580500315656566*_PC_V)).astype(np.int32),0,255)
_PC_B=np.clip((_PC_Y + (2.031999684343434*_PC_U - 0.000481376262626*_PC_V)).astype(np.int32),0,255)

def rgb_to_yuv(r,g,b):
    # Empirically matches the HuC6261/TETSU palette path used by libpcfx.
    # (The pcfxemu search table above is useful for KING texture colors, but
    # its entry interpretation is not the same as this TETSU palette API.)
    y=(76*r+150*g+29*b+128)>>8
    u=8+(((b-y)*2016+32768)>>16)
    v=8+(((r-y)*3594+32768)>>16)
    u=max(0,min(15,u)); v=max(0,min(15,v))
    up=(u<<4)-128; vp=(v<<4)-128
    ss=r+g+b-((36654*vp+107301*up+32768)>>16)
    y=(ss*21845+32768)>>16
    y=max(0,min(255,y))
    return (y<<8)|(u<<4)|v


def encode_bg_tile(idx):
    words=[]
    for row in range(8):
        p0=p1=0
        for col in range(8):
            px=idx[row*8+col]; bit=7-col
            if px&1:p0|=1<<bit
            if px&2:p1|=1<<bit
        words.append(p0|(p1<<8))
    for row in range(8):
        p2=p3=0
        for col in range(8):
            px=idx[row*8+col]; bit=7-col
            if px&4:p2|=1<<bit
            if px&8:p3|=1<<bit
        words.append(p2|(p3<<8))
    return words


def encode_sprite_cell(pixels16):
    words=[]
    for plane in range(4):
        for y in range(16):
            w=0
            for x in range(16):
                if pixels16[y][x] & (1<<plane): w |= 1 << (15-x)
            words.append(w)
    return words


def encode_8x8_as_sprite_cell(pix8):
    """Place one Genesis 8x8 pattern in the upper-left of a 16x16 VDC cell."""
    canvas=[[0]*16 for _ in range(16)]
    for y in range(8):
        for x in range(8):
            canvas[y][x]=pix8[y*8+x]
    return encode_sprite_cell(canvas)


def encode_sprite_32x48(canvas):
    words=[]
    for strip in range(3):
        for cellx in range(2):
            cell=[[canvas[strip*16+y][cellx*16+x] for x in range(16)] for y in range(16)]
            words.extend(encode_sprite_cell(cell))
    return words


def encode_sprite_32x32(canvas):
    words=[]
    for strip in range(2):
        for cellx in range(2):
            cell=[[canvas[strip*16+y][cellx*16+x] for x in range(16)] for y in range(16)]
            words.extend(encode_sprite_cell(cell))
    return words


def encode_sprite_16x32(canvas):
    words=[]
    for strip in range(2):
        cell=[[canvas[strip*16+y][x] for x in range(16)] for y in range(16)]
        words.extend(encode_sprite_cell(cell))
    return words


def encode_sprite_32x64(canvas):
    words=[]
    for strip in range(4):
        for cellx in range(2):
            cell=[[canvas[strip*16+y][cellx*16+x] for x in range(16)] for y in range(16)]
            words.extend(encode_sprite_cell(cell))
    return words


def pack_bg_rgb(img):
    assert img.width%8==0 and img.height%8==0
    pals=[]; tile_src=[]
    for ty in range(img.height//8):
        for tx in range(img.width//8):
            pixels=[]; colors=[]; seen=set()
            for y in range(8):
                for x in range(8):
                    c=img.getpixel((tx*8+x,ty*8+y))[:3]
                    pixels.append(c)
                    if c not in seen:
                        seen.add(c); colors.append(c)
            if BLACK in seen: colors=[BLACK]+[c for c in colors if c!=BLACK]
            else: colors=[BLACK]+colors
            if len(colors)>16: raise RuntimeError(f'tile {tx},{ty} has {len(colors)} colors')
            best=None
            for i,p in enumerate(pals):
                union=p+[c for c in colors if c not in p]
                if len(union)<=16:
                    cand=(len(union)-len(p),len(union),i,union)
                    if best is None or cand[:2]<best[:2]: best=cand
            if best is None:
                if len(pals)>=16: raise RuntimeError('needs >16 VDC palettes')
                pals.append(colors[:]); pi=len(pals)-1
            else:
                _,_,pi,union=best; pals[pi]=union
            tile_src.append((pi,pixels))
    tiles=[]; lookup={}; bat=[]
    for pi,pixels in tile_src:
        pal=pals[pi]
        idx=tuple(pal.index(c) for c in pixels)
        key=(pi,idx)
        if key not in lookup:
            lookup[key]=len(tiles); tiles.append(idx)
        bat.append((pi<<12)|lookup[key])
    words=[]
    for idx in tiles: words.extend(encode_bg_tile(idx))
    palette=[rgb_to_yuv(0,0,0)]*256
    for pi,p in enumerate(pals):
        for ci,c in enumerate(p): palette[pi*16+ci]=rgb_to_yuv(*c)
    return dict(width=img.width//8,height=img.height//8,palette=palette,tile_words=words,bat=bat,tile_count=len(tiles))


def read_stage_palette(leveldir):
    rgb=[BLACK]*64
    with (leveldir/'palette.csv').open() as f:
        for row in csv.DictReader(f): rgb[int(row['index'])]=(int(row['r']),int(row['g']),int(row['b']))
    yuv=[rgb_to_yuv(0,0,0)]*256
    for i,c in enumerate(rgb): yuv[i]=rgb_to_yuv(*c)
    return rgb,yuv


def tilepix(vram, idx, h=False, v=False):
    b=vram[idx*32:idx*32+32]
    if len(b)<32: return tuple([0]*64)
    rows=[]
    for y in range(8):
        row=[]
        for x in range(8):
            q=b[y*4+x//2]
            row.append((q>>4)&15 if (x&1)==0 else q&15)
        rows.append(row)
    if h: rows=[r[::-1] for r in rows]
    if v: rows=rows[::-1]
    return tuple(p for r in rows for p in r)


def read_words_be(path):
    b=path.read_bytes(); return [int.from_bytes(b[i:i+2],'big') for i in range(0,len(b),2)]


def append_genesis_word(word, vram, tiles, lookup):
    idx=word&0x7FF; hf=bool(word&0x0800); vf=bool(word&0x1000); pal=(word>>13)&3
    pix=tilepix(vram,idx,hf,vf)
    key=(pal,pix)
    if key not in lookup:
        lookup[key]=len(tiles); tiles.append(pix)
    return (pal<<12)|lookup[key]


def genesis_cram_rgb(w):
    return (((w>>1)&7)*255//7, ((w>>5)&7)*255//7, ((w>>9)&7)*255//7)


def be16(data,o): return int.from_bytes(data[o:o+2],'big')
def be32(data,o): return int.from_bytes(data[o:o+4],'big')


def vdp_addr(cmd):
    return (((cmd>>16)&0x3FFF) | ((cmd&3)<<14))


def blit_gen_tiles_to_canvas(vram, base, w, h, canvas, dx=0, dy=0):
    for tx in range(w):
        for ty in range(h):
            pix=tilepix(vram,base+tx*h+ty)
            for yy in range(8):
                for xx in range(8):
                    cy=dy+ty*8+yy; cx=dx+tx*8+xx
                    if 0<=cy<len(canvas) and 0<=cx<len(canvas[0]): canvas[cy][cx]=pix[yy*8+xx]


def build_player_bank(kit, bank):
    frame_dir=kit/f'graphics/player_animation/player_bank_{bank}/gfx_frames'
    all_words=[]
    for fi in range(1,21):
        p=next(frame_dir.glob(f'frame_{fi:02d}_ROM_*_len_0480.bin'))
        raw=p.read_bytes()
        canvas=[[0]*32 for _ in range(48)]
        fake=bytes(32)+raw
        blit_gen_tiles_to_canvas(fake,1,4,4,canvas,0,8)
        blit_gen_tiles_to_canvas(fake,17,4,1,canvas,0,40)
        all_words.extend(encode_sprite_32x48(canvas))
    return all_words


def build_player_death(kit):
    # Animation state 8 uses graphics frame 13 and metasprite 37.  Metasprite
    # 37 is 48x32: a 32x32 left block plus a 16x32 right block at y+16.
    frame_dir=kit/'graphics/player_animation/player_bank_0/gfx_frames'
    p=next(frame_dir.glob('frame_13_ROM_*_len_0480.bin'))
    fake=bytes(32)+p.read_bytes()
    c=[[0]*48 for _ in range(48)]
    blit_gen_tiles_to_canvas(fake,1,4,4,c,0,16)
    blit_gen_tiles_to_canvas(fake,17,2,4,c,32,16)
    left=[[c[y][x] for x in range(32)] for y in range(16,48)]
    right=[[c[y][32+x] for x in range(16)] for y in range(16,48)]
    return encode_sprite_32x32(left)+encode_sprite_16x32(right)


def build_reference_player_assets(project):
    """Pack separate in-game down and continue-room pose banks.

    The supplied sheet contains two different graphics groups: the down pose
    used during play, and the two wrapped poses used by the continue/game-over
    room.  Keep those groups in distinct VRAM banks so the gameplay pose can
    never become one of the continue animation frames.
    """
    sheet=Image.open(project/'marioandluigisprites.png').convert('RGBA')
    transparent=(112,174,202,255)
    down_boxes=(
        (166,76,198,116),
        (166,192,198,232),
    )
    continue_boxes=(
        ((166,134,198,174),(199,134,231,174)),
        ((166,250,198,290),(199,250,231,290)),
    )
    colors=[]
    all_boxes=list(down_boxes)+[box for row in continue_boxes for box in row]
    for box in all_boxes:
        for rgba in sheet.crop(box).getdata():
            if rgba==transparent or rgba[3]==0: continue
            rgb=rgba[:3]
            if rgb not in colors: colors.append(rgb)
    if len(colors)>15:
        raise RuntimeError(f'reference player union needs {len(colors)} colors')
    palette=[rgb_to_yuv(0,0,0)]+[rgb_to_yuv(*c) for c in colors]

    def pack_one(box):
        crop=sheet.crop(box)
        canvas=[[0]*32 for _ in range(48)]
        for y in range(40):
            for x in range(32):
                rgba=crop.getpixel((x,y))
                if rgba==transparent or rgba[3]==0: continue
                canvas[y][x]=colors.index(rgba[:3])+1
        return encode_sprite_32x48(canvas)

    def pack_row(row):
        words=[]
        for box in row:
            words.extend(pack_one(box))
        return words

    return palette,pack_one(down_boxes[0]),pack_one(down_boxes[1]),pack_row(continue_boxes[0]),pack_row(continue_boxes[1])


def build_coin_frames(frontdir):
    imgs=[Image.open(frontdir/f'coin_{f}.png').convert('RGB') for f in (120,180,240,300)]
    colors=[BLACK]
    for im in imgs:
        for c in im.getdata():
            if c not in colors: colors.append(c)
    if len(colors)>16:
        raise RuntimeError(f'coin union needs {len(colors)} colors')
    words=[]
    for im in imgs:
        canvas=[[0]*64 for _ in range(64)]
        for y in range(64):
            for x in range(64):
                c=im.getpixel((x,y))
                canvas[y][x]=colors.index(c) if c in colors else 0
        left=[[canvas[y][x] for x in range(32)] for y in range(64)]
        right=[[canvas[y][32+x] for x in range(32)] for y in range(64)]
        words.extend(encode_sprite_32x64(left))
        words.extend(encode_sprite_32x64(right))
    pal=[rgb_to_yuv(0,0,0)]*16
    for i,c in enumerate(colors): pal[i]=rgb_to_yuv(*c)
    return pal,words


def build_simple_sprite_image(path):
    im=Image.open(path).convert('RGB')
    assert im.width%16==0 and im.height%16==0
    colors=[BLACK]
    for c in im.getdata():
        if c not in colors: colors.append(c)
    if len(colors)>16: raise RuntimeError(f'{path.name} needs {len(colors)} colors')
    words=[]
    for cy in range(im.height//16):
        for cx in range(im.width//16):
            cell=[]
            for y in range(16):
                row=[]
                for x in range(16): row.append(colors.index(im.getpixel((cx*16+x,cy*16+y))))
                cell.append(row)
            words.extend(encode_sprite_cell(cell))
    pal=[rgb_to_yuv(0,0,0)]*16
    for i,c in enumerate(colors): pal[i]=rgb_to_yuv(*c)
    return pal,words


def build_menu_character_assets(frontdir,kit):
    base=Image.open(frontdir/'menu_common.png').convert('RGB')
    stage_rgb,_=read_stage_palette(kit/'graphics/levels/level_01')
    pal_rgb=stage_rgb[:16]
    pal=[rgb_to_yuv(*c) for c in pal_rgb]
    def nearest(c):
        return min(range(16),key=lambda i:(pal_rgb[i][0]-c[0])**2*3+(pal_rgb[i][1]-c[1])**2*4+(pal_rgb[i][2]-c[2])**2*2)
    caproot=frontdir
    words=[]
    for fn in ('menu_walk_1000.png','menu_walk_1020.png','menu_walk_1040.png','menu_walk_1060.png'):
        im=Image.open(caproot/fn).convert('RGB')
        pts=[]
        for y in range(150,210):
            for x in range(0,256):
                if im.getpixel((x,y))!=base.getpixel((x,y)): pts.append((x,y))
        left=min(x for x,y in pts)
        idx=[[0]*32 for _ in range(48)]
        for y in range(160,208):
            for x in range(left,min(left+32,256)):
                col=im.getpixel((x,y))
                if col!=base.getpixel((x,y)): idx[y-160][x-left]=nearest(col)
        words.extend(encode_sprite_32x48(idx))
    # Luigi from 2P menu, same source palette family.
    im=Image.open(frontdir/'menu_2p.png').convert('RGB')
    idx=[[0]*32 for _ in range(48)]
    for y in range(160,208):
        for x in range(66,98):
            col=im.getpixel((x,y))
            if col!=base.getpixel((x,y)): idx[y-160][x-66]=nearest(col)
    lwords=encode_sprite_32x48(idx)
    return pal,words,pal,lwords


def build_menu_combo(frontdir):
    base1=Image.open(frontdir/'menu_1p_base.png').convert('RGB')
    base2=Image.open(frontdir/'menu_2p_base.png').convert('RGB')
    common=Image.open(frontdir/'menu_common.png').convert('RGB')
    caproot=frontdir
    mario_crops=[]
    for fn in ('menu_walk_1000.png','menu_walk_1020.png','menu_walk_1040.png','menu_walk_1060.png'):
        im=Image.open(caproot/fn).convert('RGB')
        pts=[]
        for y in range(150,210):
            for x in range(256):
                if im.getpixel((x,y))!=common.getpixel((x,y)): pts.append((x,y))
        left=min(x for x,y in pts)
        crop=Image.new('RGB',(32,48),BLACK)
        for y in range(160,208):
            for x in range(left,min(left+32,256)):
                c=im.getpixel((x,y))
                if c!=common.getpixel((x,y)): crop.putpixel((x-left,y-160),c)
        mario_crops.append(crop)
    # Luigi static crop from the captured 2P menu.
    im2=Image.open(frontdir/'menu_2p.png').convert('RGB')
    luigi=Image.new('RGB',(32,48),BLACK)
    for y in range(160,208):
        for x in range(66,98):
            c=im2.getpixel((x,y))
            if c!=common.getpixel((x,y)): luigi.putpixel((x-66,y-160),c)
    panels=[base1,base2]
    for crop in mario_crops:
        ov=Image.new('RGB',(256,240),BLACK); ov.paste(crop,(38,160)); panels.append(ov)
    for crop in mario_crops:
        ov=Image.new('RGB',(256,240),BLACK); ov.paste(crop,(38,160)); ov.paste(luigi,(68,160)); panels.append(ov)
    combo=Image.new('RGB',(256*len(panels),240),BLACK)
    for i,im in enumerate(panels): combo.paste(im,(i*256,0))
    return pack_bg_rgb(combo)



def build_front_transition_combo(frontdir):
    frames=[]
    for fr in range(300,601,5):
        im=Image.open(frontdir/f'md_transition_{fr}.png').convert('RGB')
        if im.size!=(256,240): raise RuntimeError(f'md_transition_{fr}.png must be 256x240')
        frames.append(im)
    combo=Image.new('RGB',(256*len(frames),240),BLACK)
    for i,im in enumerate(frames): combo.paste(im,(i*256,0))
    return pack_bg_rgb(combo)

def build_object_sprite(vram, base, w, h, outw=32, outh=32):
    c=[[0]*outw for _ in range(outh)]
    blit_gen_tiles_to_canvas(vram,base,w,h,c,0,0)
    return encode_sprite_32x32(c) if outh==32 else encode_sprite_32x48(c)


def build_gem_sprite(vram, base):
    c=[[0]*16 for _ in range(16)]
    blit_gen_tiles_to_canvas(vram,base,2,2,c,0,0)
    return encode_sprite_cell(c)


def build_enemy_assets(rom, kit):
    allm=json.loads((kit/'graphics/metasprites/all_metasprites.json').read_text())
    # F3AE dynamically loads palette 7 for all five Stage-1 enemy gfx banks.
    palptr=be32(rom,0x72338+7*4)
    enemy_pal=[genesis_cram_rgb(be16(rom,palptr+i*2)) for i in range(16)]
    enemy_yuv=[rgb_to_yuv(*c) for c in enemy_pal]
    words=[]
    # Two graphics chunks per enemy type are the walking animation frames.
    # Export both instead of freezing the first chunk as revision 4 did:
    # 5 gfx types x 2 frames x 3 load buffers x 2 shapes.
    for d3 in range(0x10,0x15):
        subt=be32(rom,0x7246C+d3*4)
        for anim in range(2):
            gfxptr=be32(rom,subt+anim*4)
            rawlen=0x120 if d3==0x14 else 0x180
            raw=rom[gfxptr:gfxptr+rawlen]
            for subtype in range(3):
                vv=bytearray(65536)
                addr=vdp_addr(be32(rom,0xF454+subtype*4))
                vv[addr:addr+len(raw)]=raw
                # Record table chooses either base metasprite CF (207, 24x24)
                # or D5 (213, 24x32). Export both shapes.
                for baseid in (207,213):
                    m=next(x for x in allm if x['id']==baseid+subtype*2)
                    c=[[0]*32 for _ in range(32)]
                    for part in m['parts']:
                        blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],c,part['x_offset'],part['y_offset'])
                    words.extend(encode_sprite_32x32(c))
    return enemy_yuv, words


def world_x_for_trigger(segment, tile):
    # FF007A is 1-based Stage segment. d5 is the 0..31 tile camera phase.
    return (segment-1)*256 + tile*8 + 256


def extract_stage_object_tables(rom):
    # Initial level-loader objects from reconstruction kit, in screen/world coords.
    crates=[(112,168,0)]
    gems=[(56,88),(88,88),(120,88),(152,88)]
    # Dynamic gems: pointer table 11AC4, 8-byte records, 8888 terminator.
    p=be32(rom,0x11AC4)
    while be16(rom,p)!=0x8888:
        seg,tile,x,y=[be16(rom,p+i) for i in (0,2,4,6)]
        gems.append((world_x_for_trigger(seg,tile)+(x-0x180), y-128))
        p+=8
    # Dynamic crates: pointer table 13262, 10-byte records.
    p=be32(rom,0x13262)
    while be16(rom,p)!=0x8888:
        seg,tile,x,y,extra=[be16(rom,p+i) for i in (0,2,4,6,8)]
        crates.append((world_x_for_trigger(seg,tile)+(x-0x180),y-128,extra))
        p+=10
    # Enemy/object table at D5CE, 22 bytes. It is the Stage-1 encounter table.
    enemies=[]; p=be32(rom,0xD5CE)
    while be16(rom,p)!=0x8888:
        ws=[be16(rom,p+i*2) for i in range(11)]
        seg,tmin,tmax,pool,gfx_type,x,y,base_meta,subtype,state,flags=ws
        enemies.append((world_x_for_trigger(seg,tmin)+(x-0x180), y-128, gfx_type-0x10, subtype, 0 if base_meta==0xCF else 1, state, flags))
        p+=22
    return crates,gems,enemies


def build_stage_assets(kit,rom):
    leveldir=kit/'graphics/levels/level_01'
    segroot=kit/'graphics/stage_sets/set_01_slots_01/segments'
    runtime=kit/'graphics/runtime_gfx/frame_gameplay'
    level_vram=(leveldir/'assembled_vram.bin').read_bytes()
    live_vram=(runtime/'vram.bin').read_bytes()
    pal_rgb,palette=read_stage_palette(leveldir)

    # Plane A: true stage maps only. Crates/gems are now live sprites, not baked in.
    tiles=[tuple([0]*64)]; lookup={(0,tiles[0]):0}
    width=13*32; height=32
    bat=[0]*(width*height); collision=bytearray(width*height)
    # Keep the actual capture geometry. No sprite/asset centering is performed.
    for seg in range(13):
        sd=segroot/f'segment_{seg:02d}'
        words0=read_words_be(sd/'map_decoded_words_be.bin')
        cf=next(sd.glob('collision_ROM_*.bin')).read_bytes()
        for sy in range(28):
            for sx in range(32):
                wx=seg*32+sx
                bat[sy*width+wx]=append_genesis_word(words0[sy*32+sx],level_vram,tiles,lookup)
                collision[sy*width+wx]=cf[sy*32+sx]
        # Extend the 224-line source art into the PC-FX 240-line presentation
        # by copying the final visual tile row. Do not extend collision: pits
        # must remain open below the source playfield.
        for sy in (28,29):
            for sx in range(32):
                wx=seg*32+sx
                bat[sy*width+wx]=bat[27*width+wx]
    fg_words=[]
    for t in tiles: fg_words.extend(encode_bg_tile(t))
    fg=dict(width=width,height=height,palette=palette,tile_words=fg_words,bat=bat,tile_count=len(tiles))

    # Plane B directly from live VDP map, also uncentered.
    with (runtime/'plane_B_words.csv').open() as f:
        src=[[int(x,16) for x in row] for row in csv.reader(f)]
    btiles=[tuple([0]*64)]; blookup={(0,btiles[0]):0}; bbat=[0]*(64*64)
    for y in range(28):
        for x in range(64): bbat[y*64+x]=append_genesis_word(src[y][x],live_vram,btiles,blookup)
    for y in (28,29):
        for x in range(64): bbat[y*64+x]=bbat[27*64+x]
    bg_words=[]
    for t in btiles: bg_words.extend(encode_bg_tile(t))
    bg=dict(width=64,height=64,palette=palette,tile_words=bg_words,bat=bbat,tile_count=len(btiles))

    # Exact live SAT components, converted one-for-one to VDC sprites.  The
    # Mega Drive emits the life icon/x as one 24x16 object and the projectile
    # icon as one 16x16 object; counters/hearts are separate objects below.
    # Keeping those object boundaries avoids the old 7x2 tile-shaped HUD sheet
    # inheriting arena palette/position artifacts.
    hud_life_canvas=[[0]*32 for _ in range(16)]
    blit_gen_tiles_to_canvas(live_vram,1873,3,2,hud_life_canvas)
    hud_life_words=[]
    for cellx in range(2):
        hud_life_words.extend(encode_sprite_cell(
            [[hud_life_canvas[y][cellx*16+x] for x in range(16)] for y in range(16)]))
    hud_fire_canvas=[[0]*16 for _ in range(16)]
    blit_gen_tiles_to_canvas(live_vram,189,2,2,hud_fire_canvas)
    hud_fire_words=encode_sprite_cell(hud_fire_canvas)

    # The game uses tile 1869 as one heart and tiles 1792..1801 as digits 0..9.
    # Keep these as independent 16x16 VDC cells so health/ammo are not baked.
    hud_heart_words=encode_8x8_as_sprite_cell(tilepix(live_vram,1869))
    hud_digit_words=[]
    for ti in range(1792,1802):
        hud_digit_words.extend(encode_8x8_as_sprite_cell(tilepix(live_vram,ti)))

    player_words=build_player_bank(kit,0)
    luigi_words=build_player_bank(kit,1)
    death_words=build_player_death(kit)
    crate_words=build_object_sprite(level_vram,1802,3,3)
    gem_words=[]
    for b in (1885,1889,1893): gem_words.extend(build_gem_sprite(level_vram,b))
    fireball_words=build_object_sprite(level_vram,1860,3,3)
    enemy_pal,enemy_words=build_enemy_assets(rom,kit)
    crates,gems,enemies=extract_stage_object_tables(rom)
    return fg,bg,bytes(collision),palette[:16],player_words,luigi_words,death_words,hud_life_words,hud_fire_words,hud_heart_words,hud_digit_words,crate_words,gem_words,fireball_words,enemy_pal,enemy_words,crates,gems,enemies



def build_continue_assets(kit):
    """Reconstruct the Mega Drive CONTINUE room from scene ADR_14FA.

    Background comes from the extracted 32x28 scene map.  Static metasprites
    308/320/429 are composited into it; the Mario/Luigi objects are live VDC1
    sprites so their two-frame animation can update without touching the BAT.
    """
    sd=kit/'graphics/static_assets/gameplay'
    bg=Image.open(sd/'plane_A_initial.png').convert('RGB').resize((256,224),Image.Resampling.NEAREST)
    row=bg.crop((0,216,256,224)); canvas=Image.new('RGB',(256,240),(0,0,0)); canvas.paste(bg,(0,0)); canvas.paste(row,(0,224)); canvas.paste(row,(0,232))
    gfx=(sd/'gameplay_gfx_ROM_053F8C_len_BFE0.bin').read_bytes()
    common=(kit/'graphics/static_assets/common_gfx_ROM_04E906_len_1000.bin').read_bytes()
    vram=bytearray(0x10000); vram[:len(gfx)]=gfx; vram[0xE000:0xF000]=common
    pal=[]
    with (sd/'palette.csv').open() as f:
        for r in csv.DictReader(f): pal.append((int(r['r']),int(r['g']),int(r['b'])))
    allm=json.loads((kit/'graphics/metasprites/all_metasprites.json').read_text())
    byid={int(m['id']):m for m in allm}

    def draw_meta_rgb(im,mid,x0,y0):
        px=im.load(); m=byid[mid]
        for part in m['parts']:
            wt=int(part['width_tiles']); ht=int(part['height_tiles']); base=int(part.get('tile_index',part.get('tile',0)))
            pidx=int(part.get('palette',0)); hf=bool(part.get('hflip',False)); vf=bool(part.get('vflip',False))
            for ty in range(ht):
                for tx in range(wt):
                    # Genesis sprite patterns advance column-major by height.
                    src_tx=(wt-1-tx) if hf else tx; src_ty=(ht-1-ty) if vf else ty
                    t=base+src_tx*ht+src_ty
                    pix=tilepix(vram,t,hf,vf)
                    dx=x0+int(part['x_offset'])+tx*8; dy=y0+int(part['y_offset'])+ty*8
                    for yy in range(8):
                        for xx in range(8):
                            ci=pix[yy*8+xx]
                            if ci and 0<=dx+xx<256 and 0<=dy+yy<240: px[dx+xx,dy+yy]=pal[pidx*16+ci]

    # Object coordinates include the Mega Drive +128 sprite origin.
    # Metasprite 308 (the 184px GAME OVER line) spans x=-96..87 relative
    # to its object.  x=132 places its actual bounds at 36..219, centered in
    # the 256px raster; the old x=112 was visibly twenty pixels left.
    # 302/305 are the animated Mario/Luigi continue-room characters.  Leave
    # them out of the baked background; draw_continue_digit() emits their
    # current pose as VDC1 sprites on every frame.
    objects={308:(132,112),429:(88,144),320:(128,192)}
    for mid,(x,y) in objects.items(): draw_meta_rgb(canvas,mid,x,y)
    bgasset=pack_bg_rgb(canvas)

    # Large digits are metasprites 310..319 = 0..9, 24x24 each.
    digit_words=[]
    for digit in range(10):
        mid=310+digit; idx=[[0]*32 for _ in range(32)]
        for part in byid[mid]['parts']:
            wt=int(part['width_tiles']); ht=int(part['height_tiles']); base=int(part.get('tile_index',part.get('tile',0)))
            hf=bool(part.get('hflip',False)); vf=bool(part.get('vflip',False))
            for ty in range(ht):
                for tx in range(wt):
                    src_tx=(wt-1-tx) if hf else tx; src_ty=(ht-1-ty) if vf else ty
                    t=base+src_tx*ht+src_ty; pix=tilepix(vram,t,hf,vf)
                    dx=int(part['x_offset'])+tx*8; dy=int(part['y_offset'])+ty*8
                    for yy in range(8):
                        for xx in range(8):
                            xx2=dx+xx; yy2=dy+yy
                            if 0<=xx2<32 and 0<=yy2<32: idx[yy2][xx2]=pix[yy*8+xx]
        digit_words.extend(encode_sprite_32x32(idx))
    digit_pal=[rgb_to_yuv(*c) for c in pal[:16]]
    return bgasset,digit_pal,digit_words

def emit_u16(f,name,vals,cols=12):
    f.write(f'const unsigned short {name}[{len(vals)}] = {{\n')
    for i in range(0,len(vals),cols): f.write('  '+', '.join(f'0x{v:04X}' for v in vals[i:i+cols])+',\n')
    f.write('};\n\n')


def emit_u8(f,name,vals,cols=24):
    f.write(f'const unsigned char {name}[{len(vals)}] = {{\n')
    for i in range(0,len(vals),cols): f.write('  '+', '.join(str(v) for v in vals[i:i+cols])+',\n')
    f.write('};\n\n')


def emit_s16_records(f,name,rows):
    flat=[]
    for row in rows: flat.extend(row)
    f.write(f'const short {name}[{len(flat)}] = {{\n')
    for i in range(0,len(flat),14): f.write('  '+', '.join(str(int(v)) for v in flat[i:i+14])+',\n')
    f.write('};\n\n')


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--kit',required=True); ap.add_argument('--rom',required=True); ap.add_argument('--project',required=True)
    a=ap.parse_args(); kit=Path(a.kit); rom=Path(a.rom).read_bytes(); p=Path(a.project)
    frontdir=p/'assets/reference/front_timeline'
    front_files=[
        ('boot_120','boot_120.png'),('boot_180','boot_180.png'),('boot_240','boot_240.png'),('boot_300','boot_300.png'),
        ('boot_360','boot_360.png'),('boot_420','boot_420.png'),('world_480','world_480.png'),('world_push','world_push.png'),('world_nopush','world_nopush.png'),
        ('trans_650','trans_650.png'),('trans_670','trans_670.png'),('trans_690','trans_690.png'),('trans_720','trans_720.png'),('trans_740','trans_740.png'),
        ('trans_760','trans_760.png'),('trans_780','trans_780.png'),('trans_800','trans_800.png'),('trans_820','trans_820.png'),('trans_840','trans_840.png'),
        ('menu_1p','menu_1p.png'),('menu_2p','menu_2p.png'),('menu_1p_base','menu_1p_base.png'),('menu_2p_base','menu_2p_base.png'),('menu_common','menu_common.png')]
    fronts=[]
    for nm,fn in front_files:
        im=Image.open(frontdir/fn).convert('RGB')
        if im.size!=(256,240): raise RuntimeError(fn+' must be 256x240')
        fronts.append((nm,pack_bg_rgb(im)))
    fronts.append(('menu_combo',build_menu_combo(frontdir)))
    fronts.append(('front_transition',build_front_transition_combo(frontdir)))
    coin_pal,coin_words=build_coin_frames(frontdir)
    push_pal,push_words=build_simple_sprite_image(frontdir/'push_start_sprite.png')
    star_pal,star_words=build_simple_sprite_image(frontdir/'menu_star_sprite.png')
    menu_m_pal,menu_m_words,menu_l_pal,menu_l_words=build_menu_character_assets(frontdir,kit)
    reference_player_pal,player_down_words,luigi_down_words,continue_mario_words,continue_luigi_words=build_reference_player_assets(p)
    fg,bg,collision,player_pal,player_words,luigi_words,death_words,hud_life_words,hud_fire_words,hud_heart_words,hud_digit_words,crate_words,gem_words,fireball_words,enemy_pal,enemy_words,crates,gems,enemies=build_stage_assets(kit,rom)
    continue_bg,continue_digit_pal,continue_digit_words=build_continue_assets(kit)
    fronts.append(('continue_base',continue_bg))

    h=p/'src/assets_generated.h'; c=p/'src/assets_generated.c'
    h.write_text('''#ifndef SMB_PCFX_ASSETS_GENERATED_H\n#define SMB_PCFX_ASSETS_GENERATED_H\n\ntypedef struct {\n    unsigned short width_tiles, height_tiles, tile_count;\n    const unsigned short *palette;\n    const unsigned short *tile_words;\n    const unsigned short *bat;\n} BgAsset;\n\n'''+''.join(f'extern const BgAsset g_{nm}_asset;\n' for nm,_ in fronts)+'''\nextern const BgAsset g_level_fg_asset, g_level_bg_asset;\nextern const unsigned char g_level_collision[];\nextern const unsigned short g_player_palette[16];\nextern const unsigned short g_player_frame_words[7680];
extern const unsigned short g_luigi_frame_words[7680];
extern const unsigned short g_player_death_words[384];
extern const unsigned short g_coin_palette[16];
extern const unsigned short g_coin_sprite_words[4096];
extern const unsigned short g_push_palette[16];
extern const unsigned short g_push_sprite_words[384];
extern const unsigned short g_menu_star_palette[16];
extern const unsigned short g_menu_star_sprite_words[128];
extern const unsigned short g_menu_mario_palette[16];
extern const unsigned short g_menu_mario_words[1536];
extern const unsigned short g_menu_luigi_palette[16];
extern const unsigned short g_menu_luigi_words[384];\nextern const unsigned short g_hud_life_words[128];\nextern const unsigned short g_hud_fire_words[64];\nextern const unsigned short g_hud_heart_words[64];\nextern const unsigned short g_hud_digit_words[640];
extern const unsigned short g_continue_digit_palette[16];
extern const unsigned short g_continue_digit_words[2560];
extern const unsigned short g_continue_player_palette[16];
extern const unsigned short g_player_down_words[384];
extern const unsigned short g_luigi_down_words[384];
extern const unsigned short g_continue_mario_words[768];
extern const unsigned short g_continue_luigi_words[768];
extern const unsigned short g_crate_sprite_words[256];\nextern const unsigned short g_gem_sprite_words[192];\nextern const unsigned short g_fireball_sprite_words[256];\nextern const unsigned short g_enemy_palette[16];\nextern const unsigned short g_enemy_sprite_words[15360];\nextern const short g_crate_spawns[];\nextern const short g_gem_spawns[];\nextern const short g_enemy_spawns[];\n#define PLAYER_FRAME_COUNT 20\n#define PLAYER_FRAME_WORDS 384\n#define CRATE_SPAWN_COUNT %d\n#define GEM_SPAWN_COUNT %d\n#define ENEMY_SPAWN_COUNT %d\n#define LEVEL_WIDTH_TILES 416\n#define LEVEL_HEIGHT_TILES 32\n#define LEVEL_COLLISION_HEIGHT 32\n#define LEVEL_BG_WIDTH_TILES 64\n#define LEVEL_BG_HEIGHT_TILES 64\n#endif\n'''%(len(crates),len(gems),len(enemies)))
    with c.open('w') as f:
        f.write('#include "assets_generated.h"\n\n')
        for nm,a0 in fronts+[('level_fg',fg),('level_bg',bg)]:
            emit_u16(f,f'g_{nm}_palette',a0['palette']); emit_u16(f,f'g_{nm}_tile_words',a0['tile_words']); emit_u16(f,f'g_{nm}_bat',a0['bat'])
            f.write(f'const BgAsset g_{nm}_asset = {{{a0["width"]}, {a0["height"]}, {a0["tile_count"]}, g_{nm}_palette, g_{nm}_tile_words, g_{nm}_bat}};\n\n')
        emit_u8(f,'g_level_collision',collision)
        emit_u16(f,'g_player_palette',player_pal); emit_u16(f,'g_player_frame_words',player_words); emit_u16(f,'g_luigi_frame_words',luigi_words); emit_u16(f,'g_player_death_words',death_words); emit_u16(f,'g_coin_palette',coin_pal); emit_u16(f,'g_coin_sprite_words',coin_words); emit_u16(f,'g_push_palette',push_pal); emit_u16(f,'g_push_sprite_words',push_words); emit_u16(f,'g_menu_star_palette',star_pal); emit_u16(f,'g_menu_star_sprite_words',star_words); emit_u16(f,'g_menu_mario_palette',menu_m_pal); emit_u16(f,'g_menu_mario_words',menu_m_words); emit_u16(f,'g_menu_luigi_palette',menu_l_pal); emit_u16(f,'g_menu_luigi_words',menu_l_words)
        emit_u16(f,'g_hud_life_words',hud_life_words); emit_u16(f,'g_hud_fire_words',hud_fire_words)
        emit_u16(f,'g_hud_heart_words',hud_heart_words); emit_u16(f,'g_hud_digit_words',hud_digit_words)
        emit_u16(f,'g_continue_digit_palette',continue_digit_pal); emit_u16(f,'g_continue_digit_words',continue_digit_words)
        emit_u16(f,'g_continue_player_palette',reference_player_pal); emit_u16(f,'g_player_down_words',player_down_words); emit_u16(f,'g_luigi_down_words',luigi_down_words); emit_u16(f,'g_continue_mario_words',continue_mario_words); emit_u16(f,'g_continue_luigi_words',continue_luigi_words)
        emit_u16(f,'g_crate_sprite_words',crate_words); emit_u16(f,'g_gem_sprite_words',gem_words); emit_u16(f,'g_fireball_sprite_words',fireball_words)
        emit_u16(f,'g_enemy_palette',enemy_pal); emit_u16(f,'g_enemy_sprite_words',enemy_words)
        emit_s16_records(f,'g_crate_spawns',crates); emit_s16_records(f,'g_gem_spawns',gems); emit_s16_records(f,'g_enemy_spawns',enemies)
    print('front assets',[(n,a['tile_count']) for n,a in fronts])
    print('level tiles',fg['tile_count'],bg['tile_count'])
    print('spawns crates/gems/enemies',len(crates),len(gems),len(enemies))

if __name__=='__main__': main()
