#!/usr/bin/env python3
from pathlib import Path
import json,csv,sys
from PIL import Image
sys.path.insert(0,str(Path(__file__).parent))
import build_assets as ba

import os
PROJECT=Path(__file__).resolve().parent.parent
KIT=Path(os.environ.get('KIT','/mnt/data/mdkit_full/Super_Mario_Bros_MD_reconstruction_kit'))
ROMPATH=Path(os.environ.get('ROM','/mnt/data/r9mdrom/smb_patched.md'))
BOSSREF=Path(os.environ.get('BOSSREF',str(PROJECT/'assets/reference/boss')))

# These slots share the early cloud-sky background family.  The Mega Drive
# source is a 224-line composition; on PC-FX it is placed at Y+16 rather than
# fabricating extra bottom rows to reach 240 lines.
CLOUD_BG_Y16_SLOTS={1,2,4}

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
        vram=(KIT/f'graphics/levels/level_{first:02d}/assembled_vram.bin').read_bytes()
        tiles=[tuple([0]*64)]; lookup={(0,tiles[0]):0}; bat=[]; collisions=[]
        for seg in range(sx['segment_count']):
            sd=root/f'segments/segment_{seg:02d}'
            words=ba.read_words_be(sd/'map_decoded_words_be.bin')
            segbat=[]
            src_h=max(1,len(words)//32)
            for y in range(30):
                # Mode-2 maps can contain the full 32 source rows. Ordinary
                # V28 maps are 28 rows and extend their last row through 240.
                sy=y if src_h>=30 else min(y,src_h-1)
                for x in range(32): segbat.append(ba.append_genesis_word(words[sy*32+x],vram,tiles,lookup))
            bat.extend(segbat)
            cf=next(sd.glob('collision_ROM_*.bin')).read_bytes(); collisions.extend(cf[:1024])
        # Level-specific background layers are converted into this set's shared tile dictionary.
        bgs={}
        for slot in sx['level_slots']:
            ld=KIT/f'graphics/levels/level_{slot:02d}'; vv=(ld/'assembled_vram.bin').read_bytes()
            fs=sorted(ld.glob('extra_layer_00_ROM_*_words_be.bin'))
            if fs:
                ww=ba.read_words_be(fs[0]); bb=[]
                src_h=max(1,len(ww)//32)
                for y in range(30):
                    if slot in CLOUD_BG_Y16_SLOTS and src_h>=28:
                        # Preserve the actual 224-line source (rows 0..27) and
                        # place it 16 pixels lower.  Fill the new top margin
                        # with the source sky row; do not repeat bottom art.
                        sy=0 if y<2 else min(y-2,27)
                    else:
                        sy=y if src_h>=30 else min(y,src_h-1)
                    for x in range(32): bb.append(ba.append_genesis_word(ww[sy*32+x],vv,tiles,lookup))
            else: bb=[0]*(32*30)
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

def make_boss_background():
    # Real forced boss-mode capture, converted to native VDC tiles. Bowser is
    # absent at f2300. Mario/HUD are removed; the arena's brown floor row is
    # extended through 240 just as normal levels extend their final visual row.
    src=Image.open(BOSSREF/'b3_2300.png').convert('RGB')
    # The Mega Drive boss plane begins below a two-tile black status margin.
    # The old screenshot conversion discarded that scroll origin, lifting the
    # entire room by 16 pixels and making Bowser appear to float.
    im=Image.new('RGB',(256,240),(0,0,0))
    im.paste(src.crop((0,0,256,224)),(0,16))
    for y in range(152,208):
        for x in range(0,48): im.putpixel((x,y),im.getpixel((x+64,y)))
    floor=im.crop((0,216,256,224))
    for y in range(208,240,8): im.paste(floor,(0,y))
    return ba.pack_bg_rgb(im)

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

def make_bowser_sprite():
    """Recover two Bowser animation poses from forced boss-mode captures.

    b3_2300 is a clean arena. 2600/2620 contain Bowser at two animation
    phases.  The alpha mask comes from the largest connected difference
    component, while RGB is read from the live frame.  Importantly, black
    outline pixels remain non-zero sprite indices rather than being confused
    with PC-FX transparent colour 0.
    """
    ref=Image.open(BOSSREF/'b3_2300.png').convert('RGB')
    src=[]; freq={}
    for fn in ('b3_2600.png','b3_2620.png'):
        im=Image.open(BOSSREF/fn).convert('RGB')
        pts=_largest_diff_component(ref,im)
        if not pts: raise RuntimeError(f'Could not isolate Bowser in {fn}')
        xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
        x0,y0=min(xs),min(ys)
        pix={}
        for x,y in pts:
            xx,yy=x-x0,y-y0
            if 0<=xx<96 and 0<=yy<96:
                c=im.getpixel((x,y)); pix[(xx,yy)]=c; freq[c]=freq.get(c,0)+1
        src.append(pix)
    # Index 0 must remain transparent. The fifteen visible entries include
    # true black, preserving Bowser's outline from the MD sprite.
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
    py=[ba.rgb_to_yuv(0,0,0)]+[ba.rgb_to_yuv(*c) for c in colors]
    py += [ba.rgb_to_yuv(0,0,0)]*(16-len(py))
    return words,masks,py[:16]

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
    return hp1,hp2,params,offsets,altspawn,handlers

def main():
    r=ROMPATH.read_bytes(); sm=json.load(open(KIT/'graphics/stage_sets/stage_sets_manifest.json')); init=json.load(open(KIT/'graphics/metasprites/level_init_objects_and_spawns.json'))
    routes=parse_routes(r,sm); sets=build_sets(sm); levels=parse_level_objects(r,routes,init); variants=build_enemy_variants(r,levels)
    set_index={slot:s['set_id']-1 for s in sets for slot in s['slots']}
    palettes={slot:palette_for_level(slot) for slot in range(1,20)}
    bossbg=make_boss_background(); bosswords,bossmasks,bosspal=make_bowser_sprite()
    bosshp1,bosshp2,bossparams,bossoffsets,bossaltspawn,bosshandlers=boss_tables(r)
    out=PROJECT/'src/game_levels_generated.c'; h=out.with_suffix('.h')
    h.write_text('''#ifndef SMB_PCFX_GAME_LEVELS_GENERATED_H\n#define SMB_PCFX_GAME_LEVELS_GENERATED_H\n\ntypedef struct { const unsigned short *tile_words; const unsigned short *segment_bat; const unsigned char *collision; unsigned short tile_count; unsigned char segment_count; } StageSetData;\ntypedef struct { const unsigned short *words; const unsigned short *palette; unsigned short mask0,mask1; unsigned char width,height; } EnemyVariantData;\ntypedef struct { unsigned char set_index, route_len; const unsigned char *route,*route_modes,*route_quadrants; const unsigned short *palette,*bg_bat; short spawn_x,spawn_y; unsigned short crate_count,gem_count,enemy_count; const short *crates,*gems,*enemies; unsigned char variant_count; const unsigned char *variant_ids; } GameLevelData;\nextern const StageSetData g_stage_sets[12];\nextern const GameLevelData g_game_levels[19];\nextern const EnemyVariantData g_enemy_variants[];\nextern const unsigned short g_enemy_variant_count;\nextern const unsigned char g_boss_level_flags[19];\nextern const unsigned short g_boss_bg_palette[256],g_boss_bg_tile_words[] ,g_boss_bg_bat[];\nextern const unsigned short g_boss_bg_tile_count;\nextern const unsigned short g_bowser_words[],g_bowser_palette[16];\nextern const unsigned long long g_bowser_masks[2];\nextern const unsigned short g_boss_hp_1p[19],g_boss_hp_2p[19];\nextern const unsigned short g_boss_params[57],g_boss_alt_spawn[38];\nextern const short g_boss_hit_offsets[38];\nextern const unsigned long g_boss_handler_address[19];\n#define ENEMY_FRAME_WORDS 576\n#define ENEMY_VARIANT_VRAM_WORDS 1152\n#define BOWSER_FRAME_WORDS 2304\n#endif\n''')
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
        emit_u16(f,'g_boss_bg_palette',bossbg['palette']);emit_u16(f,'g_boss_bg_tile_words',bossbg['tile_words']);emit_u16(f,'g_boss_bg_bat',bossbg['bat']);f.write(f'const unsigned short g_boss_bg_tile_count={bossbg["tile_count"]};\n\n')
        emit_u16(f,'g_bowser_words',bosswords);emit_u16(f,'g_bowser_palette',bosspal);f.write(f'const unsigned long long g_bowser_masks[2]={{0x{bossmasks[0]:09X}ULL,0x{bossmasks[1]:09X}ULL}};\n')
        emit_u16(f,'g_boss_hp_1p',bosshp1); emit_u16(f,'g_boss_hp_2p',bosshp2)
        emit_u16(f,'g_boss_params',bossparams); emit_s16(f,'g_boss_hit_offsets',[bossoffsets])
        emit_u16(f,'g_boss_alt_spawn',bossaltspawn)
        f.write('const unsigned long g_boss_handler_address[19] = {'+', '.join(f'0x{x:08X}UL' for x in bosshandlers)+'};\n')
    print('wrote',out,out.stat().st_size)

if __name__=='__main__': main()
