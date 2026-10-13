# Native runtime restoration — 0.6.0 development baseline

Date: 2026-10-13. All work and logs are under `G:/Monster Hunter Endfield`.

## Origin and reconstruction

The eight missing local implementation headers and five test sources were
not found in targeted filename searches of `G:/Better Endfield`,
`G:/Endfield Mod Loader` and `G:/Endfield Local Backend`. This is a **new
implementation of the interfaces still called by `native/module.cpp`**, not
recovery of byte-identical historical files. Deleted recovery copies were
not used. The existing `native/core.h` controller and module ID remain.

The EML SDK and nlohmann JSON are real existing local headers copied with
provenance and licenses in [third_party](../../third_party/README.md).
No extra native runtime dependency or missing-tool download was required.

## Implemented behavior

| Header | Behavior |
| --- | --- |
| `retarget.h` | Vector/frame math; stable opposite-vector quaternion; source/target anatomical body frame; T/A rest direction alignment preserving target roll; finger-derived palm alignment; world rest-delta transport; fixed-length two-bone arm IK with reachable-distance clamping. |
| `weapon_pose.h` | Local bounds evidence for a practice blade axis, rejecting ambiguous center grips; per-frame source hand separation and support weights with transition blending. It never turns source socket rotation into an undocumented blade mount or forced down-pointing sword. |
| `grip.h` | Strict binary hand-track loading, dimensions/length/finite values/ranges, linear support-offset sampling. |
| `grounding.h` | Walkable terrain sample validation, source foot-clearance sampling, correction against individual foot/toe floors and the measured sole offset. |
| `practice.h` | Read-only target snapshots copied into independent HP; segment/sphere contact; temporally sampled blade sweep; once per target per move window; actual clamped local damage and statistics; suspension excludes stale swept positions. |
| `practice_hud.h` | Dedicated UI thread; transparent, no-activate, click-through Win32 overlay; hides when the game process loses focus; explicit update/start/stop lifetime; queue-ready synchronization for immediate shutdown. |
| `audio.h` | RIFF PCM 8/16/24/32 and float32 reading; mono/stereo resampling to 48 kHz stereo signed PCM16; bounded loudness balancing; up to 16 independent voices mixed by the Windows endpoint; completion polling; reset/unprepare/close on stop; unavailable output returns failure without stopping animation. |
| `audio_timeline.h` | Motion-serial and time-based cue progression; repeated moves/loops; independent multiple clips; mute consumes elapsed cues without a later burst; reset on cleanup. |

Audio follows one offline PCM path. No game Wwise playback event is invoked.
Practice contains no entity-HP writes, attack calls, backend dependency,
network transport, rewards or save manipulation. Game networking itself is
outside this pure local simulation.

### Restored binary hand-track contract

The historical `.lsg` files were absent. The explicit replacement is:

```
offset 0:  LSG1 ASCII magic
offset 4:  little-endian u32 frame count
offset 8:  little-endian u32 integer FPS
offset 12: little-endian u32 component count, exactly 4
offset 16: frame-count rows of four little-endian float32 values
           support_offset_x, support_offset_y, support_offset_z, weight
```

Offsets are in source metres, weight in [0,1]. Dimensions must match the
associated body animation; malformed files fail instead of silently
enabling uncalibrated IK. The asset baker is being restored separately.

## Validation

Fresh configure directory: `build/runtime-restore` (VS 2022 x64, C++20).
Configure/build logs are `build/runtime-restore-configure.log` and
`build/runtime-restore-build.log`. DLL and all real test sources compile.
`CMakeLists.txt` reports 0.6.0 and includes the separate character-selection
regression authored by the root agent.

Tests exercise quaternion direction/basis transport, anatomical palm
alignment, fixed bone lengths, singular/invertible Unity column-major
matrices, measured weapon axes, grip binary integrity, grounded/airborne
clearance, swept local hits, independent HP/refresh/deduplication, real WAV
conversion, multiple cue IDs, mute/replay, invalid audio endpoint, successful
silent overlapping output when an endpoint exists, and runtime configuration,
messages, transform-write ownership and HUD early shutdown.

`runtime_tests.cpp` includes the actual module and supplies a narrow fake
managed API. The fake transforms hold local rotations/positions/aliveness;
there is no game process or server. It verifies restoration only when a
transform still equals this module's last write, preserving another
system's intervening writes. It also exposed dead-transform write flags
remaining set in the legacy bone loop; the shared restoration lambda must
clear them even when a transform has disappeared.

At the first run, six of seven tests passed; `longsword_runtime` stopped on
that real legacy ownership defect. The root agent replaced the bone loop
with the shared `restore` lambda, which also restores position-only writes.
After that production repair, the DLL rebuilt and **all seven tests passed**
in 0.53 seconds. The tests retain destroyed-transform and foreign-write
cases as regressions. `build/runtime-restore-tests.log` records the latest
actual result. Successful audio-endpoint overlap and completion were tested
with silent PCM; no audible preview or game injection was performed.

## Limits

This reconstruction makes implementation and failure paths testable; it
does not certify the new character poses or Levatain's visual weapon in
game. The module's scene-query integration and baked ground/hand/weapon
tracks are separate inputs. Floor sampling and swept sphere practice are
approximations, not cloth collision or game combat hit boxes. Missing or
invalid required animation/profile resources still fail initialization.
Source weapon mounting cannot be inferred from the existence of hand
tracks. No build artifact hashes were computed and no commit/push was made
by this restoration task.
