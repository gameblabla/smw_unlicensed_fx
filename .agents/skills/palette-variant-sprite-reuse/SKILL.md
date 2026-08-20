---
name: palette-variant-sprite-reuse
description: Add visually distinct character or object variants on palette-based sprite hardware when pattern VRAM is constrained, using measured bank reuse, palette allocation, and frame-safe publication.
---

# Palette Variant Sprite Reuse

Measure the live scene before allocating anything: pattern banks and their exact word/byte sizes, alignment rules, sprite attribute tables, palette lines, tile/map banks, scene lifetimes, and peak sprite-cell count. Use the repository's layout checker or linker/map data when available. Do not declare memory exhausted or free based only on nominal capacity.

Prefer these representations in order:

1. Reuse identical pattern data and select a different hardware palette.
2. Share common frames and allocate only genuinely different poses.
3. Reuse banks whose scene lifetimes do not overlap.
4. Stream or double-buffer only when the transfer and publication boundary are proven safe.

A palette-only variant works only when both characters' pixels use compatible color indices. Compare the source palettes and indexed art. If indices encode different semantic regions, remap the offline asset or keep the required unique frames; changing the palette alone may recolor skin, outlines, and clothing incorrectly.

Allocate a stable palette line for each simultaneously visible variant. Audit every scene separately, especially boss rooms and HUDs, because a line free in an ordinary stage may be owned by an arena, enemy, effect, or UI elsewhere. Upload the palette while output is hidden or in a verified safe interval, and make sprite attributes select it consistently for standing, walking, jumping, crouching, carrying, throwing, hurt, death, and UI icons.

Keep identity selection separate from mode selection. A two-player flag answers how many participants exist; it does not identify the sprite currently being submitted. For each render branch, resolve `(player identity, pose)` to a compatible `(pattern bank, palette line)` pair. Dedicated indexed art may require its own shared palette even when normal poses reuse another character's pattern bank.

When sharing a pattern bank, the sprite address must remain identical and only the palette bits should differ. Do not upload one character over a bank while another character can still be scanned from it. If palette lines are scarcer than pattern memory, invert the tradeoff deliberately and document it in the layout source of truth.

Verify the layout checker, build, and native-resolution captures with both variants visible through movement, every exceptional pose, damage, death, transitions, and palette effects such as fades or cycling. Record a player-by-pose matrix and inspect both identities in each dedicated-art branch. Inspect the first publication frame as well as the settled image; a correct final palette does not rule out one-frame mixed colors.
