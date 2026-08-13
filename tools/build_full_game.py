#!/usr/bin/env python3
from pathlib import Path
import json,csv,sys
from PIL import Image
sys.path.insert(0,str(Path(__file__).parent))
import build_assets as ba
from level_extension import foreground_words_240

import os
PROJECT=Path(__file__).resolve().parent.parent
KIT=Path(os.environ.get('KIT','/mnt/data/mdkit_full/Super_Mario_Bros_MD_reconstruction_kit'))
ROMPATH=Path(os.environ.get('ROM','/mnt/data/r9mdrom/smb_patched.md'))
BOSSREF=Path(os.environ.get('BOSSREF',str(PROJECT/'assets/reference/boss')))
LEVEL_OVERRIDES=Path(os.environ.get('LEVEL_OVERRIDES',str(PROJECT/'level_overrides')))

# These slots share the early cloud-sky background family.  The Mega Drive
# source is a 224-line composition; on PC-FX it is placed at Y+16 rather than
# fabricating extra bottom rows to reach 240 lines.
CLOUD_BG_Y16_SLOTS={1,2,4}

# The Mega Drive background plane is 64 cells wide and is loaded as two 32x32
# maps: the descriptor writing to ...000 fills columns 0..31, the one writing
# to ...040 fills columns 32..63 (32 cells = 0x40 bytes into the row).  Using
# only the first map and repeating it every 32 cells, as r12m did, dropped
# every cloud and hill that lives in the right half and cut in two the ones
# that straddle the seam.
BG_PLANE_W=64

# Mega Drive display height in cells (V28 = 224 lines).  Anything the source
# maps hold below this was never on screen.
MD_VISIBLE_ROWS=28

# Level 3's extracted foreground retains one empty top map row.  The other
# eight pixels of the 16-line emulator border came from the VDP raster origin,
# so only this one stored row is removed from the PC-FX tile/collision maps.
LEVEL3_ACTIVE_CROP_ROWS=1

def override_json(name):
    path=LEVEL_OVERRIDES/name
    if not path.exists(): return None
    data=json.loads(path.read_text())
    if data.get('version')!=1: raise RuntimeError(f'Unsupported level override version in {path}')
    return data

def override_words(name,default,expected):
    data=override_json(name)
    if data is None: return default
    words=data.get('words')
    if not isinstance(words,list) or len(words)!=expected or any(not isinstance(x,int) or not 0<=x<=0xFFFF for x in words):
        raise RuntimeError(f'Invalid {expected}-word tilemap override: {LEVEL_OVERRIDES/name}')
    return words

def apply_tile_overrides(vram,name):
    data=override_json(name)
    if data is None:return vram
    out=bytearray(vram)
    for key,pixels in data.get('tiles',{}).items():
        tile=int(key)
        if not 0<=tile<2048 or not isinstance(pixels,list) or len(pixels)!=64 or any(not isinstance(x,int) or not 0<=x<16 for x in pixels):
            raise RuntimeError(f'Invalid tile override {key}: {LEVEL_OVERRIDES/name}')
        off=tile*32
        for i in range(32):out[off+i]=(pixels[i*2]<<4)|pixels[i*2+1]
    return bytes(out)

def apply_packed_tile_overrides(asset,name):
    """Apply editor pixels to an already packed PC-FX 4bpp tile bank."""
    data=override_json(name)
    if data is None:return asset
    words=list(asset['tile_words']);highest=asset['tile_count']-1
    for key,pixels in data.get('tiles',{}).items():
        tile=int(key)
        if not 0<=tile<2048 or not isinstance(pixels,list) or len(pixels)!=64 or any(not isinstance(x,int) or not 0<=x<16 for x in pixels):
            raise RuntimeError(f'Invalid packed boss tile override {key}: {LEVEL_OVERRIDES/name}')
        while len(words)<(tile+1)*16:words.extend([0]*16)
        words[tile*16:(tile+1)*16]=ba.encode_bg_tile(tuple(pixels));highest=max(highest,tile)
    asset['tile_words']=words;asset['tile_count']=highest+1
    return asset

def packed_with_transparent_zero(asset):
    """Reserve BAT index zero for a genuinely transparent foreground cell.

    pack_bg_rgb() assigns index zero to the first visible arena tile.  Boss
    foreground defaults also use zero, so sharing that bank without shifting
    it paints the first wall/floor tile across VDC1 and hides VDC0.
    """
    out=dict(asset)
    out['tile_words']=[0]*16+list(asset['tile_words'])
    out['bat']=[(w&0xF000)|((w&0x0FFF)+1) for w in asset['bat']]
    out['tile_count']=asset['tile_count']+1
    return out

def background_plane_halves(level_dir):
    """The level's two background maps, left half first.

    Falls back to using one map for both halves when the level loads the same
    map twice, which is what several later slots do.
    """
    manifest=json.loads((level_dir/'manifest.json').read_text())
    layers=manifest.get('extra_layers',[])
    if not layers: return None
    def half(suffix):
        for i,l in enumerate(layers):
            if l['vram_destination'].endswith(suffix):
                f=next(level_dir.glob(f'extra_layer_{i:02d}_ROM_*_words_be.bin'))
                return ba.read_words_be(f)
        return None
    left=half('000'); right=half('040')
    if left is None: return None
    return left,(right if right is not None else left)


def b16(r,o): return int.from_bytes(r[o:o+2],'big')
def b32(r,o): return int.from_bytes(r[o:o+4],'big')

