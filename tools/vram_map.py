#!/usr/bin/env python3
"""Single source of truth for both VDC VRAM layouts.

Every sprite/tile bank the game uploads is declared once in BANKS below.  The
tool checks that no two banks that can be resident at the same time overlap,
that each bank fits its chip, and that 32-pixel-wide sprite banks keep the
128-word alignment the VDC requires, then writes src/vram_map.h with the
addresses.  main.c includes that header instead of defining addresses itself,
so a layout mistake fails the build rather than the screen.

Banks whose size depends on the level (the stage tile set, the per-level enemy
variant banks) are measured from the generated data, and the play scene is
validated once per level with that level's real sizes.

  python3 tools/vram_map.py            regenerate src/vram_map.h, check, report
  python3 tools/vram_map.py --check    check only, write nothing
"""
from pathlib import Path
import re, sys

PROJECT = Path(__file__).resolve().parent.parent
HEADER = PROJECT / 'src/vram_map.h'

# The VDC address space is 16-bit word-addressed.
VRAM_WORDS = 0x10000
TILE_WORDS = 16
ENEMY_VARIANT_WORDS = 1152
ENEMY_FRAME_WORDS = 576
PLAYER_FRAME_WORDS = 384
PLAYER_FRAME_COUNT = 20

# Scenes are the states in which a bank must hold its contents.  Two banks may
# share addresses only when no scene lists both.
BOOT, TITLE, MENU, PLAY, BOSS, CONT = 'boot', 'title', 'menu', 'play', 'boss', 'continue'
SCENES = (BOOT, TITLE, MENU, PLAY, BOSS, CONT)

FRONT, BACK = 'front', 'back'

