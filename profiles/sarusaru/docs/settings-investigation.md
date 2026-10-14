# Settings, timing and camera investigation

## Implemented scope

The windowed shell loads a validated per-user INI file before starting SDL or
the game. Internal resolution, keyboard/gamepad bindings, axis dead zone and
presentation rate are applied at startup. `--scale` is a transient override.
The file also retains a FOV preference, with an explicit warning that nonzero
values have no effect yet. The in-game editor belongs to the launcher work.

`PresentationClock` selects swapchain presentations on the unchanged guest
clock. Every vblank still polls input, submits the GE's render work and handles
capture, even when no swapchain image is acquired. Host real-time pacing still
runs on every guest vblank. This avoids changing controller sampling, kernel
waits, music or movie timing when a lower presentation cap is selected.

## Evidence from the supported local executable

On 2026-10-14, a headless `GameSession` probe ran for 120 seconds of virtual time
using the user's verified executable and read-only ISO, an isolated empty
memory stick, and scripted Start/Circle presses. It stopped at the requested
time without an unmodeled import. This is a timing/projection observation,
not a level-completion or camera-state validation.

The local probe source and log are ignored, at `progress/settings_probe.cpp`
and `analysis/settings_probe.log`. Game-derived/generated source stays ignored.
No original game instructions or assets are included in this document.

### Frame timing

The PSP clock in `kernel/guest_clock.hpp` advances at 60000/1001 vblanks per
second. Changing this shared clock would affect controller samples, kernel
waits, display counters, audio and movie playback as well as the game.

Static inspection found the game's frame-wait routine at `0x088FEAD8`. Its
frame object holds a previous vcount at offset 48 and an interval-minus-one at
offset 52. The routine compares the vcount delta with that interval plus one,
waits through `sceDisplayWaitVblankStartCB`, updates its saved vcount and flips
its buffer index. It also calls GE synchronization/buffer helpers.

The probe instrumented the routine's `sceDisplayGetVcount` return site
(`0x088FEB1C`), while preserving that HLE call's vcount result. The interval was
**two vblanks** on every sampled call: 149 or 150 frame waits and completed GE
lists per five seconds, apart from extra startup lists. This supports a native
rate near 29.97 Hz for this observed path. It does not prove that every gameplay
subsystem uses the same timestep or that a one-vblank interval is safe.

Before offering higher simulation rates:

1. Locate the constructor/setter for the frame object's interval and its callers.
2. Track the game timestep through movement, animation, camera and script updates.
3. Test a controlled one-vblank experiment with identical scripted input and
   compare position, animation duration, guest elapsed time and completion events.
4. Check audio, movies, menus, saves and cutscenes at both rates. If simulation
   cannot be raised independently, investigate interpolation separately.

### Projection and FOV

The draw sink recorded non-through, non-clear projections in five-second
windows. Two perspective matrices occurred, with these rounded coefficients:

| Projection | P[0] | P[5] | P[11] | P[15] | Approximate vertical FOV |
| --- | ---: | ---: | ---: | ---: | ---: |
| Common scene | 1.726 | -3.037 | -1 | 0 | 36.5 degrees |
| Narrow startup scene | 3.871 | -6.810 | -1 | 0 | 16.7 degrees |

FOV is inferred from `2 * atan(1 / abs(P[5]))`; these are observed GE
coefficients, not verified camera configuration fields. A negative vertical
scale is part of the game's projection convention. Through-mode draws and
orthographic projections need no FOV adjustment.

The common projection appears both before and after the scripted menu input.
Its coefficient alone therefore cannot establish that a draw is gameplay.
A blanket perspective-matrix override would change other 3D scenes too and
would leave guest camera culling unaware of a wider view.

Before activating FOV:

1. Trace the producer of the projection upload and its camera/FOV parameters.
2. Identify gameplay/menu/cutscene camera state and any intentional zoom paths.
3. Prefer a camera-level hook that updates projection and culling together.
4. Verify HUD placement, menus, movies, cutscenes and geometry at the frustum edges.

## Verification

- Native macOS arm64 shell and settings test executable build successfully.
- All 29 CTest suites pass, including new synthetic settings/input/clock tests.
- Tests cover defaults and partial files, readable binding round-trips, malformed
  input, preserving invalid existing files, keyboard alternatives and unbinding,
  shared gamepad bindings, trigger thresholds, axis reversal and dead zones.
- Clock tests cover native and capped cadence on actual rational PSP vblank
  timestamps, repeated timestamps and large time jumps.
- The 120-second headless ISO probe above supplies timing/camera evidence.
- No interactive GPU presentation test or Windows/Linux build was performed
  for this chunk. Gameplay FOV and higher simulation rates remain unimplemented.
