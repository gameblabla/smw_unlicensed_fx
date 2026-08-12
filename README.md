# Super Mario Bros. (Mega Drive unlicensed) -> PC-FX reconstruction r12

C reconstruction targeting PC-FX HuC6270 VDC hardware in TETSU 262-line / 5 MHz mode. Audio remains stubbed through the sample API; game graphics use VDC0/VDC1 rather than KING/Rainbow rendering.

## Revision 11: full level-data pass

Revision 11 extends the r10 gameplay engine from Stage 1 to the complete 19-slot level progression extracted from the Mega Drive ROM.

## r12 regression repair pass

r12 resumes from the r11 19-level/full-game source and repairs regressions introduced by that expansion:

- Reserve lives and continue credits are independent. A death decrements the HUD life reserve and reloads the current level while lives remain; only zero reserves enter the CONTINUE room. Accepting a continue consumes a separate credit, restores four reserve lives, and restarts the level.
- Levels 2 and 6 restore the source moving-carrier records from `$10B2A`: three metasprite-741 carriers in Level 2 and the missing metasprite-742 carrier before boss 2. Handler `$F5B0` moves each 48x8 platform vertically through the original 100-pixel down/up cycle and carries Mario while he stands on it.
- Levels 1, 2 and 4 keep the original 224-line cloud/background artwork and place it at Y+16 instead of extending it to 240 lines by repeating the last tile row.
- Level 3 removes its unused top map row and uses the Mega Drive's 224-line active origin; its objects move up 8 pixels and its screen-fixed HUD moves up the full 16-pixel capture border.
- Level 3 and boss rooms extend their 224-line active picture to the PC-FX's 240 lines by repeating the final visible 8-pixel tile row twice; hidden Mega Drive map rows are never exposed.
- Normal gameplay sprites use the source level palette line 0 again. r11's forced palette-line-13 selection was the cause of the yellow/green Mario regression; line 13 remains available for boss arenas.
- Enemy palettes are deduplicated by their 16 color values rather than by generated-array address. This prevents Level 6's 13 same-palette variants from overflowing the 12-line hardware palette table and corrupting the return stack during the load before boss 2.
- Boss damage now follows the Mega Drive state-3 response: a nonfatal crate or fireball hit plays the placeholder hit sample, holds the captured recoil/impact-puff frame for 30 frames without flicker or movement, rejects further hits during that pause, then resumes the interrupted action. Bowser's attached breath flame is also replaced by the detached projectile rather than being drawn underneath it.
- The title wall now continues its 80-pixel vertical tile period into rows 224-239, completing the Mario/Luigi faces instead of duplicating the last visible hat row.

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

`tools/build_full_game.py` generates `src/game_levels_generated.[ch]`. Its boss-reference captures are included under `assets/reference/boss/`: the real boss-1 arena plus four walk and four breath poses, and the older forced-mode shots retained for reference.

For targeted emulator/development testing, `src/main.c` accepts optional compile-time `FORCE_LEVEL` (0-based) and `FORCE_BOSS` macros. They are **not** enabled in the release build. For example: `make EXTRA_CFLAGS='-DFORCE_LEVEL=2 -DFORCE_BOSS=1'`.

## HTML level editor

Start the local editor from the repository root:

```sh
make editor KIT=Super_Mario_Bros_MD_reconstruction_kit
```

Then open `http://127.0.0.1:8765/`. It edits the final PC-FX 8×8 foreground segments and 64×30 background planes, including the two PC-FX-only tile rows at Y=224–239. The layer selector also exposes independent 32×32 foreground/background maps for every boss slot (3, 6, 8, 10, 13, 16 and 19); their extra rows remain editable for wraparound effects. Available operations include pencil, fill, erase, rectangular tile selection/copy/paste, palette and flip selection, map undo/redo, and direct 8×8 tile-pixel editing. Select a rectangle and use **Copy**, then choose **Paste** and click its destination; Ctrl+C/Ctrl+V are also supported. Foreground maps and tile graphics are shared when their source stage set is shared; the editor displays the affected level slots. Levels 9 and 10 share map layouts but use different graphics banks, so their selectors expose only their source-valid segment ranges (0–7 and 8–21 respectively).

The default Y=224–239 foreground is derived by stage family: Level 3 continues its cropped masonry phase, Levels 6 and 9 continue each column's established terrain period, Levels 7–8 extend the jungle interior without repeating decorative cap tiles, and Level 10 onward uses transparent foreground cells so the level-specific background remains visible. The editor and asset generator share this conversion logic.

**Save overrides** writes small JSON files under `level_overrides/` without modifying the reconstruction kit. **Restore default** removes the selected map override and its layer-specific tile-pixel overrides, returning to the extracted Mega Drive source. Visual edits do not alter collision grids.

Apply saved edits and rebuild with:

```sh
make apply-level-edits \
  KIT=Super_Mario_Bros_MD_reconstruction_kit \
  ROM=/path/to/smb_patched.md
make clean
make
```

The full `make assets` target also applies `level_overrides/`. Use `LEVEL_OVERRIDES=/another/directory` with the editor and generator to maintain alternate map sets.

## Current fidelity limits

Revision 11 implements the complete level-data/progression structure, but several source systems still need deeper decompilation for an instruction-faithful port:

1. A small set of late-game route records uses `FF0050 = 2` for vertical room transitions. Their original mode/quadrant metadata is preserved, but r11 currently linearizes these rooms into the horizontal PC-FX world instead of reproducing the exact 2-D transition camera. This mainly affects levels 11, 12, 14, 15 and 19.
2. Enemy graphics/placement/animation are ROM-derived for all levels, but special enemy state machines still share generalized C movement/collision logic rather than being exact ports of every 68000 object handler.
3. Boss occurrence and handler class come directly from the disassembly. Boss 1 now follows the `$7526` action table and its six reachable movement/breath states; the five later handler classes are still source-derived approximations rather than instruction-for-instruction translations.
4. The final level-19 boss currently returns to the opening/front-end after defeat; the original ending sequence is not yet reconstructed.
5. Full active two-player gameplay is still unfinished. The front-end 2P presentation remains, but r11 does not claim a complete second-player object/controller implementation.

These limitations are recorded rather than hidden so the next reverse-engineering pass can target the remaining behavior without re-extracting the level data.