# name, chip, base, words, scenes, note
# words may be an int, or one of the 'sized:' markers resolved below.
BANKS = [
    # ---- VDC1 (FRONT): sprites, and the foreground plane during gameplay ----
    ('FG_BAT_BASE_WORD',            FRONT, 0x0000, 64*32,  (PLAY, BOSS, MENU, CONT, TITLE, BOOT), 'foreground BAT, 64x32 cells'),
    ('FG_TILE_BASE_WORD',           FRONT, 0x0800, 'sized:stage_tiles', (PLAY, BOSS), 'stage/boss foreground tile set'),
    ('BOOT_MARIO_BASE_WORD',        FRONT, 0x1000, PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS, (BOOT, TITLE), 'opening animation Mario'),
    ('BOOT_LUIGI_BASE_WORD',        FRONT, 0x3000, PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS, (BOOT, TITLE), 'opening animation Luigi'),
    ('HUD_BAND_BLACK_TILE_WORD',    FRONT, 0x4640, 16,     (PLAY,), 'opaque black cell for the 224..239 HUD band'),
    ('PLAYER_BASE_WORD',            FRONT, 0x4800, PLAYER_FRAME_COUNT*PLAYER_FRAME_WORDS, (PLAY, BOSS), 'gameplay player poses'),
    ('PUSH_BASE_WORD',              FRONT, 0x5E00, 384,    (TITLE, MENU), 'PUSH START sprite'),
    ('STAR_BASE_WORD',              FRONT, 0x5F80, 128,    (TITLE, MENU), 'menu star'),
    ('COIN_BASE_WORD',              FRONT, 0x6000, 4096,   (TITLE, MENU), 'title coin animation'),
    ('THROW_BASE_WORD',             FRONT, 0x6600, 448,    (PLAY, BOSS), 'death/throw 48x32 pose'),
    ('CRATE_BASE_WORD',             FRONT, 0x6800, 256,    (PLAY, BOSS), 'crate object'),
    ('GEM_BASE_WORD',               FRONT, 0x6900, 192,    (PLAY, BOSS), 'gem, three frames'),
    ('HUD_BASE_WORD',               FRONT, 0x6A00, 192,    (PLAY, BOSS), 'life icon (128) + fire icon (64)'),
    ('ITEM_BASE_WORD',              FRONT, 0x6B00, 448,    (PLAY,), 'seven hidden crate items, 16x16 each'),
    ('FIREBALL_BASE_WORD',          FRONT, 0x6E00, 256,    (PLAY, BOSS), 'fireball'),
    ('HUD_HEART_BASE_WORD',         FRONT, 0x7000, 64,     (PLAY, BOSS), 'heart'),
    ('HUD_DIGIT_BASE_WORD',         FRONT, 0x7040, 640,    (PLAY, BOSS), 'HUD digits'),
    ('PLATFORM_BASE_WORD',          FRONT, 0x7300, 192,    (PLAY,), 'level 2/6/8 carrier, 48x16'),
    ('ENEMY_BASE_WORD',             FRONT, 0x7400, 'sized:enemy_variants', (PLAY,), 'per-level enemy variant banks'),
    ('BOSS_BASE_WORD',              FRONT, 0x7400, 'sized:boss_body', (BOSS,), 'boss body poses (Bowser uses the ten-pose bank)'),
    ('MENU_MARIO_BASE_WORD',        FRONT, 0x8400, 1536,   (MENU,), 'menu walk Mario'),
    ('MENU_LUIGI_BASE_WORD',        FRONT, 0x8A00, 384,    (MENU,), 'menu walk Luigi'),
    ('MONKEY_SPECIAL_BASE_WORD',    FRONT, 0x9800, 1152,   (PLAY,), 'monkey throw poses (levels with monkeys)'),
    # Level 11's walk banks reach 0x9E00 now that its duck carries a 3x3
    # throwing pose, so the mimic's own bank moved out of their way.
    ('CRATE_MIMIC_WALK_BASE_WORD',  FRONT, 0xE500, 1152,   (PLAY,), 'walking crate poses (levels with mimics)'),
    ('CONTINUE_DIGIT_BASE_WORD',    FRONT, 0xA000, 2560,   (CONT,), 'continue countdown digits'),
    ('STRIKE_SNAKE_BASE_WORD',      FRONT, 0xA100, 3072,   (PLAY,), 'snake strike, three 64x40 poses'),
    ('CONTINUE_PLAYER_BASE_WORD',   FRONT, 0xB300, 1536,   (PLAY, BOSS, CONT), 'continue/crouch Mario (768) + Luigi (768)'),
    ('LEVEL8_WEDGE_R_BASE_WORD',    FRONT, 0xB900, 768,    (PLAY,), 'level 8 down-right wedge, 48x64', 128),
    ('LEVEL8_WEDGE_L_BASE_WORD',    FRONT, 0xBC00, 384,    (PLAY,), 'level 8 down-left wedge, 32x48', 128),
    # 2880 = the five distinct extra cels levels 17..19 need at once: the
    # spearman's two, the duck's two and the hopper's one.  The hit bank moved
    # up by one cell's worth to make room for the fifth.
    ('ENEMY_ALT_BASE_WORD',         FRONT, 0xBD80, 2880,   (PLAY,), 'extra live cels: pterodactyl/floor-attacker third pose plus every variant attack pose'),
    ('ENEMY_HIT_BASE_WORD',         FRONT, 0xC900, 'sized:enemy_hit', (PLAY,), 'per-variant hit frame'),
    ('FRONT_TILE_BASE_WORD',        BACK,  0xD000, 'sized:menu_combo', (TITLE, MENU), 'menu/transition tile set on VDC0'),
    # These two sat at 0xDA00/0xDA40, inside the run the hit banks of the
    # bigger casts (levels 11 and 15..17) grow into, so the level that owned a
    # projectile had its art overwritten by a later variant's death pose.  The
    # gap above the pot is clear of every play-scene bank.
    ('ENEMY_SHOT_BASE_WORD',        FRONT, 0xE300, 256,    (PLAY,), 'thrown rock/spear/spark for the $C0C6, $BAD6 and $BCC8 throwers'),
    ('ENEMY_BANANA_BASE_WORD',      FRONT, 0xE400, 64,     (PLAY,), 'thrown banana (levels with monkeys)'),
    # The flier's bombing pose and its bomb are shared by all three of the
    # family's DMA-buffer variants, so they ride their own pair of banks in the
    # clear run above the crouch poses rather than the per-variant attack and
    # projectile arenas, which levels 16..18 already fill to the word.
    ('CHIP_BOMB_BASE_WORD',         FRONT, 0xEC80, 64,     (PLAY,), 'bomb dropped by the $C748 flier (levels with CHIP)'),
    ('CHIP_ATTACK_BASE_WORD',       FRONT, 0xECC0, 256,    (PLAY,), 'flier bombing pose, 24x24 (levels with CHIP)'),
    ('BOWSER_FLAME_BASE_WORD',      FRONT, 0xDD00, 384,    (BOSS,), 'Bowser flame'),
    ('BOSS_AUX_BASE_WORD',          FRONT, 0xE000, 2304,   (BOSS,), 'ring/hammer/pot/bell/bomb/crate4/platform'),
    ('BOSS_POT_BASE_WORD',          FRONT, 0xE200, 256,    (PLAY,), 'pot object, also uploaded by every normal stage',
     None, ['BOSS_AUX_BASE_WORD:the pot lives inside the boss aux bank at the same address']),
    ('BOSS_WATER_BASE_WORD',        FRONT, 0xE900, 64,     (BOSS,), 'boss 1 water'),
    ('GAMEPLAY_DOWN_MARIO_BASE_WORD', FRONT, 0xE980, PLAYER_FRAME_WORDS, (PLAY, BOSS), 'crouch Mario', 128),
    ('GAMEPLAY_DOWN_LUIGI_BASE_WORD', FRONT, 0xEB00, PLAYER_FRAME_WORDS, (PLAY, BOSS), 'crouch Luigi', 128),
    ('SATB_BASE0',                  FRONT, 0xF000, 256,    SCENES, 'sprite attribute table, page 0'),
    ('SATB_BASE1',                  FRONT, 0xF200, 256,    SCENES, 'sprite attribute table, page 1'),
    ('STAGE_OBJECT_BASE_WORD',      FRONT, 0xF300, 'sized:stage_objects', (PLAY,), 'per-level stage object shapes', 128),

    # ---- VDC0 (BACK): background plane ----
    ('BG_BAT_BASE_WORD',            BACK,  0x0000, 64*64,  SCENES, 'background BAT, 64x64 cells'),
    ('BG_TILE_BASE_WORD',           BACK,  0x1000, 'sized:bg_tiles', (PLAY, BOSS, BOOT), 'background/boot tile set'),
    ('CONTINUE_TILE_BASE_WORD',     BACK,  0x2000, 'sized:continue_tiles', (CONT,), 'continue room tile set'),
    ('BOOT_DUAL_BACK_BASE_WORD',    BACK,  0x5000, 'sized:boot_dual', (BOOT,), 'opening dual-plane streaming buffer'),
]

