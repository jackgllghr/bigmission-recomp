# Settings, timing and camera investigation

## Implemented scope

The windowed shell loads a validated per-user INI file before starting SDL or
the game. Internal resolution, keyboard/gamepad bindings, axis dead zone and
presentation rate are applied at startup. An opt-in experimental game cadence
override is also available; fixed-step gameplay is not compensated. `--scale` is a transient override.
Frame interpolation gives 60 FPS motion at the native game speed, a FOV
override rescales the game's perspective projections, and the window's startup
size and fullscreen are configurable. The in-game editor belongs to the
launcher work.

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

### Implemented experimental cadence override

`experimental_game_frame_rate` accepts 0, 15, 20, 30 and 60. Zero installs no
hooks. The other values select a four-, three-, two- or one-vblank interval.
`game/frame_rate.*` patches the frame object's interval only when GetVcount
returns to the verified frame-wait site. It checks the object against the game's
global frame-object pointer and validates its memory range and interval. An
unexpected object stops the experiment rather than writing to unrelated memory.

The frame-wait routine contains at most two vblank waits, rather than a loop
over its interval. An interval-only experiment therefore produced about 30
updates per second for both 15 and 20. The implemented hook also supplies the
frame's absolute deadline to the routine's two WaitVblankStartCB call sites.
Both calls share that deadline. The deadline accounts for vblanks already
elapsed since the previous game frame, so it does not accumulate another full
interval or drift. Every intervening PSP vblank still happens, including kernel
frame observers and guest interrupts. Other callers retain ordinary HLE waits.

This operates at import boundaries so it also covers generated same-unit jumps
and AOT call chaining. Generated source and the user's ISO/executable are not
modified. GameSession validates the executable hash before installing the hooks.

The game's FPS getter (`0x088FF154`) derives 15/20/30/60 from this same interval,
so its FPS-aware counters see the selected cadence. Other update paths embed
fixed 1/30-second steps and integer frame counters, including routines around
`0x0888B7E8`, `0x0895E6A4`, `0x0898D748` and `0x089C3C48`. The override does
**not** compensate those paths. Higher rates can accelerate movement, animation
or scripts; lower rates can slow them. This is an explicitly experimental
cadence control, not verified timing-correct gameplay.

Before offering timing-correct higher simulation rates:

1. Locate the constructor/setter for the frame object's interval and its callers.
2. Track the game timestep through movement, animation, camera and script updates.
3. Test a controlled one-vblank experiment with identical scripted input and
   compare position, animation duration, guest elapsed time and completion events.
4. Check audio, movies, menus, saves and cutscenes at both rates. If simulation
   cannot be raised independently, investigate interpolation separately.

### Frame interpolation (implemented)

Compensating the cadence override inside the game is not practical: besides
the FPS-aware counters, movement, animation and frame-counted script waits all
step by a fixed amount per game frame. Interpolation leaves the game at its
native cadence instead and works on the GE's draw calls only
(`gpu/frame_interpolator.*`, `shell/ge_renderer.*`).

- A game frame ends at each accepted `sceDisplaySetFrameBuf`. A headless probe
  (`progress/interp_probe.cpp`, ignored) found no draws between a request and
  its latch over 200 s, so this boundary splits frames cleanly.
- Draws pair by vertex address, index address, vertex type, primitive, count
  and texture, falling back to the same key without addresses for buffers the
  game rebuilds each frame. The render target is excluded, since the game
  alternates two display buffers (0x04000000, 0x04088000). Instances of one mesh pair
  with the nearest previous instance, compared in view space. Over 200 s of title,
  menu and hub with scripted input, 99.3% of draws matched. About six per
  frame were unmatched, and a few per second snapped.
- Matrices, CPU-transformed vertices, normals, colours and texture offsets are
  blended. Texture coordinates are not, because they select animation cells. A draw
  whose anchor moves more than 35% of its view depth (160 pixels for
  screen-space draws), or any vertex that jumps that far, is drawn unblended.
- The renderer draws the blended copies into a second set of render targets.
  Textures are resolved when a draw is queued, and render-to-texture reads the
  second set. The queue is replayed at the next vblank.
- At the vblank where a new framebuffer latches, the in-between picture is
  presented, but only if the previous frame lasted two or more vblanks. The real
  frame follows one vblank later.

Verification with the windowed shell, capturing every vblank: on the title
logo and while walking in the stage hub, frame-to-frame differences that
alternate 0 / X natively became X/2, X/2. Each real frame was pixel-identical
to the native capture one vblank earlier, which confirms that game timing is
unchanged. The title movie is a 30 FPS video sprite and stays as it is.
Not yet checked: stages with many particles, cutscenes, and the save dialog.

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

