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
LEVEL_DEFAULTS=Path(os.environ.get('LEVEL_DEFAULTS',str(PROJECT/'level_defaults')))

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

def edit_json(root,name):
    path=root/name
    if not path.exists(): return None
    data=json.loads(path.read_text())
    if data.get('version')!=1: raise RuntimeError(f'Unsupported level edit version in {path}')
    return data

def override_json(name):return edit_json(LEVEL_OVERRIDES,name)
def default_json(name):return edit_json(LEVEL_DEFAULTS,name)

def override_words(name,default,expected):
    promoted=default_json(name)
    if promoted is not None:
        default=promoted.get('words')
    data=override_json(name)
    if promoted is None and data is None:return default
    words=default if data is None else data.get('words')
    if not isinstance(words,list) or len(words)!=expected or any(not isinstance(x,int) or not 0<=x<=0xFFFF for x in words):
        raise RuntimeError(f'Invalid {expected}-word tilemap override: {LEVEL_OVERRIDES/name}')
    return words

def apply_collision_overrides(name,collision):
    """Apply sparse 8x8 collision-cell edits to an extracted segment grid."""
    out=bytearray(collision)
    for root,data in ((LEVEL_DEFAULTS,default_json(name)),(LEVEL_OVERRIDES,override_json(name))):
        if data is None:continue
        cells=data.get('cells',{})
        if not isinstance(cells,dict):
            raise RuntimeError(f'Invalid collision override: {root/name}')
        for key,value in cells.items():
            try:index=int(key)
            except (TypeError,ValueError):raise RuntimeError(f'Invalid collision cell {key!r}: {root/name}')
            if not 0<=index<len(out) or not isinstance(value,int) or not 0<=value<=0xFF:
                raise RuntimeError(f'Invalid collision cell {key!r}: {root/name}')
            out[index]=value
    return bytes(out)

def apply_tile_overrides(vram,name):
    out=bytearray(vram)
    for root,data in ((LEVEL_DEFAULTS,default_json(name)),(LEVEL_OVERRIDES,override_json(name))):
        if data is None:continue
        for key,pixels in data.get('tiles',{}).items():
            tile=int(key)
            if not 0<=tile<2048 or not isinstance(pixels,list) or len(pixels)!=64 or any(not isinstance(x,int) or not 0<=x<16 for x in pixels):
                raise RuntimeError(f'Invalid tile override {key}: {root/name}')
            off=tile*32
            for i in range(32):out[off+i]=(pixels[i*2]<<4)|pixels[i*2+1]
    return bytes(out)

def apply_packed_tile_overrides(asset,name):
    """Apply editor pixels to an already packed PC-FX 4bpp tile bank."""
    words=list(asset['tile_words']);highest=asset['tile_count']-1
    for root,data in ((LEVEL_DEFAULTS,default_json(name)),(LEVEL_OVERRIDES,override_json(name))):
        if data is None:continue
        for key,pixels in data.get('tiles',{}).items():
            tile=int(key)
            if not 0<=tile<2048 or not isinstance(pixels,list) or len(pixels)!=64 or any(not isinstance(x,int) or not 0<=x<16 for x in pixels):
                raise RuntimeError(f'Invalid packed boss tile override {key}: {root/name}')
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

def canonicalize_packed_palettes(words,source_bat):
    """Restore the palette line belonging to each packed tile.

    The editor historically copied the 12-bit tile number without the upper
    palette bits.  Packed RGB tiles are keyed by (palette,pixels), therefore a
    tile has one authoritative palette line and recovering it is lossless.
    """
    palette_by_tile={w&0x0fff:w&0xf000 for w in source_bat}
    return [(w&0x0fff)|palette_by_tile.get(w&0x0fff,w&0xf000) for w in words]

def make_dumped_boss_planes(slot):
    """Convert sprite-free live VDP planes for the last two arenas.

    Only the active 32 columns are copied.  Level 16's rows 28..31 are VDP
    object/scratch work, so its final visible floor row is continued for the
    PC-FX extension.  The final boss background is a true 32-row scrolling
    texture and is intentionally retained in full.
    """
    layer=BOSSREF/f'layers/boss{slot}'
    vram=(layer/f'md{slot}_vram.bin').read_bytes()
    dump=json.loads((layer/f'md{slot}_dump.json').read_text())
    palette=[ba.rgb_to_yuv(0,0,0)]*256
    for i,wc in enumerate(dump['cram']):palette[i]=ba.rgb_to_yuv(*ba.genesis_cram_rgb(wc))
    tiles=[tuple([0]*64)];lookup={(0,tiles[0]):0};maps={}
    for name in ('background','foreground'):
        raw=ba.read_words_be(layer/f'md{slot}_{name}_words_be.bin')
        source=[]
        for y in range(32):
            for x in range(32):
                if slot==16 and name=='foreground' and y>=28:
                    # Repeat the complete final 16-pixel floor tile, not only
                    # its lower 8-pixel chevron half.
                    word=raw[(26+(y-28)%2)*64+x]
                elif slot==19 and name=='foreground' and y>=30:word=0
                else:word=raw[y*64+x]
                source.append(word)
        maps[name]=[ba.append_genesis_word(w,vram,tiles,lookup) for w in source]
    tile_words=[]
    for tile in tiles:tile_words.extend(ba.encode_bg_tile(tile))
    return dict(palette=palette,tile_words=tile_words,tile_count=len(tiles),maps=maps,tiles=tiles)

