#!/usr/bin/env python3
"""Local HTTP backend for the PC-FX tilemap editor."""
from argparse import ArgumentParser
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse, parse_qs
import csv, json, os, tempfile
from level_extension import foreground_words_240
import build_assets as ba
from PIL import Image

ROOT=Path(__file__).resolve().parent.parent
WEB=ROOT/'tools/level_editor'
VISIBLE_ROWS=28
BOSS_LEVELS=(3,6,8,10,13,16,19)
BOSSREF=ROOT/'assets/reference/boss'

# Slots 9 and 10 reuse the same 22 decoded layouts with different graphics
# banks. The outdoor slot routes through 0..7 and the cave slot through 8..21.
# Showing the other half with the wrong bank produces apparently corrupt art.
LEVEL_SEGMENT_RANGES={9:range(0,8),10:range(8,22)}

def read_words(path):
    data=path.read_bytes()
    return [int.from_bytes(data[i:i+2],'big') for i in range(0,len(data),2)]

def atomic_json(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix=path.name+'.',dir=path.parent)
    try:
        with os.fdopen(fd,'w') as f:json.dump(data,f,separators=(',',':'))
        os.replace(tmp,path)
    finally:
        if os.path.exists(tmp):os.unlink(tmp)

class Model:
    def __init__(self,kit,overrides):
        self.kit=kit;self.overrides=overrides
        self.manifest=json.loads((kit/'graphics/stage_sets/stage_sets_manifest.json').read_text())
        self.by_level={slot:s for s in self.manifest for slot in s['level_slots']}

    def set_root(self,set_id):
        return next((self.kit/'graphics/stage_sets').glob(f'set_{set_id:02d}_slots_*'))

    def catalog(self):
        return {'levels':[{'level':n,'set':self.by_level[n]['set_id'],
            'segments':list(LEVEL_SEGMENT_RANGES.get(n,range(self.by_level[n]['segment_count']))),
            'shared_with':self.by_level[n]['level_slots']}
            for n in range(1,20)],'boss_levels':list(BOSS_LEVELS)}

    def palette(self,level):
        colors=[[0,0,0] for _ in range(64)]
        with (self.kit/f'graphics/levels/level_{level:02d}/palette.csv').open() as f:
            for row in csv.DictReader(f):
                i=int(row['index'])
                if i<64:colors[i]=[int(row['r']),int(row['g']),int(row['b'])]
        return colors

    def names(self,level,layer,segment):
        if layer.startswith('boss-'):
            return (f'boss_{level:02d}_{layer[5:]}.json',f'boss_{level:02d}_tiles.json')
        stage=self.by_level[level];sid=stage['set_id']
        if layer=='foreground':
            return (f'set_{sid:02d}_segment_{segment:02d}.json',f'set_{sid:02d}_tiles.json')
        return (f'level_{level:02d}_background.json',f'level_{level:02d}_background_tiles.json')

    def boss_default(self,level,layer):
        if level not in BOSS_LEVELS:raise ValueError('this level is not a boss room')
        which=layer[5:]
        if level==19:
            root=BOSSREF/'layers/boss19'
            raw=read_words(root/f'md19_{which}_words_be.bin')
            if which=='foreground':words=[raw[y*64+x] if y<26 else 0 for y in range(32) for x in range(32)]
            else:words=[raw[min(y,27)*64+x] for y in range(32) for x in range(32)]
            vram=(root/'md19_vram.bin').read_bytes()
            cram=json.loads((root/'md19_dump.json').read_text())['cram']
            palette=[[0,0,0] for _ in range(64)]
            for i,w in enumerate(cram):palette[i]=list(ba.genesis_cram_rgb(w))
            return words,vram,palette
        # The earlier captured rooms are already exact-size PC-FX tile banks.
        # Their foreground starts empty and can be authored independently.
        if level in (3,6):
            src=Image.open(BOSSREF/'boss1_arena.png').convert('RGB')
            for y in range(208,240):
                for x in range(128):src.putpixel((x,y),src.getpixel((x+128,y)))
            im=Image.new('RGB',(256,240),(0,0,0));im.paste(src.crop((0,16,256,240)),(0,0));last=im.crop((0,216,256,224));im.paste(last,(0,224));im.paste(last,(0,232))
        else:im=Image.open(BOSSREF/'later'/f'boss{level}_arena.png').convert('RGB')
        packed=ba.pack_bg_rgb(im)
        packed['tile_words']=[0]*16+packed['tile_words']
        packed['bat']=[(w&0xF000)|((w&0x0FFF)+1) for w in packed['bat']]
        packed['tile_count']+=1
        words=(packed['bat']+[0]*(32*32-len(packed['bat']))) if which=='background' else [0]*(32*32)
        vram=bytearray(2048*32)
        for tile in range(packed['tile_count']):
            pix=[]
            # Decode the packed planar words back to editor pixels.
            tw=packed['tile_words'][tile*16:(tile+1)*16]
            for y in range(8):
                for x in range(8):
                    bit=7-x;pix.append(((tw[y]>>bit)&1)|(((tw[y]>>(8+bit))&1)<<1)|(((tw[8+y]>>bit)&1)<<2)|(((tw[8+y]>>(8+bit))&1)<<3))
            off=tile*32
            for i in range(32):vram[off+i]=(pix[i*2]<<4)|pix[i*2+1]
        palette=[list(ba.pcfxemu_decode_yuv444(w)) for w in packed['palette'][:64]]
        return words,bytes(vram),palette

    def read_override(self,name):
        p=self.overrides/name
        return json.loads(p.read_text()) if p.exists() else None

    def apply_tiles(self,vram,name):
        data=self.read_override(name);out=bytearray(vram)
        if data:
            for key,pix in data.get('tiles',{}).items():
                off=int(key)*32
                for i in range(32):out[off+i]=(pix[i*2]<<4)|pix[i*2+1]
        return bytes(out)

    def foreground_default(self,level,segment):
        stage=self.by_level[level]
        if not 0<=segment<stage['segment_count']:raise ValueError('segment out of range')
        words=read_words(self.set_root(stage['set_id'])/f'segments/segment_{segment:02d}/map_decoded_words_be.bin')
        return foreground_words_240(stage['set_id'],words)

    def background_halves(self,level):
        ld=self.kit/f'graphics/levels/level_{level:02d}'
        desc=json.loads((ld/'manifest.json').read_text()).get('extra_layers',[])
        def half(suffix):
            for i,item in enumerate(desc):
                if item['vram_destination'].endswith(suffix):
                    return read_words(next(ld.glob(f'extra_layer_{i:02d}_ROM_*_words_be.bin')))
        left=half('000');right=half('040')
        return None if left is None else (left,right or left)

    def background_default(self,level):
        halves=self.background_halves(level)
        if not halves:return [0]*(64*30)
        left,right=halves;rows=max(1,len(left)//32);out=[]
        for y in range(30):
            if level in (1,2,4) and rows>=28:sy=0 if y<2 else min(y-2,27)
            else:
                crop=1 if level==3 else 0
                sy=min((VISIBLE_ROWS-1+crop) if y>=VISIBLE_ROWS else y+crop,rows-1)
            out.extend(left[sy*32:sy*32+32]);out.extend(right[sy*32:sy*32+32])
        return out

    def map_data(self,level,layer,segment):
        if level not in self.by_level or layer not in ('foreground','background','boss-foreground','boss-background'):raise ValueError('bad map request')
        map_name,tile_name=self.names(level,layer,segment)
        isboss=layer.startswith('boss-')
        if isboss:default,vram,palette=self.boss_default(level,layer)
        else:default=self.foreground_default(level,segment) if layer=='foreground' else self.background_default(level)
        saved=self.read_override(map_name);words=saved.get('words',default) if saved else default
        stage=self.by_level[level]
        if not isboss:
            vram_level=stage['level_slots'][0] if layer=='foreground' else level
            vram=(self.kit/f'graphics/levels/level_{vram_level:02d}/assembled_vram.bin').read_bytes();palette=self.palette(level)
        vram=self.apply_tiles(vram,tile_name)
        pixels=[]
        for tile in range(2048):
            b=vram[tile*32:tile*32+32]
            for q in b:pixels.extend((q>>4,q&15))
        return {'version':1,'level':level,'layer':layer,'segment':segment,
            'set':stage['set_id'],'shared_with':stage['level_slots'],'width':32 if layer in ('foreground','boss-foreground','boss-background') else 64,
            'height':32 if isboss else 30,'extension_start_row':VISIBLE_ROWS,'words':words,'default_words':default,'tile_pixels':pixels,
            'palette':palette,'saved':saved is not None,'tile_saved':self.read_override(tile_name) is not None}

    def save(self,data):
        level=int(data['level']);layer=data['layer'];segment=int(data.get('segment',0))
        isboss=layer.startswith('boss-');width=32 if layer in ('foreground','boss-foreground','boss-background') else 64;height=32 if isboss else 30;words=data.get('words')
        if not isinstance(words,list) or len(words)!=width*height or any(type(x) is not int or not 0<=x<=65535 for x in words):raise ValueError('invalid tilemap')
        map_name,tile_name=self.names(level,layer,segment)
        atomic_json(self.overrides/map_name,{'version':1,'kind':layer,'level':level,'segment':segment,'words':words})
        changed=data.get('tile_overrides',{})
        if changed:
            old=self.read_override(tile_name) or {'version':1,'kind':'tiles','tiles':{}}
            for key,pix in changed.items():
                tile=int(key)
                if not 0<=tile<2048 or not isinstance(pix,list) or len(pix)!=64 or any(type(x) is not int or not 0<=x<16 for x in pix):raise ValueError('invalid tile pixels')
                old['tiles'][str(tile)]=pix
            atomic_json(self.overrides/tile_name,old)
        return {'ok':True,'map_file':map_name,'tile_file':tile_name if changed else None}

    def restore(self,data):
        level=int(data['level']);layer=data['layer'];segment=int(data.get('segment',0))
        names=self.names(level,layer,segment);removed=[]
        for name in names:
            p=self.overrides/name
            if p.exists():p.unlink();removed.append(name)
        return {'ok':True,'removed':removed}

class Handler(SimpleHTTPRequestHandler):
    model=None
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(WEB),**kwargs)
    def json_out(self,data,status=200):
        raw=json.dumps(data,separators=(',',':')).encode();self.send_response(status)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
    def do_GET(self):
        u=urlparse(self.path)
        try:
            if u.path=='/api/catalog':return self.json_out(self.model.catalog())
            if u.path=='/api/map':
                q=parse_qs(u.query);return self.json_out(self.model.map_data(int(q['level'][0]),q['layer'][0],int(q.get('segment',['0'])[0])))
            return super().do_GET()
        except Exception as e:self.json_out({'error':str(e)},400)
    def do_POST(self):
        try:
            length=int(self.headers.get('Content-Length','0'));data=json.loads(self.rfile.read(length))
            if self.path=='/api/save':return self.json_out(self.model.save(data))
            if self.path=='/api/restore':return self.json_out(self.model.restore(data))
            self.json_out({'error':'not found'},404)
        except Exception as e:self.json_out({'error':str(e)},400)

def main():
    ap=ArgumentParser();ap.add_argument('--kit',type=Path,default=ROOT/'Super_Mario_Bros_MD_reconstruction_kit')
    ap.add_argument('--overrides',type=Path,default=ROOT/'level_overrides');ap.add_argument('--host',default='127.0.0.1');ap.add_argument('--port',type=int,default=8765)
    a=ap.parse_args();Handler.model=Model(a.kit.resolve(),a.overrides.resolve())
    server=ThreadingHTTPServer((a.host,a.port),Handler)
    print(f'PC-FX level editor: http://{a.host}:{a.port}/');print(f'Overrides: {a.overrides.resolve()}')
    try:server.serve_forever()
    except KeyboardInterrupt:pass

if __name__=='__main__':main()