Previously implemented (`gpu/fov.*`, since replaced): `fov_degrees` scaled the x and y terms of every
perspective projection (clip w taken from z, no constant w) by
`tan(36.5/2) / tan(fov/2)`, through GeState's view hook, before the renderer
and the interpolator see the draw. Orthographic and through-mode draws are
untouched. This is projection-level, not camera-level. Menus and cutscenes
with 3D cameras widen too, and the game's culling does not know about the wider
view. In the stage hub at 60 degrees no missing geometry was visible at the
edges.

The override applies only to gameplay-camera frames (`game/gameplay_camera.*`).
Every scene shares one camera parameter block, `*(*(0x08A82B24) + 20)`, which
holds the near plane at +0 and the yaw at +240. A routine at 0x088807A4 copies
its vectors to the render camera at 0x08AA59E0 and 0x08AA59F0. The block's
contents alone do not separate scripted and gameplay cameras, so the hook
uses the routine that writes it, found with a write watch (chaining off) on
the block's position:

| Scene | Camera writer |
| --- | --- |
| Title, menus | none (static) |
| Hub, while walking | 0x08809014 (0x08809814 runs every hub frame) |
| Warp tunnel | 0x0882CEB4 |
| Stage intro | 0x088946A0, then 0x088944DC / 0x088944F4 |
| Stage gameplay | 0x0888A09C, the follow camera |

Call hooks showed that 0x0888A09C and 0x08809814 run once per game frame
exactly while the player is in control. They stop in the stage-select and
pause menus, and only three stray calls happen during the stage load. Both
are reached through cross-unit calls, so the runtime's pre-chained-call and
pre-dispatch hooks see them, at no measurable speed cost. A frame counts as
gameplay if either ran in that game frame or the one before. The stage's
pause state runs 0x08866918 every paused frame and in no other sampled scene,
including title, menus, stage select, hub, tunnel, intro and gameplay. A pause
entered while gameplay is active extends it, so the paused scene keeps the
wide view; a pause cannot start gameplay on its own.

Verification at FOV 60 against native, one capture per second over a scripted
run: the title, main menu, load menu and loading (25-66 s), stage select
(73-75 s) and tunnel and intro (76-85 s) were pixel-identical to native. The
hub (67-72 s), stage gameplay (86-100 s, 105 s on) and the pause menu entered
from it (101-104 s) were widened. Untested: other stage types and vehicles, which may
use other follow cameras and would then stay native; mid-stage events;
minigames.

#### Camera-level FOV (replaces the projection override)

The projection-level override left objects near the screen edges missing at a
wider FOV: the game culled them against its native view. In stage 1-1 at an
FOV of 55.6 degrees the fence and bunting at the left edge of the opening view
were not drawn.

Every camera object keeps its lens as near-plane frustum extents: near at +0,
far at +4, then left, right, bottom and top at +8..+20 (-3.461, 3.461,
-1.961, 1.961 for the common camera, near 6, far 9000). 0x088FD308 makes a
camera: 0x088FD270 sets the FOV parameter at +248 to 62, 0x088FD008 sets the
near and far planes and 0x088FCF80 computes the extents as
`size * 0.002 * near * tan(fov * pi / 360)` for a 480x272 size; 0x088FD40C
recomputes them for a new near plane. They are computed when the camera is made, not
every frame; the trace saw the stage camera made once, at the end of the
stage introduction.

The game builds both its GE projection and its culling from those extents.
Scaling them by 1.6 once the stage camera existed, with no GE override,
widened the picture exactly as the GE override does at the matching FOV
(55.6 degrees), except that the fence at the left edge was drawn. Scaling the
far plane by 4 changed no pixel in the same run, so the far plane is not what
limits the draw distance there.

Implemented (`game/camera_fov.*`): at the view-build call (0x088FD1B0) of
the scene's camera, `fov_degrees` scales the extents by
`tan(fov/2) / tan(36.5/2)` when the call comes from the stage follow camera or
the hub's walking camera, and puts the game's own extents back when another
caller builds a view with that camera. Extents the game recomputed (a new
camera or lens) become the new native ones. The GE projection is no longer
changed.

Verification at 55.6 degrees against native over a scripted run: the title,
menus, tunnel and stage introduction (60-78 s) were pixel-identical to native.
Stage gameplay (80 s on) and the pause menu entered from it were widened, and
matched the extents experiment above. Untested: the hub, other stage types,
vehicles, mid-stage events and minigames.