def emit_u16(f,n,v,cols=12,static=False):
    pre='static ' if static else ''
    f.write(f'{pre}const unsigned short {n}[{len(v)}] = {{\n')
    for i in range(0,len(v),cols): f.write('  '+', '.join(f'0x{x&0xffff:04X}' for x in v[i:i+cols])+',\n')
    f.write('};\n\n')
def emit_u8(f,n,v,cols=24,static=False):
    pre='static ' if static else ''
    f.write(f'{pre}const unsigned char {n}[{len(v)}] = {{\n')
    for i in range(0,len(v),cols): f.write('  '+', '.join(str(int(x)&255) for x in v[i:i+cols])+',\n')
    f.write('};\n\n')
def emit_s16(f,n,rows,cols=14,static=False):
    flat=[]
    for row in rows: flat.extend(row)
    pre='static ' if static else ''
    f.write(f'{pre}const short {n}[{len(flat)}] = {{\n')
    for i in range(0,len(flat),cols): f.write('  '+', '.join(str(int(x)) for x in flat[i:i+cols])+',\n')
    f.write('};\n\n')

def find_set_root(set_id): return next((KIT/'graphics/stage_sets').glob(f'set_{set_id:02d}_slots_*'))

def palette_for_level(slot):
    ld=KIT/f'graphics/levels/level_{slot:02d}'
    out=[ba.rgb_to_yuv(0,0,0)]*256
    with (ld/'palette.csv').open() as fh:
        for row in csv.DictReader(fh):
            i=int(row['index']); out[i]=ba.rgb_to_yuv(int(row['r']),int(row['g']),int(row['b']))
    return out

def parse_routes(r,stage_manifest):
    bylevel={}
    set_for={slot:sx for sx in stage_manifest for slot in sx['level_slots']}
    for slot in range(1,20):
        sx=set_for[slot]; route=[int(sx['entry_segment_by_level'][str(slot)])]; modes=[0]; quadrants=[0]
        p=b32(r,0x14CBA+(slot-1)*4)
        for _ in range(128):
            a=b16(r,p); bb=b16(r,p+2); c=b16(r,p+4); p+=6
            if a==0x8888: break
            route.append(bb&255); modes.append(c&255); quadrants.append(a&255)
        bylevel[slot]=(route,modes,quadrants)
    return bylevel

