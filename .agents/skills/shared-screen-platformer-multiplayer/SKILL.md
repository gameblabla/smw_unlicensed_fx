---
name: shared-screen-platformer-multiplayer
description: Implement or repair simultaneous shared-screen multiplayer in a platform game, including independent input, player state, damage, lives, elimination, camera ownership, and in-place respawning while preserving single-player rules.
---

# Shared-screen Platformer Multiplayer

Start from the reference game's observable rules and state transitions. Do not infer multiplayer merely from a menu option or second character asset: trace controller reads, player object slots, damage/death handlers, reserve counters, camera selection, rendering, stage completion, and game-over/continue gates.

## Model players explicitly

Give each player independent position, velocity, input-edge history, movement/animation state, health, invulnerability, lives, death/respawn state, and eliminated flag. Keep stage objects, enemies, camera, score rules, and scene transitions shared only where the design requires them.

Treat participant count and character identity as separate data. Never choose a player's art, palette, abilities, or pose from a global "multiplayer enabled" flag. Resolve those properties from that player's slot or record, and audit every pose branch for every identity: idle, walk, jump, crouch, carry, throw, hurt, death, and re-entry.

Audit old file-scope player globals before reusing the single-player update path. A safe conversion either parameterizes helpers by player or deliberately binds a current-player context, then restores it before shared-world work. Never advance enemies, timers, music, stage transitions, or DMA once per player by accident.

Read every physical controller used by the mode and keep edge detection per controller. Initialize all required ports. Verify controller numbering from the target SDK or input backend rather than assuming it.

## Death and re-entry

Represent death as a state machine rather than reloading a stage unconditionally. Separate:

- damage grace time;
- death animation/fall;
- reserve-life consumption;
- permanent elimination at zero reserves;
- off-screen or top-of-screen positioning;
- a bounded steering/hover interval, if the reference uses one;
- the drop back into collision;
- post-entry mercy invulnerability;
- the all-players-eliminated transition.

Consume one life exactly once. An eliminated player must stop participating and must not be revived by a later stage load. Enter continue/game-over only when every participating player is eliminated. Preserve the original full reload behavior in single-player unless the requested design changes it.

During re-entry, clamp steering to the current shared view. Restore health and transient movement state deliberately; do not restore lives. Decide whether hazards can affect the player during the drop and make the invulnerability timer match real frame rate.

## Shared-world interactions

Check both players against terrain, moving platforms, enemies, projectiles, pickups, bosses, stage exits, and pits. Define ownership for carried objects and projectiles instead of sharing one global owner slot. Run shared actors once per frame, then evaluate contacts for every active player.

Choose a camera rule that remains valid if either player is eliminated. Common policies are leader/rightmost progress, a bounded midpoint, or the surviving player. Clamp or regroup lagging players according to the reference; never leave the camera permanently attached to an eliminated object.

## Verify

Build deterministic two-controller replays covering: both players move independently; every supported pose preserves each player's identity; each can die while the other continues; re-entry steering; mercy duration; one player reaches zero lives; both reach zero lives; continue acceptance/timeout; stage and boss transitions; and single-player regression. Capture transition frames and confirm that shared simulation advances once per frame. Use a player-by-pose matrix when a branch has dedicated art or palette handling; testing only both idle characters will miss identity leaks in exceptional poses.