def boss13_sprite_free_bat(bat):
    """Remove the two carrier sprites captured into the level-13 arena map.

    The level-13 reference was captured with the Mega Drive's two moving
    carriers enabled.  They are rebuilt as live PC-FX sprites, so retaining
    them in the background map creates two inert duplicates.  The arena's
    teal wall texture repeats on the 8x8 tile grid; copy clean wall cells from
    the same columns to restore the two 64x24 captured regions.  This occurs
    after level defaults/overrides are applied, ensuring a saved default map
    cannot restore the baked sprites.
    """
    out=list(bat)
    for (dx,dy),(sx,sy) in (((0,8),(0,14)),((24,23),(24,15))):
        for row in range(3):
            out[(dy+row)*32+dx:(dy+row)*32+dx+8]=out[(sy+row)*32+sx:(sy+row)*32+sx+8]
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
def emit_u64(f,n,v,cols=4,static=False):
    pre='static ' if static else ''
    f.write(f'{pre}const unsigned long long {n}[{len(v)}] = {{\n')
    for i in range(0,len(v),cols): f.write('  '+', '.join(f'0x{x:011X}ULL' for x in v[i:i+cols])+',\n')
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
            cf=apply_collision_overrides(f'set_{sx["set_id"]:02d}_segment_{seg:02d}_collision.json',cf)
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

def make_monkey_banana(r):
    """Pack metasprite $1E3 created by monkey handler $C31A."""
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text());m={x['id']:x for x in allm}[0x1E3]
    tbl=b32(r,0x7246C+6*4);ptr=b32(r,tbl+3*4);next_ptr=b32(r,tbl+4*4)
    addr=ba.vdp_addr(b32(r,0xF454));vv=bytearray(65536);vv[addr:addr+next_ptr-ptr]=r[ptr:next_ptr]
    canvas=[[0]*16 for _ in range(16)]
    for part in m['parts']:
        ba.blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],canvas,part['x_offset'],part['y_offset']+4)
    return ba.encode_sprite_cell(canvas)