# Overlaps that already existed when this map was written.  They are reported
# as warnings instead of errors so the build keeps working, but they are real:
# each one is a bank being written over another that is still live.  Fixing one
# means relocating a bank above and deleting its entry here -- never add to this
# list to silence a conflict a new change introduces.
KNOWN = {
    ('BOSS_BASE_WORD', 'CONTINUE_PLAYER_BASE_WORD'):
        'the ten-pose Bowser bank runs to 0xBD80 and overwrites the crouch art '
        'that upload_game_sprites() just put at 0xB300',
}


def _fields(entry):
    name, chip, base, words, scenes, note = entry[:6]
    align = entry[6] if len(entry) > 6 and entry[6] else 64
    waivers = entry[7] if len(entry) > 7 else []
    return name, chip, base, words, tuple(scenes), note, align, waivers


# --------------------------------------------------------------------------
# Sizes measured from the generated data, so the map tracks the real assets.
# --------------------------------------------------------------------------
def measure():
    levels_src = (PROJECT / 'src/game_levels_generated.c').read_text()
    assets_src = (PROJECT / 'src/assets_generated.c').read_text()

    # Enemy variants are packed, not strided: each one owns cols x rows cells.
    vrows = re.search(r'const EnemyVariantData g_enemy_variants\[\] = \{(.*?)\n\};', levels_src, re.S).group(1)
    variant_words = []
    variant_hit = []
    for row in re.findall(r'\{(.*?)\}', vrows, re.S):
        f = [x.strip() for x in row.split(',')]
        # ...,width,height,cols,rows,has_hit_frame,shot_cols,baseline.  The
        # bank is cols x rows cells; reading the last two fields instead --
        # which is what this did before the record grew a shot and a baseline
        # -- measured every level's enemy banks as a few hundred words and let
        # the hit run overwrite whatever the map had parked above it.
        variant_words.append(int(f[-5]) * int(f[-4]) * 64)
        variant_hit.append(int(f[-3]))

    rows = re.search(r'const GameLevelData g_game_levels\[19\] = \{(.*?)\n\};', levels_src, re.S).group(1)
    levels = []
    for row in re.findall(r'\{(.*?)\}', rows, re.S):
        f = [x.strip() for x in row.split(',')]
        ids = re.search(r'\b' + f[-1] + r'\[\d+\] = \{(.*?)\};', levels_src, re.S)
        vids = [int(v) for v in re.findall(r'\d+', ids.group(1))] if ids else []
        words = [variant_words[v] for v in vids]
        # Only variants with a real death pose take room in the hit bank.
        hit = [variant_words[v] for v in vids if variant_hit[v]]
        levels.append({'set': int(f[0]), 'enemies': f[-3], 'variants': int(f[-2]),
                       'enemy_words': sum(words) * 2, 'enemy_hit_words': sum(hit)})

    sets = re.search(r'const StageSetData g_stage_sets\[12\] = \{(.*?)\n\};', levels_src, re.S).group(1)
    set_tiles = [int(row.split(',')[-2]) for row in re.findall(r'\{(.*?)\}', sets, re.S)]

    def enemy_kinds(symbol):
        m = re.search(r'\b' + symbol + r'\[\d*\] = \{(.*?)\};', levels_src, re.S)
        if not m:
            return set()
        vals = [int(v) for v in re.findall(r'-?\d+', m.group(1))]
        return {vals[i + 7] for i in range(0, len(vals) - 7, 8)}

    bg_assets = {m.group(1): int(m.group(2))
                 for m in re.finditer(r'const BgAsset (\w+)\s*=\s*\{\s*\d+,\s*\d+,\s*(\d+)', assets_src)}

    for lv in levels:
        lv['kinds'] = enemy_kinds(lv['enemies'])
        lv['tiles'] = set_tiles[lv['set']]

    stage_object_words = max([0] + [int(m.group(1)) for m in
                                    re.finditer(r'static const unsigned short stageobj\d+_words\[(\d+)\]', levels_src)])

    boot_dual = 0
    m = re.search(r'#define BOOT_DUAL_BACK_WORDS\s+(\w+)', (PROJECT / 'src/boot_dual.h').read_text())
    if m:
        boot_dual = int(m.group(1), 0)
    if not boot_dual:
        # The streaming buffer runs to the end of the tile region it feeds.
        boot_dual = 0x1000

    return {
        'levels': levels,
        'sized': {
            'stage_tiles': max(set_tiles) * TILE_WORDS,
            'enemy_variants': max(lv['enemy_words'] for lv in levels),
            'enemy_hit': max(lv['enemy_hit_words'] for lv in levels),
            # BOWSER_SPECIAL_FRAME_WORDS (7*6*64) * BOWSER_SPECIAL_FRAME_COUNT,
        # the largest body bank any boss uploads.
        'boss_body': 7 * 6 * 64 * 7,
            'bg_tiles': max(bg_assets.get('g_level_bg_asset', 0), 1000) * TILE_WORDS,
            'continue_tiles': bg_assets.get('g_continue_base_asset', 0) * TILE_WORDS,
            'menu_combo': bg_assets.get('g_menu_combo_asset', 0) * TILE_WORDS,
            'stage_objects': stage_object_words,
            'boot_dual': boot_dual,
        },
    }


