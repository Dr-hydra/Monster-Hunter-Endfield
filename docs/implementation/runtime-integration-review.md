# Runtime integration review

2026-10-13, `G:/Monster Hunter Endfield`. This is a bounded code and
simulated-host review of the two-character/independent-blade integration,
not a game test. The reviewer changed only regression tests and this note;
the root agent made the production changes in `native/module.cpp`.

## Findings addressed

1. **Native animation contaminated the mounting transform.** The former
   `CaptureWeaponModel` recomputed `inverse(socketWorld) * weaponWorld`
   each frame. A simulated native 90-degree weapon-root animation therefore
   changed the showcase mount by 90 degrees. The module now keeps the first
   valid near-hand runtime sample for that model/parent/socket identity.
   This remains a runtime sample, not proof of the prefab's authoring pose.

2. **A reused model could have a new parent.** The former code read the
   parent only when the model identity changed. Local writes were then
   calculated against the stale parent's world matrix. Parent/socket
   identity is now checked, pinned and invalidates the previous sample.

3. **Axis detection accidentally controlled model following.** The former
   `ApplyWeaponSockets` wrote the model only when a blade axis was identified.
   Multi-bone or unavailable meshes could thus leave the weapon at its native
   position despite a valid mount. Following now depends on `mountCaptured`;
   practice-axis confidence remains a separate condition. A regression
   moves the hand by two metres with no calibrated blade axis and verifies
   the existing 0.07-metre installation, including restoration.

4. **Initially disabled renderers were omitted.** A later activation of a
   weapon LOD/part could escape hiding and appear alongside the independent
   sword. The integration now tracks both enabled and disabled renderers,
   records an enabled state before its own hide, and restores its visibility
   writes/pins. Failed discovery, retention or hiding keeps the visual clone
   hidden and restores the native equipment. The visual helper distinguishes
   prepared (`Ready`) from actually shown (`Visible`); practice and status
   use the shown state.

5. **Height scaling was tied to one numerical rig value.** Scale now uses
   the source rig's hip-to-lowest-foot distance after source-body alignment.
   For the current source, the directly measured distance is
   1.04611003398895 metres, consistent with the old 1.0461 constant. Reading
   the rig makes future source-skeleton changes explicit.

The existing restoration regressions still verify destroyed transforms,
position-only writes, intervening foreign transform writes and renderer
pins. No down-pointing blade angle or fabricated source mount was added.

## Coordinate checks

Runtime Unity matrices remain column-major. Body rest recovery uses
`inverse(modelWorld) * rendererWorld * inverse(bindpose)`. A rigid single-bone
skinned blade uses `inverse(weaponWorld) * boneWorld * bindpose` for its bounds
evidence; it does not assume that the renderer transform is the skin transform.
The tests exercise inverse/multiply/point/rotation on a scaled, rotated,
translated matrix and reject a singular matrix.

Body animations contain source world rest deltas. Retargeting transports
them through the source-to-target anatomical body basis, then applies the
target aligned rest rotation. Finger-derived palm frames, fixed-length arm
IK and the source/target scale retain separate roles. The review found no
reason to add an Euler-angle correction or point the sword at the ground.

## Validation and limits

Fresh incremental build in `build/development` completed, including the
native DLL and real test executables. All **eight non-asset tests passed**
in 0.62 seconds:

`longsword_core`, `longsword_runtime`, `longsword_audio`,
`longsword_retarget`, `longsword_practice`, `longsword_weapon`,
`character_binding`, `visual_weapon`.

Logs: `build/integration-review-build.log` and
`build/integration-review-tests.log`. The required-resource initialization
test `module_assets` was deliberately excluded while the independent asset
restoration/baking task is incomplete; this pass does not prove a package
can initialize from the current assets.

No game/server was launched, packets emitted by this test harness, artifact
hashes calculated or Git commits made. Source-pose/weapon alignment, rendering,
material animation, terrain and actual Levatain/female administrator behavior
still require game validation. First runtime mount sampling during a native
transition can itself be non-authoritative; it is not a substitute for
verified installation data. Renderer booleans alone also cannot identify a
second system writing the same `false` value during the hide interval.