def make_monkey_special_frames(r):
    """Pack selector 4 (banana throw) and selector 5 (40x40 death pose)."""
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text());byid={m['id']:m for m in allm}
    gfx,sub=6,0;tbl=b32(r,0x7246C+gfx*4);addr=ba.vdp_addr(b32(r,0xF454+sub*4));want_tile=addr//32
    frames=[];masks=[]
    for anim,shape_base in ((4,261),(5,255)):
        bw=byid[shape_base]['bbox'][2]+1;bh=byid[shape_base]['bbox'][3]+1;candidates=[]
        for m in allm:
            if m['parts'] and m['parts'][0]['tile_index']==want_tile and m['bbox'][2]+1==bw and m['bbox'][3]+1==bh:
                candidates.append((abs(m['id']-shape_base),m))
        shape=min(candidates,key=lambda x:x[0])[1] if candidates else byid[shape_base]
        ptr=b32(r,tbl+anim*4)
        # Selector 5 is the table's final entry; its data ends where gfx 7's
        # first frame begins, not at the following null table slot.
        next_ptr=b32(r,tbl+(anim+1)*4) if anim<5 else b32(r,b32(r,0x7246C+7*4))
        raw_len=next_ptr-ptr
        vv=bytearray(65536);vv[addr:addr+raw_len]=r[ptr:next_ptr];canvas=[[0]*48 for _ in range(48)]
        for part in shape['parts']:
            ba.blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],canvas,part['x_offset'],part['y_offset'])
        mask=0
        for cy in range(3):
            for cx in range(3):
                cell=[[canvas[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                if any(any(row) for row in cell):mask|=1<<(cy*3+cx)
                frames.extend(ba.encode_sprite_cell(cell))
        masks.append(mask)
    return frames,masks

def make_crate_walk_frames(r):
    """Pack crate-mimic selectors 4/5, its two exposed walking poses."""
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text());byid={m['id']:m for m in allm}
    gfx,sub=1,1;tbl=b32(r,0x7246C+gfx*4);addr=ba.vdp_addr(b32(r,0xF454+sub*4));want_tile=addr//32
    candidates=[m for m in allm if m['parts'] and m['parts'][0]['tile_index']==want_tile and m['bbox'][2]+1==32 and m['bbox'][3]+1==40]
    shape=min(candidates,key=lambda m:abs(m['id']-231)) if candidates else byid[231]
    frames=[];masks=[]
    for anim in (4,5):
        ptr=b32(r,tbl+anim*4);next_ptr=b32(r,tbl+(anim+1)*4) if anim<5 else ptr+0x280
        vv=bytearray(65536);vv[addr:addr+next_ptr-ptr]=r[ptr:next_ptr];canvas=[[0]*48 for _ in range(48)]
        for part in shape['parts']:
            ba.blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],canvas,part['x_offset'],part['y_offset'])
        mask=0
        for cy in range(3):
            for cx in range(3):
                cell=[[canvas[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                if any(any(row) for row in cell):mask|=1<<(cy*3+cx)
                frames.extend(ba.encode_sprite_cell(cell))
        masks.append(mask)
    return frames,masks

def build_enemy_variants(r,levels):
    # Graphics family 0 is the shared Goomba/Koopa family. Its source table
    # contains two walking poses only; selector 3 is not a death/hit image.
    # All other runtime enemy families provide a third impact pose.
    no_hit_frame_gfx={0}
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text()); byid={m['id']:m for m in allm}
    keys=[]
    for lev in levels:
        for e in lev['enemies']:
            k=(e[2],e[3],e[4])
            if k not in keys: keys.append(k)
    variants=[]
    for idx,(gfx,base,sub) in enumerate(keys):
        frames=[]; masks=[]
        # Most handlers use selector 3 for their attack/hit pose.  Graphics
        # family 4 (the green pterodactyl) is different: its table has only
        # selectors 0, 1 and 2; entry 3 is null.  Selector 0 is the narrow
        # hanging pose, while 1/2 are the two wide flying poses (the second
        # also remains available as the impact/defeat image).
        anim_selectors=(0,1,2) if gfx==4 else (0,1,3)
        for anim in anim_selectors:
            tbl=b32(r,0x7246C+gfx*4); ptr=b32(r,tbl+anim*4); next_ptr=b32(r,tbl+(anim+1)*4)
            addr=ba.vdp_addr(b32(r,0xF454+sub*4));want_tile=addr//32
            # Resolve the shape first; it also provides a safe exact minimum
            # length for families whose final animation pointer has no
            # following frame pointer in the ROM table.
            # Family 4 changes shape as well as graphics when it takes off.
            # The encounter stores base 231 (32x40 hanging layout).  Its
            # 24-tile flying layout is metasprite 273 (48x32), i.e. base+42.
            # Treating those tiles as the superficially plausible 32x48
            # layout transposed the final eight tiles and left debris below
            # the left wing.
            if gfx==4 and anim!=0:
                shape_base=base+42       # pterodactyl: 32x40 -> 48x32
            elif gfx==1 and anim==1:
                shape_base=base+6        # crate: 24x24 -> 24x32
            elif gfx==1 and anim==3:
                shape_base=base+24       # crate legs: 32x40
            elif gfx==3 and anim==1:
                shape_base=base-42       # floor attacker rises: 32x32
            elif gfx==3 and anim==3:
                shape_base=base-36       # floor attacker strike: 32x40
            else:
                shape_base=base
            bm=byid[shape_base]; bw=bm['bbox'][2]-bm['bbox'][0]+1; bh=bm['bbox'][3]-bm['bbox'][1]+1
            cand=[]
            for mid in range(max(0,shape_base-12),min(len(allm),shape_base+18)):
                mm=byid[mid]
                if not mm['parts'] or mm['parts'][0]['tile_index']!=want_tile: continue
                mw=mm['bbox'][2]-mm['bbox'][0]+1; mh=mm['bbox'][3]-mm['bbox'][1]+1
                score=(0 if (mw,mh)==(bw,bh) else 100)+abs(mid-shape_base)
                cand.append((score,mid,mm))
            m=min(cand,key=lambda x:x[0])[2] if cand else bm
            shape_len=max((p['tile_index']+p['width_tiles']*p['height_tiles']-want_tile)*32 for p in m['parts'])
            # Each graphics family has its own DMA size. The former fixed
            # 0x180-byte copy happened to fit small enemies but truncated the
            # 20/24/30-tile actors in levels 7 and 8 to one corner. Adjacent
            # frame pointers are the exact packed lengths used by the ROM.
            raw_len=next_ptr-ptr
            if raw_len<=0 or raw_len>0x800 or raw_len%32:
                raw_len=shape_len
            if raw_len<shape_len: raw_len=shape_len
            raw=r[ptr:ptr+raw_len]
            vv=bytearray(65536);vv[addr:addr+len(raw)]=raw
            # The object record's metasprite base is a shape family, while the
            # graphics DMA table selects one of six VRAM tile banks. Most families
            # happen to be base+sub*2; gfx #5 does not. Resolve the metasprite by
            # matching the actual DMA destination tile and the base shape dimensions.
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
        # Family 4 alternates between a 32x40 vertical pose and a 48x32
        # horizontal pose.  Its PC-FX cell canvas must cover their union;
        # using either frame's dimensions alone clips the other pose.
        if gfx==4: width,height=48,48
        elif gfx==1: width,height=32,48
        elif gfx==3: width,height=32,48
        variants.append(dict(key=(gfx,base,sub),words=frames,masks=masks,palette=enemy_palette(r,gfx),width=width,height=height,
                             has_hit_frame=(gfx not in no_hit_frame_gfx)))
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

def make_level8_snake(r):
    """Pack gfx family 5's complete 64-pixel strike, which exceeds 3x3 cells."""
    allm=json.loads((KIT/'graphics/metasprites/all_metasprites.json').read_text());byid={m['id']:m for m in allm}
    gfx,sub=5,2;tbl=b32(r,0x7246C+gfx*4);addr=ba.vdp_addr(b32(r,0xF454+sub*4));want_tile=addr//32
    frames=[];masks=[]
    for anim,shape_base in ((0,243),(1,243),(3,291)):
        ptr=b32(r,tbl+anim*4);next_ptr=b32(r,tbl+(anim+1)*4)
        bm=byid[shape_base];bw=bm['bbox'][2]+1;bh=bm['bbox'][3]+1
        candidates=[]
        for m in allm:
            if not m['parts'] or m['parts'][0]['tile_index']!=want_tile:continue
            mw=m['bbox'][2]+1;mh=m['bbox'][3]+1
            candidates.append(((0 if (mw,mh)==(bw,bh) else 100)+abs(m['id']-shape_base),m))
        m=min(candidates,key=lambda x:x[0])[1]
        needed=max((p['tile_index']+p['width_tiles']*p['height_tiles']-want_tile)*32 for p in m['parts'])
        raw_len=next_ptr-ptr
        if raw_len<=0 or raw_len>0x800 or raw_len%32:raw_len=needed
        raw_len=max(raw_len,needed);vv=bytearray(65536);vv[addr:addr+raw_len]=r[ptr:ptr+raw_len]
        canvas=[[0]*64 for _ in range(64)]
        for part in m['parts']:
            ba.blit_gen_tiles_to_canvas(vv,part['tile_index'],part['width_tiles'],part['height_tiles'],canvas,part['x_offset'],part['y_offset'])
        mask=0
        for cy in range(4):
            for cx in range(4):
                cell=[[canvas[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                if any(any(row) for row in cell):mask|=1<<(cy*4+cx)
                frames.extend(ba.encode_sprite_cell(cell))
        masks.append(mask)
    return frames,masks,enemy_palette(r,gfx)

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
    """Pack Bowser exclusively from the canonical images in ``bowser/``."""
    freq={}
    # The supplied Bowser bank is already aligned to the source object's
    # 112x96 canvas. Retaining
    # the canvas origin is important because the three teleport poses shrink
    # toward their upper-left anchor instead of being recentered.
    bowser_anim=PROJECT/'bowser'
    def bowser_frame(name):
        im=Image.open(bowser_anim/name).convert('RGB')
        if im.size!=(112,96): raise RuntimeError(f'Unexpected Bowser frame size: {name}')
        transparent=im.getpixel((0,0));pix={}
        for y in range(96):
            for x in range(112):
                c=im.getpixel((x,y))
                if c!=transparent: pix[(x,y)]=c;freq[c]=freq.get(c,0)+1
        return pix
    # Runtime order follows the state groups selected by the Mega Drive boss
    # script: normal patrol, fire, teleport, run, then the damage pose.
    special_src=tuple(bowser_frame(name) for name in (
        'bowser_normal1.png','bowser_normal2.png',
        'bowser_fire1.png','bowser_fire2.png',
        'bowser_teleport1.png','bowser_teleport2.png','bowser_teleport3.png',
        'bowser_run1.png','bowser_run2.png','bowser_hurt.png'))
    # Index 0 remains transparent. Deriving this palette only from these
    # seven files prevents old captures from silently recoloring Bowser.
    if len(freq)>15: raise RuntimeError(f'Bowser bank has {len(freq)} visible colors; expected at most 15')
    colors=[c for c,_ in sorted(freq.items(),key=lambda kv:-kv[1])]
    specialwords=[];specialmasks=[]
    for pix in special_src:
        idx=[[0]*112 for _ in range(96)];mask=0
        for (x,y),c in pix.items():
            ci=min(range(len(colors)),key=lambda j:sum((colors[j][k]-c[k])**2 for k in range(3)))
            idx[y][x]=ci+1
        for cy in range(6):
            for cx in range(7):
                cell=[[idx[cy*16+y][cx*16+x] for x in range(16)] for y in range(16)]
                if any(any(row) for row in cell):mask|=1<<(cy*7+cx)
                specialwords.extend(ba.encode_sprite_cell(cell))
        specialmasks.append(mask)
    # The detached 42x18 flame is the second large changed component in the
    # breath capture (the first is Bowser, the third is Mario). This is a
    # projectile, not a Bowser body frame; quantize it into the canonical
    # bowser/ palette rather than allowing it to influence that palette.
    ref=Image.open(BOSSREF/'boss1_arena.png').convert('RGB')
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
    return py[:16],flamewords,flamemask,specialwords,specialmasks

def make_later_boss_assets():
    """Pack the complete non-Bowser action banks from the Genesis sheet.

    These are deliberately separate banks: the original loader selects a
    different graphics, palette and room-map pointer for every boss slot.
    """
    out=[]
    # Boss 3 starts at the MD handler's (344,200) object coordinate, i.e.
    # (216,72) after the shared $80 object-space bias is removed.
    spawns={8:(216,72),10:(8,20),13:(125,57),16:(76,53),19:(88,49)}
    sheet=Image.open(PROJECT/'bosses_besides_bowser.png').convert('RGB')
    transparent=sheet.getpixel((0,0))
    # The contact sheet is not a sprite atlas: the 96-pixel windows overlap
    # neighboring rows for slot 8.  Use the cleaned seven-pose strip for the
    # pig (boss 3) so no wheel or other neighboring sprite is baked into it.
    pig_strip=Image.open(PROJECT/'pig_boss_boss3.png').convert('RGB')
    if pig_strip.size!=(455,71):
        raise RuntimeError('pig_boss_boss3.png must be exactly 455x71')
    pig_transparent=pig_strip.getpixel((0,0))
    pig_frames=[]
    for i in range(7):
        frame=Image.new('RGB',(80,80),pig_transparent)
        frame.paste(pig_strip.crop((i*65,0,(i+1)*65,71)),(0,0))
        pig_frames.append(frame)
    # The remaining boss sheets use their existing source regions. Slot 13
    # has ten poses; its own arena has no concurrent auxiliary bank, so all
    # ten fit in VRAM.
    frame_regions={10:((0,0,3),),13:((0,192,5),(0,288,5)),
                   16:((0,384,5),),19:((0,480,5),)}
    def elephant_frames():
        """Use the cleaned five-pose strip, preserving its shared baseline."""
        strip=Image.open(PROJECT/'elephant_strip.png').convert('RGB')
        strip_transparent=strip.getpixel((0,0))
        seen=set();components=[]
        for y in range(strip.height):
            for x in range(strip.width):
                if (x,y) in seen or strip.getpixel((x,y))==strip_transparent:continue
                todo=[(x,y)];seen.add((x,y));points=[]
                while todo:
                    px,py=todo.pop();points.append((px,py))
                    for nx,ny in ((px-1,py),(px+1,py),(px,py-1),(px,py+1)):
                        if 0<=nx<strip.width and 0<=ny<strip.height and (nx,ny) not in seen and strip.getpixel((nx,ny))!=strip_transparent:
                            seen.add((nx,ny));todo.append((nx,ny))
                if len(points)>7000:components.append(points)
        if len(components)!=5:raise RuntimeError(f'Expected five elephant components, got {len(components)}')
        frames=[]
        for i,points in enumerate(sorted(components,key=lambda p:min(x for x,y in p))):
            canvas=Image.new('RGB',(112,144),transparent);xbase=i*112
            for x,y in points:
                if xbase<=x<xbase+112:canvas.putpixel((x-xbase,y+8),strip.getpixel((x,y)))
            frames.append(canvas)
        return frames
    for slot in (8,10,13,16,19):
        root=BOSSREF/'later'
        bg=packed_with_transparent_zero(ba.pack_bg_rgb(Image.open(root/f'boss{slot}_arena.png').convert('RGB')))
        # Boss maps use a 32-row BAT so vertically scrolling rooms wrap on
        # complete source data.  Captured 240-line rooms have two blank rows;
        # slot 19 is replaced below by the exact two VDP planes.
        bg['bat'] += [0]*(32*32-len(bg['bat']))
        fg=dict(tile_words=[0]*16,bat=[0]*(32*32),tile_count=1)
        if slot in (16,19):
            dumped=make_dumped_boss_planes(slot);maps=dumped['maps']
            canonical=maps['background']+maps['foreground']
            for name in ('background','foreground'):
                maps[name]=override_words(f'boss_{slot:02d}_{name}.json',maps[name],32*32)
                maps[name]=canonicalize_packed_palettes(maps[name],canonical)
            bg=dict(palette=dumped['palette'],tile_words=dumped['tile_words'],bat=maps['background'],tile_count=dumped['tile_count'])
            bg=apply_packed_tile_overrides(bg,f'boss_{slot:02d}_tiles.json')
            # Both sprite-free VDP planes reference one exact shared bank.
            fg=dict(tile_words=bg['tile_words'],bat=maps['foreground'],tile_count=bg['tile_count'])
        else:
            source_bat=list(bg['bat'])
            bg['bat']=override_words(f'boss_{slot:02d}_background.json',bg['bat'],32*32)
            bg['bat']=canonicalize_packed_palettes(bg['bat'],source_bat)
            if slot==13: bg['bat']=boss13_sprite_free_bat(bg['bat'])
            bg=apply_packed_tile_overrides(bg,f'boss_{slot:02d}_tiles.json')
            fg['bat']=override_words(f'boss_{slot:02d}_foreground.json',fg['bat'],32*32)
            # Foreground and background share a bank so a copied tile has the
            # same meaning in both editor layers.
            fg['tile_words']=bg['tile_words'];fg['tile_count']=bg['tile_count']
        frames=[]
        if slot==8:
            frames=pig_frames
            frame_transparent=pig_transparent
        elif slot==10:
            ostrich_strip=Image.open(PROJECT/'ostrich_boss_boss4.png').convert('RGB')
            if ostrich_strip.size!=(272,108):
                raise RuntimeError('ostrich_boss_boss4.png must be exactly 272x108')
            ostrich_transparent=ostrich_strip.getpixel((0,0))
            # The supplied strip is three 90/91-pixel source frames. Keep
            # their native pixels and put each into the same 112x112 canvas
            # so the action origin does not move between poses.
            frames=[]
            for x0,x1 in ((0,90),(90,181),(181,272)):
                frame=Image.new('RGB',(112,112),ostrich_transparent)
                frame.paste(ostrich_strip.crop((x0,0,x1,108)),(0,0))
                frames.append(frame)
            frame_transparent=ostrich_transparent
        elif slot==19:
            frames=elephant_frames()
            frame_transparent=transparent
        else:
            for x,y,count in frame_regions[slot]:
                frames.extend(sheet.crop((x+i*112,y,x+(i+1)*112,y+96)) for i in range(count))
            frame_transparent=transparent
        cols=5 if slot==8 else 7
        rows=5 if slot==8 else (9 if slot==19 else (7 if slot==10 else 6))
        cell_colors=[];freq={}
        for im in frames:
            for cy in range(rows):
                for cx in range(cols):
                    colors={im.getpixel((cx*16+px,cy*16+py)) for py in range(16) for px in range(16)}-{frame_transparent}
                    cell_colors.append(colors)
                    for color in colors:freq[color]=freq.get(color,0)+1
        # A Genesis sprite cell selects one 16-colour line.  Keep source
        # colours exact by grouping cells into the minimum greedy set of
        # 15-visible-colour lines; this sheet needs at most two per boss.
        groups=[]
        for colors in sorted((c for c in cell_colors if c),key=len,reverse=True):
            fit=[(len(group|colors),i) for i,group in enumerate(groups) if len(group|colors)<=15]
            if fit:groups[min(fit)[1]]|=colors
            else:groups.append(set(colors))
        if not groups or len(groups)>2:raise RuntimeError(f'Boss {slot} needs {len(groups)} sprite palettes')
        palettes=[];lookups=[]
        for group in groups:
            colors=sorted(group,key=lambda color:-freq[color])
            lookups.append({color:i+1 for i,color in enumerate(colors)})
            pal=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*color) for color in colors]
            palettes.extend(pal+[ba.rgb_to_yuv(0,0,0)]*(16-len(pal)))
        words=[];masks=[];cell_palettes=[]
        for im in frames:
            mask=0
            for cy in range(rows):
                for cx in range(cols):
                    colors={im.getpixel((cx*16+px,cy*16+py)) for py in range(16) for px in range(16)}-{frame_transparent}
                    palette=next((i for i,group in enumerate(groups) if colors<=group),None)
                    if palette is None:raise RuntimeError(f'Boss {slot} cell cannot select a source palette')
                    cell_palettes.append(palette)
                    cell=[]
                    for py in range(16):
                        row=[]
                        for px in range(16):
                            color=im.getpixel((cx*16+px,cy*16+py))
                            row.append(0 if color==frame_transparent else lookups[palette][color])
                        cell.append(row)
                    if colors:mask|=1<<(cy*cols+cx)
                    words.extend(ba.encode_sprite_cell(cell))
            masks.append(mask)
        out.append(dict(slot=slot,bg=bg,fg=fg,words=words,palettes=palettes,masks=masks,
                        cell_palettes=cell_palettes,palette_count=len(groups),frame_count=len(frames),
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

def make_boss13_platform_words():
    """User-approved 64x23 gold carrier used only by the level-13 boss."""
    im=Image.open(BOSSREF/'boss13_platform.png').convert('RGBA')
    if im.size!=(64,23): raise RuntimeError('boss13_platform.png must be exactly 64x23')
    # Black in the reference is the transparent surround.  Keep every other
    # source colour exact and place the 23 scanlines at the top of a 64x32
    # two-sprite canvas.
    colors=[]
    for rgba in im.getdata():
        rgb=rgba[:3]
        if rgba[3] and rgb!=(0,0,0) and rgb not in colors: colors.append(rgb)
    if len(colors)>15: raise RuntimeError('boss13 platform exceeds one sprite palette')
    canvas=[[0]*64 for _ in range(32)]
    pix=im.load()
    for y in range(23):
        for x in range(64):
            rgb=pix[x,y][:3]
            if pix[x,y][3] and rgb!=(0,0,0): canvas[y][x]=colors.index(rgb)+1
    words=[]
    for half in range(2):
        for cy in range(2):
            for cx in range(2):
                words.extend(ba.encode_sprite_cell([[canvas[cy*16+y][half*32+cx*16+x] for x in range(16)] for y in range(16)]))
    palette=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
    palette += [ba.rgb_to_yuv(0,0,0)]*(16-len(palette))
    return words,palette

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
    """Pack the supplied boss-only throwable/crate bitmaps exactly."""
    def pack(path, crops):
        im=Image.open(PROJECT/path).convert('RGB')
        transparent=im.getpixel((0,0)); colors=[]
        for crop in crops:
            for y in range(crop[1],crop[3]):
                for x in range(crop[0],crop[2]):
                    c=im.getpixel((x,y))
                    if c!=transparent and c not in colors: colors.append(c)
        if len(colors)>15: raise RuntimeError(f'{path} exceeds one sprite palette')
        lookup={c:i+1 for i,c in enumerate(colors)}
        words=[]
        for x0,y0,x1,y1 in crops:
            cell=[[0]*16 for _ in range(16)]
            # Every supplied object fits in one 32x32 metasprite frame. A
            # two-frame strip simply contributes two consecutive 32x32 banks.
            canvas=[[0]*32 for _ in range(32)]
            for y in range(y0,y1):
                for x in range(x0,x1):
                    c=im.getpixel((x,y))
                    if c!=transparent: canvas[y-y0][x-x0]=lookup[c]
            for cy in range(2):
                for cx in range(2):
                    words.extend(ba.encode_sprite_cell([[canvas[cy*16+py][cx*16+px] for px in range(16)] for py in range(16)]))
        palette=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
        palette += [ba.rgb_to_yuv(0,0,0)]*(16-len(palette))
        return words,palette
    pot,potpal=pack('pot.png',[(0,0,23,24)])
    bombs,bombpal=pack('bombs_boss5.png',[(0,0,32,23),(40,0,71,23)])
    crate4,crate4pal=pack('crate_boss4.png',[(0,0,16,24)])
    bell,bellpal=pack('bell_boss6.png',[(0,0,24,27)])
    return pot,potpal,bombs,bombpal,crate4,crate4pal,bell,bellpal

def make_boss_water_words():
    """Pack the pig's 7x16 falling water drop as one transparent cell."""
    im=Image.open(PROJECT/'water.png').convert('RGB')
    if im.size!=(7,16): raise RuntimeError('water.png must be exactly 7x16')
    transparent=im.getpixel((0,0)); colors=[]
    for y in range(im.height):
        for x in range(im.width):
            c=im.getpixel((x,y))
            if c!=transparent and c not in colors: colors.append(c)
    lookup={c:i+1 for i,c in enumerate(colors)}
    cell=[[0]*16 for _ in range(16)]
    for y in range(im.height):
        for x in range(im.width):
            c=im.getpixel((x,y))
            if c!=transparent: cell[y][x]=lookup[c]
    words=ba.encode_sprite_cell(cell)
    palette=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
    palette += [ba.rgb_to_yuv(0,0,0)]*(16-len(palette))
    return words,palette

def main():
    r=ROMPATH.read_bytes(); sm=json.load(open(KIT/'graphics/stage_sets/stage_sets_manifest.json')); init=json.load(open(KIT/'graphics/metasprites/level_init_objects_and_spawns.json'))
    routes=parse_routes(r,sm); sets=build_sets(sm); levels=parse_level_objects(r,routes,init); variants=build_enemy_variants(r,levels)
    set_index={slot:s['set_id']-1 for s in sets for slot in s['slots']}
    palettes={slot:palette_for_level(slot) for slot in range(1,20)}
    bowserrooms=[make_boss_background(slot) for slot in (3,6)]; bosspal,bossflamewords,bossflamemask,bowserspecialwords,bowserspecialmasks=make_bowser_sprite();laterboss=make_later_boss_assets();level6platform=make_level6_platform_words();boss13platform,boss13platformpal=make_boss13_platform_words();finalring,finalhammer,finalauxpal=make_final_boss_aux_words();potwords,potpal,bombwords,bombpal,crate4words,crate4pal,bellwords,bellpal=make_boss_item_words();waterwords,waterpal=make_boss_water_words();bananawords=make_monkey_banana(r);monkeyspecial,monkeyspecialmasks=make_monkey_special_frames(r);cratewalk,cratewalkmasks=make_crate_walk_frames(r);snakewords,snakemasks,snakepal=make_level8_snake(r)
    bosshp1,bosshp2,bossparams,bossoffsets,bossaltspawn,bosshandlers,bosscollision=boss_tables(r)
    out=PROJECT/'src/game_levels_generated.c'; h=out.with_suffix('.h')
    h.write_text('''#ifndef SMB_PCFX_GAME_LEVELS_GENERATED_H\n#define SMB_PCFX_GAME_LEVELS_GENERATED_H\n\ntypedef struct { const unsigned short *tile_words; const unsigned short *segment_bat; const unsigned char *collision; unsigned short tile_count; unsigned char segment_count; } StageSetData;\ntypedef struct { const unsigned short *words; const unsigned short *palette; unsigned short mask0,mask1; unsigned char width,height; } EnemyVariantData;\ntypedef struct { unsigned char set_index, route_len; const unsigned char *route,*route_modes,*route_quadrants; const unsigned short *palette,*bg_bat; short spawn_x,spawn_y; unsigned short crate_count,gem_count,enemy_count; const short *crates,*gems,*enemies; unsigned char variant_count; const unsigned char *variant_ids; } GameLevelData;\nextern const StageSetData g_stage_sets[12];\nextern const GameLevelData g_game_levels[19];\nextern const EnemyVariantData g_enemy_variants[];\nextern const unsigned short g_enemy_variant_count;\nextern const unsigned char g_boss_level_flags[19];\nextern const unsigned short g_boss_bg_palette[256],g_boss_bg_tile_words[] ,g_boss_bg_bat[];\nextern const unsigned short g_boss_bg_tile_count;\nextern const unsigned short g_bowser_words[],g_bowser_palette[16];\nextern const unsigned long long g_bowser_masks[5];\nextern const unsigned short g_boss_hp_1p[19],g_boss_hp_2p[19];\nextern const unsigned short g_boss_params[57],g_boss_alt_spawn[38];\nextern const short g_boss_hit_offsets[38];\nextern const unsigned long g_boss_handler_address[19];\n#define ENEMY_FRAME_WORDS 576\n#define ENEMY_VARIANT_VRAM_WORDS 1152\n#define BOWSER_FRAME_WORDS 2304\n#define BOWSER_FRAME_COUNT 5\n#define BOWSER_SPECIAL_FRAME_COUNT 7\n#endif\n''')
    # Keep the generated declarations tied to the actual captured pose count.
    # Extend the legacy generated declaration block for the complete captured
    # boss-1 bank and its detached flame metasprite.
    hs=h.read_text().replace('g_bowser_masks[5]','g_bowser_masks[9]').replace('BOWSER_FRAME_COUNT 5','BOWSER_FRAME_COUNT 9')
    hs=hs.replace('unsigned short mask0,mask1;','unsigned short mask0,mask1,mask2;')
    hs=hs.replace('unsigned char width,height; } EnemyVariantData;',
                  'unsigned char width,height,has_hit_frame; } EnemyVariantData;')
    hs=hs.replace('typedef struct { unsigned char set_index, route_len;',
        'typedef struct { const unsigned short *palette,*tile_words,*bg_bat,*fg_bat; unsigned short tile_count; } BossRoomData;\ntypedef struct { const unsigned short *bg_palette,*bg_tile_words,*bg_bat,*fg_tile_words,*fg_bat,*sprite_words,*sprite_palettes; const unsigned long long *sprite_masks; const unsigned char *sprite_cell_palettes; unsigned short bg_tile_count,fg_tile_count; unsigned char palette_count,frame_count,cols,rows; short spawn_x,spawn_y; } BossVisualData;\ntypedef struct { unsigned char set_index, route_len;')
    hs=hs.replace('g_bowser_words[],g_bowser_palette[16];','g_bowser_words[],g_bowser_flame_words[],g_bowser_special_words[],g_bowser_palette[16];\nextern const unsigned short g_level6_platform_words[192];')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned char g_bowser_flame_mask;\nextern const unsigned long long g_bowser_special_masks[10];\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned short g_monkey_banana_words[64];\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned short g_monkey_special_words[1152],g_monkey_special_masks[2];\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned short g_crate_walk_words[1152],g_crate_walk_masks[2];\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned short g_boss_hp_1p','extern const unsigned short g_level8_snake_words[3072],g_level8_snake_palette[16],g_level8_snake_masks[3];\nextern const unsigned short g_boss_hp_1p')
    hs=hs.replace('extern const unsigned long g_boss_handler_address[19];',
        'extern const unsigned long g_boss_handler_address[19];\nextern const BossVisualData g_later_boss_visuals[5];\nextern const unsigned char g_boss_collision[6144];')
    hs=hs.replace('extern const unsigned char g_bowser_flame_mask;',
        'extern const BossRoomData g_bowser_rooms[2];\nextern const unsigned short g_final_ring_words[256],g_final_hammer_words[256],g_final_aux_palette[16],g_boss_pot_words[256],g_boss_pot_palette[16],g_boss_bomb_words[512],g_boss_bomb_palette[16],g_boss_crate4_words[256],g_boss_crate4_palette[16],g_boss_bell_words[256],g_boss_bell_palette[16],g_boss_water_words[64],g_boss_water_palette[16],g_boss13_platform_words[512],g_boss13_platform_palette[16];\nextern const unsigned char g_bowser_flame_mask;')
    # Legacy Bowser captures are intentionally absent: bowser/ is the sole
    # body-frame and palette source.
    hs=hs.replace('g_bowser_words[],','').replace('extern const unsigned long long g_bowser_masks[9];\n','')
    hs=hs.replace('#define BOWSER_FRAME_WORDS 2304\n#define BOWSER_FRAME_COUNT 9\n','')
    hs=hs.replace('#define BOWSER_SPECIAL_FRAME_COUNT 7','#define BOWSER_SPECIAL_FRAME_COUNT 10')
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
        emit_u16(f,'g_monkey_special_words',monkeyspecial);emit_u16(f,'g_monkey_special_masks',monkeyspecialmasks)
        emit_u16(f,'g_crate_walk_words',cratewalk);emit_u16(f,'g_crate_walk_masks',cratewalkmasks)
        emit_u16(f,'g_level8_snake_words',snakewords);emit_u16(f,'g_level8_snake_masks',snakemasks);emit_u16(f,'g_level8_snake_palette',snakepal)
        f.write(f'const unsigned short g_enemy_variant_count={len(variants)};\n')
        f.write('const EnemyVariantData g_enemy_variants[] = {\n')
        for i,v in enumerate(variants):
            f.write(f'  {{enemy_variant_{i:02d}_words,enemy_variant_{i:02d}_pal,0x{v["masks"][0]:04X},0x{v["masks"][1]:04X},0x{v["masks"][2]:04X},{v["width"]},{v["height"]},{int(v["has_hit_frame"])}}},\n')
        f.write('};\n\n');emit_u16(f,'g_monkey_banana_words',bananawords)
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
        emit_u16(f,'g_bowser_flame_words',bossflamewords);emit_u16(f,'g_bowser_special_words',bowserspecialwords);emit_u16(f,'g_bowser_palette',bosspal);f.write('const unsigned long long g_bowser_special_masks[10]={'+','.join(f'0x{x:09X}ULL' for x in bowserspecialmasks)+'};\n');f.write(f'const unsigned char g_bowser_flame_mask=0x{bossflamemask:02X};\n');emit_u16(f,'g_level6_platform_words',level6platform);emit_u16(f,'g_boss13_platform_words',boss13platform);emit_u16(f,'g_boss13_platform_palette',boss13platformpal);emit_u16(f,'g_final_ring_words',finalring);emit_u16(f,'g_final_hammer_words',finalhammer);emit_u16(f,'g_final_aux_palette',finalauxpal);emit_u16(f,'g_boss_pot_words',potwords);emit_u16(f,'g_boss_pot_palette',potpal);emit_u16(f,'g_boss_bomb_words',bombwords);emit_u16(f,'g_boss_bomb_palette',bombpal);emit_u16(f,'g_boss_crate4_words',crate4words);emit_u16(f,'g_boss_crate4_palette',crate4pal);emit_u16(f,'g_boss_bell_words',bellwords);emit_u16(f,'g_boss_bell_palette',bellpal);emit_u16(f,'g_boss_water_words',waterwords);emit_u16(f,'g_boss_water_palette',waterpal)
        for b in laterboss:
            q=f'boss{b["slot"]}'
            emit_u16(f,q+'_bg_palette',b['bg']['palette'],static=True);emit_u16(f,q+'_bg_tiles',b['bg']['tile_words'],static=True);emit_u16(f,q+'_bg_bat',b['bg']['bat'],static=True)
            emit_u16(f,q+'_fg_tiles',b['fg']['tile_words'],static=True);emit_u16(f,q+'_fg_bat',b['fg']['bat'],static=True)
            emit_u16(f,q+'_sprite_words',b['words'],static=True);emit_u16(f,q+'_sprite_palettes',b['palettes'],static=True);emit_u64(f,q+'_sprite_masks',b['masks'],static=True);emit_u8(f,q+'_sprite_cell_palettes',b['cell_palettes'],static=True)
        f.write('const BossVisualData g_later_boss_visuals[5]={\n')
        for b in laterboss:
            q=f'boss{b["slot"]}'; f.write(f'  {{{q}_bg_palette,{q}_bg_tiles,{q}_bg_bat,{q}_fg_tiles,{q}_fg_bat,{q}_sprite_words,{q}_sprite_palettes,{q}_sprite_masks,{q}_sprite_cell_palettes,{b["bg"]["tile_count"]},{b["fg"]["tile_count"]},{b["palette_count"]},{b["frame_count"]},{b["cols"]},{b["rows"]},{b["spawn"][0]},{b["spawn"][1]}}},\n')
        f.write('};\n\n');emit_u8(f,'g_boss_collision',bosscollision)
        emit_u16(f,'g_boss_hp_1p',bosshp1); emit_u16(f,'g_boss_hp_2p',bosshp2)
        emit_u16(f,'g_boss_params',bossparams); emit_s16(f,'g_boss_hit_offsets',[bossoffsets])
        emit_u16(f,'g_boss_alt_spawn',bossaltspawn)
        f.write('const unsigned long g_boss_handler_address[19] = {'+', '.join(f'0x{x:08X}UL' for x in bosshandlers)+'};\n')
    print('wrote',out,out.stat().st_size)

if __name__=='__main__': main()