MONKEY, STRIKE_SNAKE, CRATE_MIMIC, CHIP = 1, 3, 4, 7
OPTIONAL_BY_KIND = {
    'MONKEY_SPECIAL_BASE_WORD': MONKEY,
    'ENEMY_BANANA_BASE_WORD': MONKEY,
    'STRIKE_SNAKE_BASE_WORD': STRIKE_SNAKE,
    'CRATE_MIMIC_WALK_BASE_WORD': CRATE_MIMIC,
    'CHIP_BOMB_BASE_WORD': CHIP,
    'CHIP_ATTACK_BASE_WORD': CHIP,
}


def resolve(words, sized):
    if isinstance(words, str):
        return sized[words.split(':', 1)[1]]
    return words


def overlaps(a, b):
    return a[1] > b[0] and b[1] > a[0]


def check_group(banks, label, problems, warnings):
    """banks: list of (name, base, words, waivers)."""
    for i in range(len(banks)):
        for j in range(i + 1, len(banks)):
            an, ab, aw, awv = banks[i]
            bn, bb, bw, bwv = banks[j]
            if not overlaps((ab, ab + aw), (bb, bb + bw)):
                continue
            if any(w.split(':', 1)[0] == bn for w in awv) or any(w.split(':', 1)[0] == an for w in bwv):
                continue
            known = KNOWN.get((an, bn)) or KNOWN.get((bn, an))
            record = ((an, ab, ab + aw, bn, bb, bb + bw, known), label)
            (warnings if known else problems).append(record)


