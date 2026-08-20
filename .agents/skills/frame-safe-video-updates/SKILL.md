---
name: frame-safe-video-updates
description: Diagnose and implement corruption-free graphics updates on tile, sprite, framebuffer, palette, and scanout hardware. Use when visual data is correct at rest but tears, corrupts, flashes, or becomes inconsistent during menus, scrolling, transitions, animation, or layer changes, especially on retro consoles and embedded display controllers.
---

# Frame Safe Video Updates

Reason about what the display reads during each frame, not only what memory contains after the update finishes.

## Model the visible state

For every affected layer, identify:

- the backing memory region and its real hardware layout;
- tile/pixel data, map or descriptor data, palette, sprite attributes, scroll, bank, and enable registers;
- which values are live immediately and which latch at HBlank, VBlank, DMA completion, or a later frame;
- the display's active region and the safe write budget outside it;
- whether a zero entry means black, transparent, empty, inherited, or unchanged;
- whether map dimensions and wrapping match the address formula used by software.

Do not infer layout from array shape alone. Verify controller configuration and hardware addressing. A nominal 64-by-64 map may be linear, quadrant-based, banked, wrapped, or overlap another allocation depending on the device.

## Recognize unsafe update patterns

Treat these patterns as suspects when corruption appears only during changes:

- rewriting a visible map, framebuffer, palette, or pattern bank in place;
- uploading patterns without the map that names them, or the reverse;
- performing more writes than fit in VBlank;
- changing a scroll/page/bank before the matching buffer is complete;
- clearing or reusing memory while its layer is still enabled;
- treating a sparse delta image as a complete replacement;
- updating sprite attributes and background selection in the same function even though they latch on different frames;
- relying on an emulator dropping, delaying, or accepting writes in a way hardware does not guarantee.

Partial corruption often reveals write order: a clean prefix, horizontal boundary, old/new mosaic, or one-frame layer mismatch can identify the live memory and latch that are racing.

A timing regression may be indirect. Adding sprite-table uploads, audio work,
logging, or another controller's updates can push an older in-place write out of
blanking even when the older code did not change. Account for the complete
frame's bus and CPU workload before treating the newest feature as unrelated.

## Prefer prepare-then-publish

When memory permits:

1. Build the complete next state in hidden or off-screen storage.
2. Compose base data and overlays there with explicit semantics.
3. Wait for the required hardware boundary or DMA completion.
4. Publish with the smallest possible operation: flip a page, pointer, bank, map origin, scroll coordinate, or enable bit.
5. Reuse the old buffer only after it is no longer scanned out.

Use this preference order:

1. complete variants in display-addressable off-screen pages, banks, or map regions, selected by one register;
2. complete preparation while the relevant output is disabled, followed by a small publish operation;
3. a transfer proven to fit inside the real blanking/DMA budget;
4. a deliberately blanked multi-frame transition when that visual behavior is acceptable.

For a small set of fixed screens that fit in display-addressable storage, compose and preload every complete variant once while output is hidden. This is required, not merely an optimization. Place the variants in separate pages, banks, or unused map regions; after publication begins, selection should only change the selector and should not write video memory. Do not dynamically rebuild a fixed variant on each selection. If an addressable map dimension has room for all variants, use it. Do not rewrite hundreds of visible descriptors when one register can select an already-complete view.

For dynamic double buffering, track which buffer is visible and which is writable. Write only the non-visible buffer, publish it, then swap their roles. A single “staging” region cannot be written again after it becomes visible; doing so merely moves the in-place-update bug.

Before accepting a buffering patch, trace at least two inverse transitions and record which buffer is visible and which is writable before every write. Reject the design if either direction writes the selected buffer or if a fixed variant is needlessly reconstructed.

RAM staging followed by a bulk copy into live video memory is not double buffering and is not atomic. It solves the problem only if the final copy occurs while output is disabled or is proven to fit in the safe transfer window.

If memory is insufficient, update during a verified blanking interval, split work across frames while hidden, or update only proven-safe cells. Count writes and include register/DMA overhead; do not assume an ordinary loop fits.

For streamed maps, a one-frame command queue is often enough when full double
buffering is impossible:

```text
during simulation:  compute entering row/column and queue its world coordinate
at frame boundary:  write queued off-screen cells
at same boundary:   publish the scroll or origin that can expose those cells
```

