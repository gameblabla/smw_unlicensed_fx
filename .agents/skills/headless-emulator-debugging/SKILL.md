---
name: headless-emulator-debugging
description: Reproduce, diagnose, fix, and verify visual or input-driven bugs in games and interactive software using a headless emulator, deterministic controller scripts, screenshots, video, save states, and source inspection. Use when a bug must be reached through emulated input or understood across frame boundaries; do not use for emulator-core bugs unless the evidence points into the emulator itself.
---

# Headless Emulator Debugging

Turn a visual report into deterministic evidence before changing code. The goal is a source-level fix whose behavior is demonstrated at the exact transition where the defect used to occur.

## Establish scope

- Read repository guidance and inspect the working tree first. Preserve unrelated modifications and generated artifacts.
- Identify the program under test, the headless runner, the build command, media or executable path, and any existing input scripts, save states, screenshots, or verification directories.
- Treat an emulator save state as evidence, not as a portable rebuilt program. A state may contain old RAM, VRAM, and executable state; prefer a fresh boot after rebuilding unless the state format is known to reload the new executable safely.
- Compare the media and state timestamps or build identifiers before replaying. If the state snapshots executable RAM, use it only for the before-case. Verify a source fix from a fresh boot, a debug warp compiled into the new build, or a deterministic input path that reaches the same coordinates.
- If the report names a visible screen, state, option, or animation, search for its text, state constants, input handling, render function, asset loader, and transition code before searching broadly.

## Build a deterministic replay

1. Run the headless tool's help and use its native controller-command format. Do not guess button names or frame semantics.
2. Start from an existing replay when available. Otherwise create the shortest script that:
   - passes firmware or boot prompts;
   - reaches the relevant state;
   - pulses each button long enough to create one edge-triggered press;
   - releases the button explicitly;
   - repeats the failing transition at least twice.
3. Capture a clean state immediately before the failing action when supported. Use it only to accelerate repeated tests against the same build.
4. Capture the transition, not merely the final frame. Prefer a short lossless video or screenshots for the frame before, the action frame, the following frame, and the settled frame.

When the supplied state starts after the interesting action, continue it long
enough to identify the direction and settling behavior, then construct a fresh
replay that starts earlier. Do not claim that a rebuilt fix was exercised merely
because a newer media image was supplied alongside a state that restored old
code from RAM.

Keep temporary commands, states, videos, and extracted frames outside version control unless the repository explicitly keeps regression artifacts.

## Diagnose from evidence

Compare stable frames and transition frames. Determine which class best fits the evidence:

- wrong source data or asset composition;
- stale or partially initialized tilemap/framebuffer data;
- visible memory rewritten while scanned out;
- upload too large for the safe update window;
- background, sprite, palette, or scroll changes becoming visible on different frames;
- wrong map dimensions, wrapping, stride, bank, palette, priority, or relocation;
- input edge handling toggling more than once;
- emulator-only behavior.

Inspect both writers and consumers of the affected memory. Check loop bounds, layout formulas, sparse-overlay semantics, hardware map dimensions, buffering, register-latch timing, and when output is enabled. A visually plausible final frame does not disprove a one-frame corruption bug.

Use locally available symbols and dumps to replace visual guesses with runtime
facts. A linker map plus a RAM dump can identify the active scene, camera,
buffer index, or transition state without modifying the program. Keep this
read-only and document address base, width, and byte order when decoding it.

Compare the regression with nearby source history and uncommitted work. A new
sprite plane, audio transfer, logging path, or second buffer can expose a latent
blanking-window violation without touching the corrupt layer itself; total
frame workload is evidence, not just the last writer in source order.

Before modifying an emulator core, reproduce with the accurate backend and check whether game code violates documented hardware timing. Use a fast backend only as a secondary comparison.

## Choose the fix

- Fix the invariant that was violated, not the screenshot symptom.
- Prefer preparing complete state while hidden, then publishing it with a small atomic operation such as a buffer, page, pointer, bank, or scroll change.
- Preserve intentional sparse overlays: determine whether zero means black, transparent, unchanged, or an actual tile before composing them.
- Synchronize independently latched layers when they form one visual choice or pose.
- Keep unrelated timing, input behavior, assets, and renderer configuration unchanged.
- Add concise comments only for hardware behavior or data semantics that are not obvious from the code.

## Verify

Rebuild from the changed source and replay from a fresh boot. Then:

- capture several repetitions of the action;
- inspect every transition frame at native resolution;
- use frame hashes to show that only expected complete states occur when the scene is otherwise static;
- confirm both directions or inverse operations;
- exercise the next action after the fixed screen, such as confirming the selection, to catch state-handoff regressions;
- run the project's ordinary checks and a diff whitespace check.

For rebuilt-code verification, record which route proves the new executable was
running. Useful proofs include a fresh boot timestamp, a build-only diagnostic
warp, a changed build identifier visible in RAM, or a deliberately harmless
instrumentation counter. Remove temporary diagnostics before the final build.

For multi-controller sprite changes, add deterministic scenes one below, at,
and one above each per-line sprite and pixel/cell-fetch limit. Make the first
spilled entries visually identifiable and exercise opaque overlap, transparent
holes, both background planes, top and bottom clipping, total SATB capacity,
the frame where both tables first become visible, and a transition that clears
and re-enables the auxiliary sprite plane.

Do not accept a crowded final screenshot alone. Inspect the exact tail entries
that crossed the limit; corruption confined to that tail often identifies a
missing mirrored pattern or palette. When available, record per-chip SATB
contents, overflow status, and referenced VRAM ranges with the screenshots.

With FFmpeg, `ffmpeg -i capture.y4m -f framemd5 hashes.txt` is useful for counting distinct lossless frames. The `select` and `tile` video filters can make a compact transition montage. Hashes detect unexpected states; visual inspection explains them. `framemd5` is an output muxer, not a video filter.

Report the root cause, the invariant restored, the replay used, and the verification outcome. If asked to commit, stage only the intended files and honor the requested signing mode.