def check(data):
    problems = []
    warnings = []
    sized = data['sized']
    resolved = []
    for entry in BANKS:
        name, chip, base, words, scenes, note, align, waivers = _fields(entry)
        n = resolve(words, sized)
        resolved.append((name, chip, base, n, scenes, note, align, waivers))
        if base % align:
            problems.append(f'{name}: 0x{base:04X} is not {align}-word aligned')
        if base + n > VRAM_WORDS:
            problems.append(f'{name}: 0x{base:04X}+{n} runs past the end of {chip} VRAM')

    for chip in (FRONT, BACK):
        for scene in SCENES:
            if scene == PLAY:
                continue
            group = [(n, b, w, wv) for n, c, b, w, s, _, _, wv in resolved
                     if c == chip and scene in s]
            check_group(group, f'{chip}/{scene}', problems, warnings)

    # The play scene is checked once per level: the enemy banks and the stage
    # tile set are sized from that level's own data, and the monkey/snake/mimic
    # banks only exist when the level actually spawns them.
    for idx, lv in enumerate(data['levels'], 1):
        group = []
        for name, chip, base, n, scenes, note, align, waivers in resolved:
            if chip != FRONT or PLAY not in scenes:
                continue
            kind = OPTIONAL_BY_KIND.get(name)
            if kind is not None and kind not in lv['kinds']:
                continue
            if name == 'ENEMY_BASE_WORD':
                n = lv['enemy_words']
            elif name == 'ENEMY_HIT_BASE_WORD':
                n = lv['enemy_hit_words']
            group.append((name, base, n, waivers))
        check_group(group, f'front/play level {idx}', problems, warnings)
        fg = lv['tiles'] * TILE_WORDS
        tile_base = dict((e[0], e[2]) for e in BANKS)['FG_TILE_BASE_WORD']
        player_base = dict((e[0], e[2]) for e in BANKS)['PLAYER_BASE_WORD']
        if tile_base + fg > player_base:
            problems.append(
                f'front/play level {idx}: stage tile set ({lv["tiles"]} tiles) '
                f'runs to 0x{tile_base+fg:04X}, past PLAYER_BASE_WORD 0x{player_base:04X}')
    return resolved, problems, warnings