def build_sets(stage_manifest):
    sets=[]
    for sx in stage_manifest:
        root=find_set_root(sx['set_id']); first=sx['level_slots'][0]
        vram=apply_tile_overrides((KIT/f'graphics/levels/level_{first:02d}/assembled_vram.bin').read_bytes(),f'set_{sx["set_id"]:02d}_tiles.json')
        tiles=[tuple([0]*64)]; lookup={(0,tiles[0]):0}; bat=[]; collisions=[]
        for seg in range(sx['segment_count']):
            sd=root/f'segments/segment_{seg:02d}'
            words=ba.read_words_be(sd/'map_decoded_words_be.bin')
            segbat=[]
            default_words=foreground_words_240(sx['set_id'],words)
            edited_words=override_words(f'set_{sx["set_id"]:02d}_segment_{seg:02d}.json',default_words,32*30)
            for word in edited_words:segbat.append(ba.append_genesis_word(word,vram,tiles,lookup))
            bat.extend(segbat)
            cf=next(sd.glob('collision_ROM_*.bin')).read_bytes()[:1024]
            if sx['set_id']==3:
                cf=cf[32:]+cf[-32:]
            collisions.extend(cf)
        # Level-specific background layers are converted into this set's shared tile dictionary.
        bgs={}
        for slot in sx['level_slots']:
            ld=KIT/f'graphics/levels/level_{slot:02d}'; vv=apply_tile_overrides((ld/'assembled_vram.bin').read_bytes(),f'level_{slot:02d}_background_tiles.json')
            halves=background_plane_halves(ld)
            if halves:
                left,right=halves; bb=[]
                src_h=max(1,len(left)//32)
                for y in range(30):
                    if slot in CLOUD_BG_Y16_SLOTS and src_h>=28:
                        # Preserve the actual 224-line source (rows 0..27) and
                        # place it 16 pixels lower.  Fill the new top margin
                        # with the source sky row; do not repeat bottom art.
                        sy=0 if y<2 else min(y-2,27)
                    else:
                        crop=LEVEL3_ACTIVE_CROP_ROWS if slot==3 else 0
                        if y>=MD_VISIBLE_ROWS:
                            sy=min(MD_VISIBLE_ROWS-1+crop,src_h-1)
                        else:
                            sy=min(y+crop,src_h-1)
                    for half in (left,right):
                        for x in range(32): bb.append(ba.append_genesis_word(half[sy*32+x],vv,tiles,lookup))
            else: bb=[0]*(BG_PLANE_W*30)
            raw_bg=override_words(f'level_{slot:02d}_background.json',None,BG_PLANE_W*30)
            if raw_bg is not None:
                bb=[ba.append_genesis_word(word,vv,tiles,lookup) for word in raw_bg]
            bgs[slot]=bb
        tw=[]
        for t in tiles: tw.extend(ba.encode_bg_tile(t))
        sets.append(dict(set_id=sx['set_id'],slots=sx['level_slots'],segment_count=sx['segment_count'],tiles=tw,tile_count=len(tiles),bat=bat,collision=collisions,bgs=bgs))
        print('set',sx['set_id'],'tiles',len(tiles),'segments',sx['segment_count'])
    return sets

def parse_level_objects(r, routes, initdata):
    byinit={x['level_slot']:x for x in initdata}; levels=[]
    for slot in range(1,20):
        route=routes[slot][0]; width=len(route)*256
        crates=[]; gems=[]
        it=byinit[slot]
        for group in it['groups']:
            for rec in group['records']:
                mid=rec['metasprite_id']; x=rec['x']-128; y=rec['y']-128
                if mid==91: crates.append((x,y,rec.get('extra',0)))
                elif mid==97: gems.append((x,y))
        # dynamic gems
        p=b32(r,0x11AC4+(slot-1)*4)
        while b16(r,p)!=0x8888:
            seg,tile,x,y=[b16(r,p+i) for i in (0,2,4,6)]; p+=8
            wx=ba.world_x_for_trigger(seg,tile)+(x-0x180)
            if -64<=wx<width+320: gems.append((wx,y-128))
        # dynamic crates
        p=b32(r,0x13262+(slot-1)*4)
        while b16(r,p)!=0x8888:
            seg,tile,x,y,extra=[b16(r,p+i) for i in (0,2,4,6,8)]; p+=10
            wx=ba.world_x_for_trigger(seg,tile)+(x-0x180)
            if -64<=wx<width+320: crates.append((wx,y-128,extra))
        # enemies raw; variants assigned later
        enemies=[]; p=b32(r,0xD5CE+(slot-1)*4)
        while b16(r,p)!=0x8888:
            w=[b16(r,p+i*2) for i in range(11)]; p+=22
            seg,tmin,tmax,pool,gfx,x,y,base,sub,state,flags=w
            wx=ba.world_x_for_trigger(seg,tmin)+(x-0x180)
            if -96<=wx<width+384: enemies.append([wx,y-128,gfx,base,sub,state,flags])
        sp=it['normal_spawn']; spawn=(sp['x']-128,sp['y']-128)
        levels.append(dict(slot=slot,crates=crates,gems=gems,enemies=enemies,spawn=spawn))
        print('slot',slot,'route',len(route),'cr',len(crates),'gem',len(gems),'en',len(enemies),'spawn',spawn)
    return levels

def enemy_palette(r,gfx):
    pi=b16(r,0xF426+gfx*2); ptr=b32(r,0x72338+pi*4)
    return [ba.rgb_to_yuv(*ba.genesis_cram_rgb(b16(r,ptr+i*2))) for i in range(16)]

def build_enemy_variants(r,levels):
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text()); byid={m['id']:m for m in allm}
    keys=[]
    for lev in levels:
        for e in lev['enemies']:
            k=(e[2],e[3],e[4])
            if k not in keys: keys.append(k)
    variants=[]
    for idx,(gfx,base,sub) in enumerate(keys):
        frames=[]; masks=[]
        for anim in range(2):
            tbl=b32(r,0x7246C+gfx*4); ptr=b32(r,tbl+anim*4); raw=r[ptr:ptr+0x180]
            vv=bytearray(65536); addr=ba.vdp_addr(b32(r,0xF454+sub*4)); vv[addr:addr+len(raw)]=raw
            # The object record's metasprite base is a shape family, while the
            # graphics DMA table selects one of six VRAM tile banks. Most families
            # happen to be base+sub*2; gfx #5 does not. Resolve the metasprite by
            # matching the actual DMA destination tile and the base shape dimensions.
            want_tile=addr//32; bm=byid[base]; bw=bm['bbox'][2]-bm['bbox'][0]+1; bh=bm['bbox'][3]-bm['bbox'][1]+1
            cand=[]
            for mid in range(max(0,base-12),min(len(allm),base+18)):
                mm=byid[mid]
                if not mm['parts'] or mm['parts'][0]['tile_index']!=want_tile: continue
                mw=mm['bbox'][2]-mm['bbox'][0]+1; mh=mm['bbox'][3]-mm['bbox'][1]+1
                score=(0 if (mw,mh)==(bw,bh) else 100)+abs(mid-base)
                cand.append((score,mid,mm))
            m=min(cand,key=lambda x:x[0])[2] if cand else bm
            canvas=[[0]*64 for _ in range(64)]
            for part in m['parts']:
                ba.blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],canvas,part['x_offset'],part['y_offset'])
            words=[]; mask=0
            for cy in range(3):
                for cx in range(3):
                    cell=[[canvas[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                    if any(any(row) for row in cell): mask|=1<<(cy*3+cx)
                    words.extend(ba.encode_sprite_cell(cell))
            frames.extend(words); masks.append(mask)
        bbox=m.get('bbox',[0,0,23,31]); width=bbox[2]+1; height=bbox[3]+1
        variants.append(dict(key=(gfx,base,sub),words=frames,masks=masks,palette=enemy_palette(r,gfx),width=width,height=height))
        print('variant',idx,(gfx,base,sub),'wh',width,height,'mask',masks)
    keyidx={v['key']:i for i,v in enumerate(variants)}
    # localize variants per level and rewrite enemy records as [x,y,local,state,flags,width,height]
    for lev in levels:
        gids=[]
        for e in lev['enemies']:
            gi=keyidx[(e[2],e[3],e[4])]
            if gi not in gids:gids.append(gi)
        loc={gi:i for i,gi in enumerate(gids)}; lev['variant_ids']=gids
        rec=[]
        for e in lev['enemies']:
            gi=keyidx[(e[2],e[3],e[4])]; v=variants[gi]
            rec.append((e[0],e[1],loc[gi],e[5],e[6],v['width'],v['height']))
        lev['enemy_records']=rec
    return variants

def make_boss_background(slot):
    # The emulator capture is a 224-line Mega Drive active picture in a
    # 240-line screenshot: rows 0..15 are capture padding, not room graphics.
    # Crop that padding and keep the PC-FX's extra bottom 16 lines filled with
    # the room's own floor pattern.
    src=Image.open(BOSSREF/'boss1_arena.png').convert('RGB')
    for y in range(208,240):
        for x in range(128): src.putpixel((x,y),src.getpixel((x+128,y)))
    im=Image.new('RGB',(256,240),(0,0,0))
    im.paste(src.crop((0,16,256,240)),(0,0))
    # Extend the 224-line active picture one tile at a time.  Repeating a
    # 16-line strip incorrectly alternates two floor rows in the extra area.
    last_tile_row=im.crop((0,216,256,224))
    im.paste(last_tile_row,(0,224))
    im.paste(last_tile_row,(0,232))
    result=packed_with_transparent_zero(ba.pack_bg_rgb(im));result['bat'] += [0]*(32*32-len(result['bat']))
    result['bat']=override_words(f'boss_{slot:02d}_background.json',result['bat'],32*32)
    result=apply_packed_tile_overrides(result,f'boss_{slot:02d}_tiles.json')
    fg=override_words(f'boss_{slot:02d}_foreground.json',[0]*(32*32),32*32)
    return result,fg

def _largest_diff_component(ref,im):
    import numpy as np
    a=np.asarray(ref);b=np.asarray(im);d=np.any(a!=b,axis=2);seen=np.zeros(d.shape,bool);best=[]
    for y in range(d.shape[0]):
        for x in range(d.shape[1]):
            if not d[y,x] or seen[y,x]: continue
            stack=[(x,y)];seen[y,x]=1;pts=[]
            while stack:
                xx,yy=stack.pop();pts.append((xx,yy))
                for nx,ny in ((xx-1,yy),(xx+1,yy),(xx,yy-1),(xx,yy+1)):
                    if 0<=nx<d.shape[1] and 0<=ny<d.shape[0] and d[ny,nx] and not seen[ny,nx]:seen[ny,nx]=1;stack.append((nx,ny))
            # Bowser component is the huge lower-half difference; ignore title/crate deltas.
            if len(pts)>len(best) and max(p[1] for p in pts)>150:best=pts
    return best

def _diff_components(ref,im):
    """All four-connected changed regions, largest first."""
    import numpy as np
    a=np.asarray(ref);b=np.asarray(im);d=np.any(a!=b,axis=2);seen=np.zeros(d.shape,bool);out=[]
    for y in range(d.shape[0]):
        for x in range(d.shape[1]):
            if not d[y,x] or seen[y,x]: continue
            stack=[(x,y)];seen[y,x]=1;pts=[]
            while stack:
                xx,yy=stack.pop();pts.append((xx,yy))
                for nx,ny in ((xx-1,yy),(xx+1,yy),(xx,yy-1),(xx,yy+1)):
                    if 0<=nx<d.shape[1] and 0<=ny<d.shape[0] and d[ny,nx] and not seen[ny,nx]:seen[ny,nx]=1;stack.append((nx,ny))
            out.append(pts)
    return sorted(out,key=len,reverse=True)

def make_bowser_sprite():
    """Recover four walk poses and the breath pose from the real fight."""
    ref=Image.open(BOSSREF/'boss1_arena.png').convert('RGB')
    src=[]; freq={}
    frames=(('boss1_walk0.png',True,False),('boss1_walk1.png',True,False),
            ('boss1_walk2.png',True,False),('boss1_walk3.png',True,False),
            ('boss1_breath0.png',False,False),('boss1_breath1.png',False,False),
            ('boss1_breath2.png',False,False),('boss1_breath3.png',False,False),
            # The captured state-3 frame faces left. Normalize it to the same
            # right-facing storage convention as the walk/breath bank.
            ('boss1_hurt.png',True,True))
    for fn,mirror,is_hurt in frames:
        im=Image.open(BOSSREF/fn).convert('RGB')
        if is_hurt:
            # State 3 is metasprite 6 plus disconnected impact-puff pieces.
            # Retain every changed component local to Bowser, not only his
            # connected body component.
            pts=[p for comp in _diff_components(ref,im)
                 if len(comp)>=12 and min(y for x,y in comp)>=110
                 and max(y for x,y in comp)<210 for p in comp]
        else:
            pts=_largest_diff_component(ref,im)
        if not pts: raise RuntimeError(f'Could not isolate Bowser in {fn}')
        xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
        x0,y0=min(xs),min(ys)
        pix={}
        for x,y in pts:
            xx,yy=x-x0,y-y0
            if mirror: xx=95-xx
            if 0<=xx<96 and 0<=yy<96:
                c=im.getpixel((x,y)); pix[(xx,yy)]=c; freq[c]=freq.get(c,0)+1
        src.append(pix)
    # Index 0 remains transparent.  These components use exactly fifteen
    # visible Mega Drive colors, so no source color needs to be discarded.
    colors=[c for c,_ in sorted(freq.items(),key=lambda kv:-kv[1])][:15]
    words=[]; masks=[]
    for pix in src:
        idx=[[0]*96 for _ in range(96)]; mask=0
        for (x,y),c in pix.items():
            ci=min(range(len(colors)),key=lambda j:sum((colors[j][k]-c[k])**2 for k in range(3)))
            idx[y][x]=ci+1
        for cy in range(6):
            for cx in range(6):
                cell=[[idx[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                if any(any(row) for row in cell): mask|=1<<(cy*6+cx)
                words.extend(ba.encode_sprite_cell(cell))
        masks.append(mask)
    # The detached 42x18 flame is the second large changed component in the
    # breath capture (the first is Bowser, the third is Mario).  Store it as a
    # 3x2 grid of 16-pixel cells using Bowser's source palette.
    breath=Image.open(BOSSREF/'boss1_breath2.png').convert('RGB')
    flame=max((p for p in _diff_components(ref,breath)
               if 200<len(p)<1000 and min(y for x,y in p)>120),key=len)
    fx0,fy0=min(x for x,y in flame),min(y for x,y in flame)
    fidx=[[0]*48 for _ in range(32)]
    for x,y in flame:
        xx,yy=x-fx0,y-fy0
        if 0<=xx<48 and 0<=yy<32:
            c=breath.getpixel((x,y));ci=min(range(len(colors)),key=lambda j:sum((colors[j][k]-c[k])**2 for k in range(3)))
            fidx[yy][xx]=ci+1
    flamewords=[];flamemask=0
    for cy in range(2):
        for cx in range(3):
            cell=[[fidx[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
            if any(any(row) for row in cell): flamemask|=1<<(cy*3+cx)
            flamewords.extend(ba.encode_sprite_cell(cell))
    py=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
    py += [ba.rgb_to_yuv(0,0,0)]*(16-len(py))
    return words,masks,py[:16],flamewords,flamemask

def make_later_boss_assets():
    """Pack the five non-Bowser encounters captured from the Mega Drive.

    These are deliberately separate banks: the original loader selects a
    different graphics, palette and room-map pointer for every boss slot.
    """
    out=[]
    spawns={8:(96,46),10:(8,20),13:(125,57),16:(76,53),19:(88,49)}
    for slot in (8,10,13,16,19):
        root=BOSSREF/'later'
        bg=packed_with_transparent_zero(ba.pack_bg_rgb(Image.open(root/f'boss{slot}_arena.png').convert('RGB')))
        # Boss maps use a 32-row BAT so vertically scrolling rooms wrap on
        # complete source data.  Captured 240-line rooms have two blank rows;
        # slot 19 is replaced below by the exact two VDP planes.
        bg['bat'] += [0]*(32*32-len(bg['bat']))
        fg=dict(tile_words=[0]*16,bat=[0]*(32*32),tile_count=1)
        if slot==19:
            layer=BOSSREF/'layers/boss19'
            vram=apply_tile_overrides((layer/'md19_vram.bin').read_bytes(),'boss_19_tiles.json')
            dump=json.loads((layer/'md19_dump.json').read_text())
            palette=[ba.rgb_to_yuv(0,0,0)]*256
            for i,wc in enumerate(dump['cram']):palette[i]=ba.rgb_to_yuv(*ba.genesis_cram_rgb(wc))
            tiles=[tuple([0]*64)];lookup={(0,tiles[0]):0}
            maps={}
            for name in ('background','foreground'):
                raw=ba.read_words_be(layer/f'md19_{name}_words_be.bin')
                # The dumped VDP plane is 64x32.  The live room occupies the
                # first 32 columns; the second half is inactive work space.
                # V28 only displays rows 0..27.  Rows 28..31 in the live VDP
                # allocation are unrelated scratch tiles (foreground) or
                # blank (background), never room artwork.  PC-FX is 240 lines,
                # so continue the final visible tile row into the extension
                # and hidden wrap rows instead of exposing that work area.
                if name=='foreground':
                    # The platform ends at source row 25. Rows 26-27 are
                    # transparent padding and rows 28+ are object work tiles.
                    default=[raw[y*64+x] if y<26 else 0 for y in range(32) for x in range(32)]
                else:
                    default=[raw[min(y,27)*64+x] for y in range(32) for x in range(32)]
                source=override_words(f'boss_19_{name}.json',default,32*32)
                maps[name]=[ba.append_genesis_word(w,vram,tiles,lookup) for w in source]
            tile_words=[]
            for t in tiles:tile_words.extend(ba.encode_bg_tile(t))
            bg=dict(palette=palette,tile_words=tile_words,bat=maps['background'],tile_count=len(tiles))
            # Both planes deliberately reference one shared tile bank/palette.
            fg=dict(tile_words=tile_words,bat=maps['foreground'],tile_count=len(tiles))
        else:
            bg['bat']=override_words(f'boss_{slot:02d}_background.json',bg['bat'],32*32)
            bg=apply_packed_tile_overrides(bg,f'boss_{slot:02d}_tiles.json')
            fg['bat']=override_words(f'boss_{slot:02d}_foreground.json',fg['bat'],32*32)
            # Foreground and background share a bank so a copied tile has the
            # same meaning in both editor layers.
            fg['tile_words']=bg['tile_words'];fg['tile_count']=bg['tile_count']
        normal=Image.open(root/f'boss{slot}_pose1.png').convert('RGBA')
        hit_path=root/f'boss{slot}_pose_hit.png'
        hit=Image.open(hit_path).convert('RGBA') if hit_path.exists() else normal.copy()
        images=[]
        for pose in (normal,hit):
            box=pose.getchannel('A').getbbox()
            if not box: raise RuntimeError(f'empty boss {slot} sprite')
            images.append(pose.crop(box))
        w=((max(im.width for im in images)+15)//16)*16
        h=((max(im.height for im in images)+15)//16)*16
        freq={}
        for im in images:
            for r,g,b,a in im.getdata():
                if a: freq[(r,g,b)]=freq.get((r,g,b),0)+1
        colors=[c for c,_ in sorted(freq.items(),key=lambda kv:-kv[1])[:15]]
        if slot==19:
            dump=json.loads((BOSSREF/'layers/boss19/md19_dump.json').read_text())
            colors=[ba.genesis_cram_rgb(w) for w in dump['cram'][17:32]]
        words=[];masks=[];cols=w//16;rows=h//16
        for im in images:
            idx=[[0]*w for _ in range(h)]
            pix=im.load(); ox=(w-im.width)//2; oy=h-im.height
            for y in range(im.height):
                for x in range(im.width):
                    r,g,b,a=pix[x,y]
                    if not a: continue
                    c=min(range(len(colors)),key=lambda j:sum((colors[j][k]-(r,g,b)[k])**2 for k in range(3)))
                    idx[oy+y][ox+x]=c+1
            mask=0
            for cy in range(rows):
                for cx in range(cols):
                    cell=[[idx[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                    if any(any(row) for row in cell): mask|=1<<(cy*cols+cx)
                    words.extend(ba.encode_sprite_cell(cell))
            masks.append(mask)
        pal=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
        pal += [ba.rgb_to_yuv(0,0,0)]*(16-len(pal))
        out.append(dict(slot=slot,bg=bg,fg=fg,words=words,palette=pal[:16],masks=masks,
                        cols=cols,rows=rows,spawn=spawns[slot]))
    return out

def boss_tables(r):
    # Source tables used by the special-level loader at $6CDE-$6D6C and
    # boss collision code around $15C56. These are kept in generated output
    # so the PC-FX behavior remains auditable against the 68000 disassembly.
    hp1=[b16(r,0x6D6E+i*2) for i in range(19)]
    hp2=[b16(r,0x6D94+i*2) for i in range(19)]
    params=[]; offsets=[]; altspawn=[]
    for i in range(19):
        params.extend(b16(r,0x6DBA+i*6+j*2) for j in range(3))
        offsets.extend(int.from_bytes(r[0x15D20+i*4+j*2:0x15D22+i*4+j*2],'big',signed=True) for j in range(2))
        altspawn.extend(b16(r,0x1DC56+i*4+j*2) for j in range(2))
    # 68K handler entry points selected by the level dispatch at $748E.
    handlers=[0]*19
    for slot,addr in {3:0x7526,6:0x7526,8:0x806A,10:0x8764,13:0x8CEC,16:0x99BE,19:0x940E}.items(): handlers[slot-1]=addr
    collisions=[]
    for slot in (3,8,10,13,16,19):
        p=int.from_bytes(r[0x7038C+(slot-1)*4:0x70390+(slot-1)*4],'big')
        collisions.extend(r[p:p+1024])
    return hp1,hp2,params,offsets,altspawn,handlers,collisions

def make_level6_platform_words():
    """Exact metasprite 742 ($1D962), six source tiles 467..472."""
    vram=(KIT/'graphics/levels/level_06/assembled_vram.bin').read_bytes()
    canvas=[[0]*48 for _ in range(16)]
    for tx in range(6):
        pix=ba.tilepix(vram,467+tx)
        for y in range(8):
            for x in range(8): canvas[y][tx*8+x]=pix[y*8+x]
    words=[]
    for cx in range(3):
        words.extend(ba.encode_sprite_cell(
            [[canvas[y][cx*16+x] for x in range(16)] for y in range(16)]))
    return words

def make_final_boss_aux_words():
    """Exact 24x24 ring/hammer objects from the final-room live VDP bank."""
    vram=(BOSSREF/'layers/boss19/md19_vram.bin').read_bytes();out=[]
    for base in (1216,1225):
        canvas=[[0]*32 for _ in range(32)]
        for tx in range(3):
            for ty in range(3):
                pix=ba.tilepix(vram,base+tx*3+ty)
                for y in range(8):
                    for x in range(8):canvas[ty*8+y][tx*8+x]=pix[y*8+x]
        for cy in range(2):
            for cx in range(2):out.extend(ba.encode_sprite_cell([[canvas[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]))
    cram=json.loads((BOSSREF/'layers/boss19/md19_dump.json').read_text())['cram']
    palette=[ba.rgb_to_yuv(*ba.genesis_cram_rgb(w)) for w in cram[16:32]]
    return out[:256],out[256:],palette

def make_boss_item_words():
    """Distinct throwable silhouettes used by $806A (pot) and $99BE (bell)."""
    def bank(kind):
        c=[[0]*32 for _ in range(32)]
        if kind=='pot':
            # Metasprite 428 is an 8x16 pot. Enlarge it to a readable 16x24
            # PC-FX cell while retaining the narrow neck/rounded body.
            for y,(x0,x1) in enumerate([(12,19),(10,21),(10,21),(12,19),(8,23),(7,24),(6,25),(6,25),(7,24),(8,23),(9,22),(10,21),(11,20),(12,19),(13,18),(13,18)]):
                yy=y+8
                for x in range(x0,x1+1):c[yy][x]=2 if x in (x0,x1) else (4 if y<4 else 3)
        else:
            for y in range(7,24):
                half=4+max(0,(y-8)//3);x0=16-half;x1=15+half
                for x in range(x0,x1+1):c[y][x]=2 if x in (x0,x1) else 5
            for x in range(11,21):c[6][x]=3
            for x in range(9,23):c[24][x]=3
            for y in range(24,28):c[y][15]=c[y][16]=2
        words=[]
        for cy in range(2):
            for cx in range(2):words.extend(ba.encode_sprite_cell([[c[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]))
        return words
    rgb=[(0,0,0),(49,49,49),(87,49,0),(206,119,0),(255,206,87),(255,174,0)]+[(0,0,0)]*10
    return bank('pot'),bank('bell'),[ba.rgb_to_yuv(*c) for c in rgb]

def main():
    r=ROMPATH.read_bytes(); sm=json.load(open(KIT/'graphics/stage_sets/stage_sets_manifest.json')); init=json.load(open(KIT/'graphics/metasprites/level_init_objects_and_spawns.json'))
    routes=parse_routes(r,sm); sets=build_sets(sm); levels=parse_level_objects(r,routes,init); variants=build_enemy_variants(r,levels)
    set_index={slot:s['set_id']-1 for s in sets for slot in s['slots']}
    palettes={slot:palette_for_level(slot) for slot in range(1,20)}
    bowserrooms=[make_boss_background(slot) for slot in (3,6)]; bosswords,bossmasks,bosspal,bossflamewords,bossflamemask=make_bowser_sprite();laterboss=make_later_boss_assets();level6platform=make_level6_platform_words();finalring,finalhammer,finalauxpal=make_final_boss_aux_words();potwords,bellwords,itempal=make_boss_item_words()
    bosshp1,bosshp2,bossparams,bossoffsets,bossaltspawn,bosshandlers,bosscollision=boss_tables(r)
    out=PROJECT/'src/game_levels_generated.c'; h=out.with_suffix('.h')
    h.write_text('''#ifndef SMB_PCFX_GAME_LEVELS_GENERATED_H\n#define SMB_PCFX_GAME_LEVELS_GENERATED_H\n\ntypedef struct { const unsigned short *tile_words; const unsigned short *segment_bat; const unsigned char *collision; unsigned short tile_count; unsigned char segment_count; } StageSetData;\ntypedef struct { const unsigned short *words; const unsigned short *palette; unsigned short mask0,mask1; unsigned char width,height; } EnemyVariantData;\ntypedef struct { unsigned char set_index, route_len; const unsigned char *route,*route_modes,*route_quadrants; const unsigned short *palette,*bg_bat; short spawn_x,spawn_y; unsigned short crate_count,gem_count,enemy_count; const short *crates,*gems,*enemies; unsigned char variant_count; const unsigned char *variant_ids; } GameLevelData;\nextern const StageSetData g_stage_sets[12];\nextern const GameLevelData g_game_levels[19];\nextern const EnemyVariantData g_enemy_variants[];\nextern const unsigned short g_enemy_variant_count;\nextern const unsigned char g_boss_level_flags[19];\nextern const unsigned short g_boss_bg_palette[256],g_boss_bg_tile_words[] ,g_boss_bg_bat[];\nextern const unsigned short g_boss_bg_tile_count;\nextern const unsigned short g_bowser_words[],g_bowser_palette[16];\nextern const unsigned long long g_bowser_masks[5];\nextern const unsigned short g_boss_hp_1p[19],g_boss_hp_2p[19];\nextern const unsigned short g_boss_params[57],g_boss_alt_spawn[38];\nextern const short g_boss_hit_offsets[38];\nextern const unsigned long g_boss_handler_address[19];\n#define ENEMY_FRAME_WORDS 576\n#define ENEMY_VARIANT_VRAM_WORDS 1152\n#define BOWSER_FRAME_WORDS 2304\n#define BOWSER_FRAME_COUNT 5\n#endif\n''')
    # Keep the generated declarations tied to the actual captured pose count.
    # Extend the legacy generated declaration block for the complete captured
    # boss-1 bank and its detached flame metasprite.
    hs=h.read_text().replace('g_bowser_masks[5]','g_bowser_masks[9]').replace('BOWSER_FRAME_COUNT 5','BOWSER_FRAME_COUNT 9')
    hs=hs.replace('typedef struct { unsigned char set_index, route_len;',
        'typedef struct { const unsigned short *palette,*tile_words,*bg_bat,*fg_bat; unsigned short tile_count; } BossRoomData;\ntypedef struct { const unsigned short *bg_palette,*bg_tile_words,*bg_bat,*fg_tile_words,*fg_bat,*sprite_words,*sprite_palette; unsigned short bg_tile_count,fg_tile_count; unsigned long long sprite_mask,sprite_hit_mask; unsigned char cols,rows; short spawn_x,spawn_y; } BossVisualData;\ntypedef struct { unsigned char set_index, route_len;')
    hs=hs.replace('g_bowser_words[],g_bowser_palette[16];','g_bowser_words[],g_bowser_flame_words[],g_bowser_palette[16];\nextern const unsigned short g_level6_platform_words[192];')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned char g_bowser_flame_mask;\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned long g_boss_handler_address[19];',
        'extern const unsigned long g_boss_handler_address[19];\nextern const BossVisualData g_later_boss_visuals[5];\nextern const unsigned char g_boss_collision[6144];')
    hs=hs.replace('extern const unsigned char g_bowser_flame_mask;',
        'extern const BossRoomData g_bowser_rooms[2];\nextern const unsigned short g_final_ring_words[256],g_final_hammer_words[256],g_final_aux_palette[16],g_boss_pot_words[256],g_boss_bell_words[256],g_boss_item_palette[16];\nextern const unsigned char g_bowser_flame_mask;')
    h.write_text(hs)
    with out.open('w') as f:
        f.write('#include "game_levels_generated.h"\n\n')
        for s in sets:
            q=f'set{s["set_id"]:02d}'; emit_u16(f,q+'_tiles',s['tiles'],static=True); emit_u16(f,q+'_bat',s['bat'],static=True); emit_u8(f,q+'_col',s['collision'],static=True)
            for slot,bb in s['bgs'].items(): emit_u16(f,f'level{slot:02d}_bg',bb,static=True)
        for slot in range(1,20):
            route,modes,quads=routes[slot]; emit_u8(f,f'level{slot:02d}_route',route,static=True);emit_u8(f,f'level{slot:02d}_modes',modes,static=True);emit_u8(f,f'level{slot:02d}_quads',quads,static=True);emit_u16(f,f'level{slot:02d}_pal',palettes[slot],static=True)
            lev=levels[slot-1];emit_s16(f,f'level{slot:02d}_crates',lev['crates'],static=True);emit_s16(f,f'level{slot:02d}_gems',lev['gems'],static=True);emit_s16(f,f'level{slot:02d}_enemies',lev['enemy_records'],static=True);emit_u8(f,f'level{slot:02d}_variants',lev['variant_ids'],static=True)
        for i,v in enumerate(variants): emit_u16(f,f'enemy_variant_{i:02d}_words',v['words'],static=True);emit_u16(f,f'enemy_variant_{i:02d}_pal',v['palette'],static=True)
        f.write(f'const unsigned short g_enemy_variant_count={len(variants)};\n')
        f.write('const EnemyVariantData g_enemy_variants[] = {\n')
        for i,v in enumerate(variants): f.write(f'  {{enemy_variant_{i:02d}_words,enemy_variant_{i:02d}_pal,0x{v["masks"][0]:04X},0x{v["masks"][1]:04X},{v["width"]},{v["height"]}}},\n')
        f.write('};\n\n')
        f.write('const StageSetData g_stage_sets[12] = {\n')
        for s in sets: q=f'set{s["set_id"]:02d}'; f.write(f'  {{{q}_tiles,{q}_bat,{q}_col,{s["tile_count"]},{s["segment_count"]}}},\n')
        f.write('};\n\n')
        f.write('const GameLevelData g_game_levels[19] = {\n')
        for slot in range(1,20):
            lev=levels[slot-1]; route=routes[slot][0]; s=sets[set_index[slot]]; bg=f'level{slot:02d}_bg';
            f.write(f'  {{{set_index[slot]},{len(route)},level{slot:02d}_route,level{slot:02d}_modes,level{slot:02d}_quads,level{slot:02d}_pal,{bg},{lev["spawn"][0]},{lev["spawn"][1]},{len(lev["crates"])},{len(lev["gems"])},{len(lev["enemy_records"])},level{slot:02d}_crates,level{slot:02d}_gems,level{slot:02d}_enemies,{len(lev["variant_ids"])},level{slot:02d}_variants}},\n')
        f.write('};\n\n')
        flags=[1 if s in (3,6,8,10,13,16,19) else 0 for s in range(1,20)]; emit_u8(f,'g_boss_level_flags',flags)
        for ri,(room,fg) in enumerate(bowserrooms):
            q=f'bowser_room{ri}';emit_u16(f,q+'_palette',room['palette'],static=True);emit_u16(f,q+'_tiles',room['tile_words'],static=True);emit_u16(f,q+'_bg_bat',room['bat'],static=True);emit_u16(f,q+'_fg_bat',fg,static=True)
        f.write('const BossRoomData g_bowser_rooms[2]={\n')
        for ri,(room,fg) in enumerate(bowserrooms):
            q=f'bowser_room{ri}';f.write(f'  {{{q}_palette,{q}_tiles,{q}_bg_bat,{q}_fg_bat,{room["tile_count"]}}},\n')
        f.write('};\n\n')
        emit_u16(f,'g_bowser_words',bosswords);emit_u16(f,'g_bowser_flame_words',bossflamewords);emit_u16(f,'g_bowser_palette',bosspal);f.write('const unsigned long long g_bowser_masks[9]={'+','.join(f'0x{x:09X}ULL' for x in bossmasks)+'};\n');f.write(f'const unsigned char g_bowser_flame_mask=0x{bossflamemask:02X};\n');emit_u16(f,'g_level6_platform_words',level6platform);emit_u16(f,'g_final_ring_words',finalring);emit_u16(f,'g_final_hammer_words',finalhammer);emit_u16(f,'g_final_aux_palette',finalauxpal);emit_u16(f,'g_boss_pot_words',potwords);emit_u16(f,'g_boss_bell_words',bellwords);emit_u16(f,'g_boss_item_palette',itempal)
        for b in laterboss:
            q=f'boss{b["slot"]}'
            emit_u16(f,q+'_bg_palette',b['bg']['palette'],static=True);emit_u16(f,q+'_bg_tiles',b['bg']['tile_words'],static=True);emit_u16(f,q+'_bg_bat',b['bg']['bat'],static=True)
            emit_u16(f,q+'_fg_tiles',b['fg']['tile_words'],static=True);emit_u16(f,q+'_fg_bat',b['fg']['bat'],static=True)
            emit_u16(f,q+'_sprite_words',b['words'],static=True);emit_u16(f,q+'_sprite_palette',b['palette'],static=True)
        f.write('const BossVisualData g_later_boss_visuals[5]={\n')
        for b in laterboss:
            q=f'boss{b["slot"]}'; f.write(f'  {{{q}_bg_palette,{q}_bg_tiles,{q}_bg_bat,{q}_fg_tiles,{q}_fg_bat,{q}_sprite_words,{q}_sprite_palette,{b["bg"]["tile_count"]},{b["fg"]["tile_count"]},0x{b["masks"][0]:016X}ULL,0x{b["masks"][1]:016X}ULL,{b["cols"]},{b["rows"]},{b["spawn"][0]},{b["spawn"][1]}}},\n')
        f.write('};\n\n');emit_u8(f,'g_boss_collision',bosscollision)
        emit_u16(f,'g_boss_hp_1p',bosshp1); emit_u16(f,'g_boss_hp_2p',bosshp2)
        emit_u16(f,'g_boss_params',bossparams); emit_s16(f,'g_boss_hit_offsets',[bossoffsets])
        emit_u16(f,'g_boss_alt_spawn',bossaltspawn)
        f.write('const unsigned long g_boss_handler_address[19] = {'+', '.join(f'0x{x:08X}UL' for x in bosshandlers)+'};\n')
    print('wrote',out,out.stat().st_size)

if __name__=='__main__': main()
