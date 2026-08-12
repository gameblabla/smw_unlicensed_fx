# Super Mario Bros. (Mega Drive unlicensed) -> PC-FX reconstruction r12

C reconstruction targeting PC-FX HuC6270 VDC hardware in TETSU 262-line / 5 MHz mode. Audio remains stubbed through the sample API; game graphics use VDC0/VDC1 rather than KING/Rainbow rendering.

## Revision 11: full level-data pass

Revision 11 extends the r10 gameplay engine from Stage 1 to the complete 19-slot level progression extracted from the Mega Drive ROM.

## r12 regression repair pass

r12 resumes from the r11 19-level/full-game source and repairs regressions introduced by that expansion:

- Reserve lives and continue credits are independent. A death decrements the HUD life reserve and reloads the current level while lives remain; only zero reserves enter the CONTINUE room. Accepting a continue consumes a separate credit, restores four reserve lives, and restarts the level.
- Level 2 has live one-way moving platforms over its otherwise non-traversable gaps. Their visible 32x16 cells are reconstructed from Level 2's own grass-platform tiles. r11's generator discarded every level-entry object that was not a crate or gem, which is the source of this regression.
- Levels 1, 2 and 4 keep the original 224-line cloud/background artwork and place it at Y+16 instead of extending it to 240 lines by repeating the last tile row.
- Normal gameplay sprites use the source level palette line 0 again. r11's forced palette-line-13 selection was the cause of the yellow/green Mario regression; line 13 remains available for boss arenas.
- The title wall now continues its 80-pixel vertical tile period into rows 224-239, completing the Mario/Luigi faces instead of duplicating the last visible hat row.

The original extracted level-entry object JSON is not embedded in the r11 archive, so the Level-2 platform motion ranges are reconstructed from the retained r11 collision/map geometry rather than claimed as byte-for-byte translations of discarded object records.

Implemented data coverage:

- all **19 normal level slots**;
- all **12 de-duplicated stage graphics/map sets**;
- each level's extracted palette, background layer, spawn point, route/segment list and collision grids;
- all extracted crate, gem and dynamic enemy encounter records for every level;
- all **56 enemy `(gfx_type, base_metasprite, subtype)` variants** actually referenced by the 19 level tables, with two source animation frames and source palette lookup;
- level-to-level progression;
- the seven source-defined boss transitions after levels **3, 6, 8, 10, 13, 16 and 19**;
- boss-mode arena loading, boss collision/damage, projectiles, defeat/fall state and progression to the following level.

The r10 title/front-end, menu, 256x240 presentation, crates, gems, fireballs, heart damage/invulnerability, reserve lives/continue room, death flow and TETSU synchronization are retained.

## Disassembly-backed reconstruction

`docs/DISASSEMBLY_FINDINGS.md` records the ROM offsets used by r11, including:

- normal level loader around `$0065EE-$006986`;
- 19-entry route pointer table at `$14CBA`;
- route-record meanings for `FF0052`, `FF0048` and `FF0050`;
- dynamic enemy table `$D5CE`, gem table `$11AC4` and crate table `$13262`;
- enemy graphics/palette tables around `$F3AE`, `$F426`, `$72338`, `$7246C` and `$F454`;
- boss selection checks at `$16928-$16970`;
- boss-mode asset tables `$69FEE`, `$81A50`, `$A91B0`, `$6D2DE`, `$6A13A`, `$7038C`;
- boss dispatch table `$748E` and the six reachable handler classes.

A copy of the reachable 68000 disassembly used for this pass is included at:

`docs/reference/megadrive_main_reachable_disasm.asm`

The generated C also keeps source-derived boss parameter tables/handler addresses so later behavior work can be audited against the original binary.

## Controls

- D-pad: move Mario
- I: jump
- II: lift/throw crate
- III: throw fireball
- RUN: front-end/continue confirmation
- SELECT: return to menu during gameplay
- MODE1: toggle debug mode (invulnerable, unlimited fireballs)

## Build

With the PC-FX toolchain installed at `/opt/v810-gcc`:

```sh
make clean
make
```

To regenerate the derived assets from the Mega Drive reconstruction kit and patched ROM:

```sh
make assets \
  KIT=/path/to/Super_Mario_Bros_MD_reconstruction_kit \
  ROM=/path/to/smb_patched.md \
  BOSSREF=assets/reference/boss
make clean
make
```

`tools/build_full_game.py` generates `src/game_levels_generated.[ch]`. The three boss-reference captures needed by that generator are included under `assets/reference/boss/`, so a separate boss capture directory is not required unless you want to replace them.

For targeted emulator/development testing, `src/main.c` accepts optional compile-time `FORCE_LEVEL` (0-based) and `FORCE_BOSS` macros. They are **not** enabled in the release build. For example: `make EXTRA_CFLAGS='-DFORCE_LEVEL=2 -DFORCE_BOSS=1'`.

## Current fidelity limits

Revision 11 implements the complete level-data/progression structure, but several source systems still need deeper decompilation for an instruction-faithful port:

1. A small set of late-game route records uses `FF0050 = 2` for vertical room transitions. Their original mode/quadrant metadata is preserved, but r11 currently linearizes these rooms into the horizontal PC-FX world instead of reproducing the exact 2-D transition camera. This mainly affects levels 11, 12, 14, 15 and 19.
2. Enemy graphics/placement/animation are ROM-derived for all levels, but special enemy state machines still share generalized C movement/collision logic rather than being exact ports of every 68000 object handler.
3. Boss occurrence and handler class come directly from the disassembly, and the PC-FX boss actor has damage, projectiles, jumping and defeat/progression. The six individual 68000 boss handler state machines are **not yet instruction-for-instruction translations**; r11 uses source-derived class/parameter differences on a common C boss actor.
4. The final level-19 boss currently returns to the opening/front-end after defeat; the original ending sequence is not yet reconstructed.
5. Full active two-player gameplay is still unfinished. The front-end 2P presentation remains, but r11 does not claim a complete second-player object/controller implementation.

These limitations are recorded rather than hidden so the next reverse-engineering pass can target the remaining behavior without re-extracting the level data.