def format_conflicts(records):
    """One line per conflicting pair, listing every scene it shows up in."""
    grouped = {}
    for key, label in records:
        grouped.setdefault(key, []).append(label)
    lines = []
    for (an, a0, a1, bn, b0, b1, known), labels in grouped.items():
        where = labels[0] if len(labels) == 1 else f'{labels[0]} (+{len(labels)-1} more)'
        line = (f'{an} (0x{a0:04X}..0x{a1:04X}) overlaps {bn} (0x{b0:04X}..0x{b1:04X}) '
                f'in {where}')
        if len(labels) > 1:
            line += ': ' + ', '.join(labels)
        if known:
            line += f'\n    known: {known}'
        lines.append(line)
    return lines


def report(resolved, data):
    out = []
    for chip in (FRONT, BACK):
        out.append(f'--- VDC {"1 (front/sprites)" if chip == FRONT else "0 (back/background)"} ---')
        banks = sorted([b for b in resolved if b[1] == chip], key=lambda b: b[2])
        prev_end = 0
        for name, _c, base, n, scenes, note, _a, _w in banks:
            if base > prev_end:
                out.append(f'  0x{prev_end:04X}..0x{base:04X}  {base-prev_end:6d} words free')
            out.append(f'  0x{base:04X}..0x{base+n:04X}  {n:6d}  {name:32s} [{",".join(scenes)}] {note}')
            prev_end = max(prev_end, base + n)
        if prev_end < VRAM_WORDS:
            out.append(f'  0x{prev_end:04X}..0x{VRAM_WORDS:04X}  {VRAM_WORDS-prev_end:6d} words free')
    out.append('')
    bases = dict((e[0], e[2]) for e in BANKS)
    walk_base, hit_base = bases['ENEMY_BASE_WORD'], bases['ENEMY_HIT_BASE_WORD']
    out.append('Per-level enemy bank usage (front/play):')
    for idx, lv in enumerate(data['levels'], 1):
        out.append(f'  level {idx:2d}: {lv["variants"]:2d} variants -> '
                   f'0x{walk_base:04X}..0x{walk_base+lv["enemy_words"]:04X}, '
                   f'hit 0x{hit_base:04X}..0x{hit_base+lv["enemy_hit_words"]:04X}')
    return '\n'.join(out)


def emit(resolved):
    lines = ['/* Generated by tools/vram_map.py -- do not edit.',
             ' *',
             ' * Both VDC VRAM layouts live in tools/vram_map.py, which checks that no',
             ' * two banks resident in the same scene overlap before writing this file.',
             ' * Add or move a bank there, not here.',
             ' */',
             '#ifndef SMB_PCFX_VRAM_MAP_H',
             '#define SMB_PCFX_VRAM_MAP_H',
             '']
    for chip in (FRONT, BACK):
        lines.append(f'/* VDC {"1 (front/sprites)" if chip == FRONT else "0 (back/background)"} */')
        for name, c, base, n, scenes, note, _a, _w in sorted(
                [b for b in resolved if b[1] == chip], key=lambda b: b[2]):
            lines.append(f'/* 0x{base:04X}..0x{base+n:04X} [{",".join(scenes)}] {note} */')
            lines.append(f'#define {name} 0x{base:04X}')
        lines.append('')
    lines.append('#endif')
    lines.append('')
    HEADER.write_text('\n'.join(lines))


def main():
    data = measure()
    resolved, problems, warnings = check(data)
    text = report(resolved, data)
    if '--report' in sys.argv or '--check' in sys.argv:
        print(text)
    for w in format_conflicts(warnings):
        print(f'vram_map: warning: {w}', file=sys.stderr)
    for p in format_conflicts(problems):
        print(f'vram_map: ERROR: {p}', file=sys.stderr)
    if problems:
        print(f'vram_map: {len(format_conflicts(problems))} layout problem(s)', file=sys.stderr)
        return 1
    if '--check' not in sys.argv:
        emit(resolved)
        print(f'vram_map: wrote {HEADER.relative_to(PROJECT)}, {len(resolved)} banks, '
              f'{len(format_conflicts(warnings))} known overlap(s), no new conflicts')
    return 0


if __name__ == '__main__':
    sys.exit(main())