Queue selection separately from hardware writes. Flush data before the matching
selector, scroll, bank, or enable register. Size the queue for the largest legal
camera step, deduplicate repeated cells, and define what happens on scene load
or cancellation. This is safe only when the queued cells are outside the
currently committed viewport and the bounded flush fits the measured blanking
budget.

An implementable patch must use helpers, signatures, and hardware capabilities found in the repository or its included documentation. Do not invent DMA, wait, page-flip, read, or allocation APIs. If a required primitive is absent, state the limitation and use an available alternative.

## Compose sparse data correctly

Determine the producer's contract before merging an overlay:

- replacement: every entry, including zero, overwrites the destination;
- transparent overlay: transparent entries preserve the base;
- sparse patch: absent or sentinel entries preserve the base;
- masked update: a separate mask decides which entries change.

Do not use `if (value)` unless zero is documented or demonstrated to mean “preserve.” Palette or attribute bits can make a visually empty entry nonzero, and an intentional black tile may still need to overwrite the base.

When the complete base is already available in source data, compose from that source rather than reading the currently visible video memory. Live reads add hardware timing and API risks without improving the result.

## Synchronize layers

Background registers, sprite tables, palettes, and DMA commonly become visible at different times. If they represent one logical state:

- queue delayed data first;
- wait for the boundary at which it becomes active;
- publish immediate registers at that same boundary;
- verify both transition directions frame-by-frame.

The safe ordering can be expressed as:

```text
queue_delayed_layer(next_state)
wait_until_that_queue_is_visible()
publish_immediate_selector(next_state)
```

For example, when sprite submission queues a next-boundary table while a background page selector is immediate, the order is:

```text
update_logical_choice()
submit_matching_sprites()       # queues delayed table
wait_one_required_boundary()   # delayed table becomes visible
select_matching_background()   # immediate publish at that boundary
```

Do not publish the immediate selector first, and do not submit the delayed layer after the wait. An update loop that waited before reading input is not automatically synchronized: a DMA queued after that wait may still latch one frame later than an immediate register write made in the same iteration. Reordering calls without an intervening hardware boundary is also insufficient. Base synchronization on documented or observed visibility, not source-code order.

## Split sprites across parallel controllers safely

When two sprite controllers feed a fixed pre-mixer, adding their nominal limits
is not enough. Establish the pre-mixer's pixel winner first, then preserve the
original ordered sprite list under that rule. If one controller always wins, a
later winning-controller entry must not overlap an earlier losing-controller
entry; a per-scanline spill barrier is a conservative way to enforce this.
Also verify how losing-chip sprites interact with the winning chip's background.
A correct sprite/sprite partition can still put a sprite behind the wrong plane.

Budget every constraint independently: SATB entries, sprites per scanline,
fetched sprite pixels or cells per scanline, and publication cost. A list that
fits 64 entries can still overflow one line.

Treat migration between controllers as a complete resource dependency. Every
pattern bank referenced by a migratable entry must be resident where that
controller's SATB expects it; palette selection and geometry must retain their
meaning; and auxiliary SATB pages must not overlap other live video data. Fixed
HUD, cursor, projectile, and effect entries are not exempt when the allocator
can spill them. Derive the mirrored-bank set from entries that may actually
migrate, not from an informal category such as "world sprites."

Prepare complete hidden SATB pages for both controllers before queuing either
publication. Clear and enable both sprite planes as one ownership unit during
scene transitions, while keeping each controller's page index explicit.

## Validate the hardware-safe result

- Reproduce with accurate emulation or real hardware behavior when available.
- Capture the frame before, every transition frame, and the settled result.
- Repeat the transition several times and in both directions.
- On a static scene, lossless frame hashes should collapse to the expected complete visual states; investigate every extra hash.
- Confirm the selected state still drives the correct later behavior.
- Check adjacent transitions that reuse the same VRAM, map, DMA, or layer configuration.
- Re-run after enabling the heaviest concurrent layer or transfer. A fix that
  works only in the lighter single-layer case has not restored the timing
  invariant.

Keep the fix local to the broken ownership or publication path. Do not mask corruption with delays, blank frames, or broad renderer changes unless blanking is part of the intended transition and the timing is verified.