Remaining refinement: verify HUD placement, menus, movies, cutscenes and
geometry at the frustum edges in the other stage types.

### Draw distance

Level geometry has no distance limit short of the far plane: with the stage
camera's eye pulled back four times as far from its target (a debug hook at
the view build), stage 1-1's beach, rocks and fences were all drawn. The
robots, the far "?" marker and other actors were not, so actors carry their
own limit.

The actor manager draws from the actor list twice a frame. 0x08900A5C calls
each actor's draw (vtable +24) when the fade at actor+300 is at least 1, and
0x08900B14 draws those between 0 and 1 translucent; an actor at 0 is not
drawn. The update loop 0x08900BFC (vtable +16, returning to 0x08900C40 or
0x08900C6C) runs the actor updates, twice per stage frame from 0x08868108
(0x08868158 and 0x08868170) before the camera code. Each class's update sets
the fade itself: it measures from the scene camera's eye
(`*(*(0x08A82B24)+20)+112`) to its own position and calls its fade setter
(vtable +32, typically `actor+300 = f12`). The world matrix translation is at
actor+80 with w = 1 at +92.

The fades logged for three classes (0x088FFF68, 0x0882E2C0, 0x0886B17C) at
camera distances 307.8, 324.0 and 335.2 were 0.42, 0.26 and 0.15, which is
`clamp((350 - d) / 100, 0, 1)`: fully drawn to 250 units and gone at 350.
Other classes write the same limits inline (the floats 250 and 350 near
0x08831840, 0x08875028, 0x088B24D0, 0x088B5AA4) and some step the fade by 0.1
a frame towards their target. The robot's update (0x0883160C) also returns
before its AI and animation beyond 350.

Since the limits are inlined per class and the code is compiled ahead of
time, there is no one constant to change. Implemented (`game/draw_distance.*`):
while the update loop runs an actor's update, the eye at cam+112 is moved
along the line from the actor (+80) towards the real eye, so that the
distance beyond 200 units is divided by `draw_distance`; the real eye is put
back at the next call from the loop's caller (or from another thread's
stack), before the camera code and the draws read it. An eye the game wrote
in the meantime is kept. Directions from actors to the camera, and anything
within 200 units (near fades), are unchanged. At 4, actors fade from 400 to
800 units instead of 250 to 350.

Verification, stage 1-1, opening view at 88 s: with the camera pulled back
four times, native drew none of the robots by the far rocks and no far "?"
marker; at 4 they were all drawn and the framing was pixel-identical
elsewhere. At the normal camera, the robots and marker near the palm, faded at
native, were opaque at 4. A walk to the first "?" marker, its dialogue and a
pause matched between native and 4. Side effect: classes that stop animating
when far (the robots) now animate and patrol to the same scaled distance, so
distant robots start moving sooner. Untested: other stages, the hub, sound
volume or panning tied to the eye, and actors whose position is not the
world matrix at +80 (those see the real eye).

## Verification

- Native macOS arm64 shell and settings test executable build successfully.
- All 30 CTest suites pass, including synthetic settings/input/clock and
  game-frame-rate tests.
- Tests cover defaults and partial files, readable binding round-trips, malformed
  input, preserving invalid existing files, keyboard alternatives and unbinding,
  shared gamepad bindings, trigger thresholds, axis reversal and dead zones.
- Clock tests cover native and capped cadence on actual rational PSP vblank
  timestamps, repeated timestamps and large time jumps.
- The 120-second headless ISO probe above supplies timing/camera evidence.
- The production override was exercised for 120 seconds of guest time at each
  rate, with AOT chaining enabled and isolated memory sticks. All runs reached
  the requested stop time without an unmodeled import. In steady five-second
  windows, completed GE lists and override calls matched:

  | Requested cadence | Game frames per five seconds |
  | --- | ---: |
  | Native | 149-150 |
  | 15 | 74-75 |
  | 20 | 99-100 |
  | 30 | 149-150 |
  | 60 | 299-300 |

  Logs are ignored at `analysis/framerate-{0,15,20,30,60}.log`; the local probe
  is `progress/framerate_probe.cpp`. These runs verify update cadence, not
  movement/animation speed, audio sync or playable level completion.
- Frame-rate tests cover all supported intervals, invalid rates, unexpected
  objects, caller isolation, native vcount preservation, both wait sites and
  steady-state deadlines with every intervening PSP vblank observed.
- No interactive GPU presentation test or Windows/Linux build was performed
  for this chunk. Gameplay FOV and timing-correct higher simulation rates remain
  unimplemented.
