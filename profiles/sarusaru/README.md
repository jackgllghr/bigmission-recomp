# SaruSaru profile

Native recompilation work for **Saru! Get You! SaruSaru Daisakusen** (Japan),
disc ID **UCJS10060**, version **1.00**, using PSPRecomp.

**Early build: on macOS arm64 the game boots, renders and plays sound in a
window, but is not verified playable end to end. CI builds and tests the
launcher on Windows x86-64, Linux x86-64 and arm64, and macOS arm64. On Linux
arm64 the launcher installs the game and it starts; Windows is cross-compiled
with llvm-mingw and its game module builds, but neither Windows nor Linux
x86-64 has been played yet, and no release has been published.** The port is
distributed as a launcher that builds the game on the player's own machine
from their own disc (see [Releases](#releases-and-installing-the-game)). File-based settings and
keyboard/gamepad rebinding are available, as are 60 FPS frame interpolation at
the original game speed, a FOV override and window size/fullscreen settings.
A first launcher chooses the disc image and edits the main settings with a
gamepad, keyboard, mouse or touch screen, and the same settings and bindings
screens open over the paused game as a pause menu (Escape, or a gamepad's
Guide/Home button or L3+R3 together). Save management, timing-correct higher simulation rates
and translation are still to come. Native game cadence overrides are available as
an explicit experiment.
The complete project plan is in [`../../PLAN.md`](../../PLAN.md).

An optional English menu-screen pack uses the supplied English logo and covers
the intro, main menu, name entry, save/load dialogs and Options screen.
Gameplay and other screens retain Japanese. See [`docs/english-main-menu.md`](docs/english-main-menu.md)
for building the pack and selecting it with `--translation-pack FILE` or settings.

For external translation, `tools/extract_translation_labels.py export` creates
local UTF-8 CSV/JSON files with character, level and area context. Structured
labels and unresolved text candidates are separate. See
[`docs/translation-handoff.md`](docs/translation-handoff.md) for editing,
validation and the remaining context gaps.

An optional [English v1 pack](docs/english-v1.md) adds 193 machine-translated
gameplay labels. These are drafts awaiting human review; it also supports
importing the later human-edited label CSV.

Dialogue extraction is available with `tools/extract_dialogue.py export`:
71 in-game advice/dialogue messages become 177 page records, with message IDs,
scene topics, character mentions and protected controller tokens. See the
[dialogue handoff guide](docs/dialogue-translation-handoff.md). The optional
[English dialogue v1 pack](docs/english-dialogue-v1.md) now applies machine drafts
for all 177 pages through the compact-message lookup adapter. Speaker/level
verification and human review remain pending.

The English catalogs are not in this repository: they are translations of the
game's text, so they live in a separate one, [`sarusaru-translations`](https://github.com/jackgllghr/sarusaru-translations), which the
tools expect beside this checkout (or at `SARUSARU_TRANSLATIONS`). Players get
English as an [optional download](#english-game-text) from the launcher; the
port's packages never contain it.

## Requirements

- One of these hosts, with CMake 3.20+, Ninja and Python 3.10+:
  - **macOS arm64**: Xcode command-line tools (`xcode-select --install`) and
    `brew install cmake ninja python`.
  - **Linux x86-64 or arm64**: a C++20 compiler (GCC 13 or Clang 17),
    `make`, and SDL3's X11, Wayland and audio development packages. On
    Ubuntu 24.04 the list is in [`scripts/linux.Dockerfile`](scripts/linux.Dockerfile),
    which also builds an environment for compiling the Linux port from another
    system (`docker build -t sarusaru-linux -f profiles/sarusaru/scripts/linux.Dockerfile .`).
    A Vulkan driver is needed to play.
  - **Windows x86-64**: Visual Studio 2022 with the C++ workload, run from its
    _x64 Native Tools Command Prompt_ (which provides `cl`, `cmake` and `ninja`).
  - **Android arm64** (built from macOS or Linux): the Android SDK and NDK; see
    [`packaging/android/README.md`](packaging/android/README.md). The APK has
    the launcher only and cannot install the game yet.
- Your own legally obtained compatible ISO. Its name does not matter.

No commercial game files or generated game code are included. The profile uses
the ELF-formatted `PSP_GAME/SYSDIR/BOOT.BIN` present in the supported image;
it does not decrypt `EBOOT.BIN`. A modified executable is rejected by SHA-256.
This identifies the analyzed executable, not every asset on the disc: full asset
compatibility and release-grade image validation remain future work.

## Build and execute the diagnostic bootstrap

From the repository root:

```sh
python3 profiles/sarusaru/scripts/bootstrap.py \
  '../Saru! Get You! SaruSaru Daisakusen (Japan).iso'
```

The scripts work on every supported host and build into `out/<host>/`:
`macos-arm64`, `linux-x64`, `linux-arm64` or `windows-x64` (the examples below
use `macos-arm64`; on Windows the programs end in `.exe`).
The script validates/extracts the executable, builds/tests the framework,
analyzes imports and code, generates/compiles the AOT corpus, runs the tests
again, and executes startup with a 20-second timeout. Initial AOT compilation
uses `-O0` and two parallel jobs to limit compiler memory. `--jobs 1` lowers
memory pressure further. Source generation is repeatable; unchanged generated
units are not rewritten, allowing incremental native builds.

Exit **3** from the diagnostic host means execution stopped at incomplete
runtime functionality or the dispatch budget. It is **not** successful gameplay.
Exit 1 means an input, build or execution error. The bootstrap script preserves
the host's diagnostic exit code.

## Play in a window

Build and launch with one command (also prepares and generates game code on
the first run):

```sh
python3 profiles/sarusaru/scripts/build_and_run.py
```

The script uses the ISO path saved during preparation. On a fresh checkout, or
if the ISO has moved, pass `--iso '/path/to/game.iso'`. It builds incrementally
with two jobs; `--jobs 1` reduces memory pressure. Pass game options after `--`:

```sh
python3 profiles/sarusaru/scripts/build_and_run.py -- \
  --settings profiles/sarusaru/resources/settings.ini
```

The game runs until you quit, and its exit code is preserved. The script works
from any directory; explicit relative paths are resolved from your current
directory. By default this developer script uses
`profiles/sarusaru/resources/settings.ini`, so edits to that file apply to the
next launch. It prints the selected absolute settings path. An explicit
`--settings` option or `SARUSARU_SETTINGS` overrides that default; the explicit
option takes precedence over the environment variable. Directly launching the
executable still uses the normal per-user settings by default.

The game's generated code is a module, `sarusaru_game` (`.dylib`, `.so` or
`.dll`), that `sarusaru` and `sarusaru_bootstrap` load at run time. The
scripts build it beside them from `generated/` (`-DSARUSARU_GAME_MODULE=ON`).
Without one there, the executables load the one a launcher installed (below),
or `SARUSARU_GAME_MODULE=/path/to/module`.

After the bootstrap script has generated the game code once:

```sh
cmake --build out/macos-arm64 --target sarusaru
out/macos-arm64/bin/sarusaru profiles/sarusaru/game/BOOT.elf \
  '../Saru! Get You! SaruSaru Daisakusen (Japan).iso'
```

With only the ISO (`sarusaru game.iso`) the executable is read from the disc
image itself, after the same SHA-256 check; no prepared `BOOT.elf` is needed.

### Launcher

Started with no disc image, `sarusaru` opens the launcher in the game's
window: **Play**, **Settings** and **Quit** over the series'
sky, sunburst and yellow swoosh, with the title art. The focused control turns
yellow and wears Pipo's red siren lamp.

- Until a usable disc image is chosen, **Locate game disc** takes Play's place
  and opens the system file picker (dropping the file on the window works
  too). The image is checked before it is kept in `settings.ini`
  (`[game] disc`): an ISO9660 image whose `UMD_DATA.BIN` names UCJS10060 and
  whose `BOOT.BIN` is the supported executable. Another PSP game, a damaged
  image or a different executable gets a sentence saying so above the
  buttons, as does a kept image that has since moved or changed.
- **Settings** has Graphics, Controls and Interface tabs: full screen,
  window size (not while full screen), resolution, frame rate (Native, 30 or
  60 FPS motion), FOV, draw distance, cel shading, HD textures, the stick dead zone, the
  launcher's language, the game's text (below) and the confirm button. Values
  step with left/right, the row's arrows, or by activating it; each is saved
  at once.
- **Controls** (from Settings' Controls tab) lists every PSP control with
  its keyboard and gamepad inputs, drawn as keycaps and the pad's own glyphs.
  On a focused cell, confirm (or a click or tap) sets a new input, the north
  button or `+` adds another, and the west button or Backspace clears it
  (the mouse and touch get small + and x buttons in the cell). The next input
  is captured: a key, where Esc cancels (Esc itself is set in
  `settings.ini`); or a gamepad button, bound on release so even the back
  button can be bound, with a 1-second hold of it, or 6 seconds without a
  press, cancelling; or a stick or trigger in the direction pushed. A click
  or tap cancels either. An input shared with another action is pointed out
  (it is allowed), and each device can be reset to its defaults.
- **Play** starts the game in the same window. A game that cannot start (a
  moved image) returns to the launcher with the reason.

The game's stage HUD button strip and menu hint icons also follow the last
keyboard or gamepad used, with Xbox, Switch and PlayStation glyphs.
Interface → Controller Buttons defaults to Auto; Xbox, Switch, PlayStation
and Keyboard overrides can be selected there and take effect immediately. These
prompts show the first saved binding for each action (including triggers and
button chords); unbound actions show a dash. They work with the original
Japanese textures, translation packs and HD textures. Only source-verified
button regions are replaced; captions retain their selected language.

Settings' **Updates** tab shows the launcher version, checks published stable
GitHub releases in the background once a day, and opens release downloads.
Automatic checks can be disabled there. Android links to Obtainium for APK
updates. See [release/update setup](docs/auto-updates.md), including Android
signing requirements and the desktop installation phases still to come.

Every screen works with each device alone. The arrow keys, d-pad or left stick
move the focus; Enter or the confirm button activates; Esc, the other face
button, a mouse's back button or Android's Back go back; Q/W or L1/R1 switch
tabs. The mouse focuses what it hovers. A finger taps, dragging a list
scrolls it (without pressing the row it started on), and a Back pill appears
for the mouse and touch. The button hints along the bottom follow the device
last used: PlayStation shapes, Xbox or Nintendo letters (from SDL's labels
for the pad), or keyboard keys. Confirm is the south button (cross) by
default; `[interface] confirm = east` makes it circle, as the game itself
confirms in this Japanese release.
Layout scales with the window from a 960x544 design.

The launcher speaks **English and Japanese**. `[interface] language` is
`auto` (Japanese when the system's preferred language is, English otherwise),
`en` or `ja`, and the Interface tab switches it at once; each language is
named in itself there. All text is in `host/ui/texts.inc`, one line per text
with both languages, and the tests check both have every text with the same
`{0}` placeholders. The disc check reports reasons, not sentences, so they
are worded in the chosen language. Reasons the game itself gives for not
starting are still English, inside a translated sentence. The game's own text
is unchanged (Japanese).

The launcher draws with [Dear ImGui](https://github.com/ocornut/imgui) on the
shell's Vulkan context, in Luckiest Guy (titles, with M PLUS Rounded 1c Black
for Japanese) and M PLUS Rounded 1c (text, including Japanese file names); see [`THIRD_PARTY.md`](THIRD_PARTY.md). They
are downloaded at configure time, pinned and hash-checked, into
`bin/resources/` beside the executable with their licences.

### English game text

**Game text** on the Interface tab chooses the game's own language: Japanese
(the original) or English. The port carries no translation; the first time a
player chooses English, the launcher explains that it is an unofficial,
unreviewed machine translation, not made or endorsed by Sony, and downloads
the pack this build was configured with. The pack is kept only if its SHA-256
matches, in `<user data>/translations/english-<hash>.sstp`, and `[translation]`
in `settings.ini` then points at it; switching back and forth needs no second
download. A pack already set in `settings.ini` (one built locally) is used as
it is. The pack also carries the movies' English subtitles
([docs/movie-subtitles.md](docs/movie-subtitles.md)).

Builds offer the pack pinned in `profiles/sarusaru/CMakeLists.txt`
(`SARUSARU_ENGLISH_PACK_URL`, `_SHA256` and `_SIZE`); publishing a new pack
means updating those three defaults. A build can point at another pack:

```sh
cmake -S . -B out/macos-arm64 \
  -DSARUSARU_ENGLISH_PACK_URL=https://github.com/jackgllghr/sarusaru-translations/releases/download/<tag>/<pack>.sstp \
  -DSARUSARU_ENGLISH_PACK_SHA256=<sha256> -DSARUSARU_ENGLISH_PACK_SIZE=<bytes>
```

With `-DSARUSARU_ENGLISH_PACK_URL=` (empty) the launcher says English is not
offered by this build. An existing build directory keeps the values in its
CMake cache, so it needs reconfiguring with the new ones (or a fresh directory). The pack
is built from the catalogs and the maintainer's own disc (see the
[`sarusaru-translations`](https://github.com/jackgllghr/sarusaru-translations) README) and published there, apart from the port.

### HD textures

HD and 4K textures are upscaled on the player's device from their own disc;
no game pixels are distributed. **Textures** on the Graphics tab chooses
Original, HD (4×) or 4K (8×). The first time HD or 4K is chosen, the launcher
does three things:

- It downloads the texture recipe this build was configured with
  (`SARUSARU_TEXTURE_RECIPE_URL/SHA256/SIZE`) and the pinned Real-ESRGAN
  upscaler.
- It reads the listed textures from the player's disc and upscales them, with
  colour and alpha as separate planes.
- It writes the pack to `<user data>/textures/`.

Textures the recipe can't find on the disc are collected by the game as they
are drawn, and upscaled the next time the pack is chosen.
`tools/build_texture_recipe.py` builds a recipe from `--dump-textures DIR`
dumps and the maintainer's disc. `--texture-pack DIR` (or `none`) overrides
the pack for one run. See [`docs/hd-textures.md`](docs/hd-textures.md).

A build can instead offer a **prebuilt HD pack** for the menus, icons,
backgrounds and game textures, published apart from the port in
[`sarusaru-hd-textures`](https://github.com/jackgllghr/sarusaru-hd-textures) (`SARUSARU_HD_PACK_URL/SHA256/SIZE`, pinned to its current release). HD (4×)
then downloads that zip once, checks its SHA-256 and that its `pack.ini`
names the player's disc, and unpacks it to
`<user data>/textures/4x-prebuilt-<hash>/`; 4K (8×) is still made on the
device. `tools/package_hd_pack.py PACK_DIR -o pack.zip` makes the zip and
prints the three values:

```sh
cmake -S . -B out/macos-arm64 -DSARUSARU_HD_PACK_URL=https://github.com/jackgllghr/sarusaru-hd-textures/releases/download/<tag>/sarusaru-hd-4x.zip \
  -DSARUSARU_HD_PACK_SHA256=<sha256> -DSARUSARU_HD_PACK_SIZE=<bytes>
```

### Presets

**Preset**, first on the Graphics tab, sets several rows at once:

| Preset | Resolution | Textures | Game text | Draw distance | Intro logos |
| --- | --- | --- | --- | --- | --- |
| Native | 1× (480×272) | Original | Japanese | Native | Shown |
| Modern | 4× (1920×1088) | HD (4×) | English | 8× | Skipped |

Modern is the default. A new `settings.ini` starts with its resolution, draw
distance and logo skip, and the first **Play** opens the English and HD
texture download screens in turn (Back skips one) before the game starts;
`[game] modern_downloads` records that this has happened. Settings files from
earlier versions keep their values and are not offered the downloads.

Choosing Modern later opens the same screens for whatever is not downloaded
yet. The preset is read from those rows, not stored: changing any of them (or
skipping one of Modern's downloads) shows **Custom**. A download the build
does not offer (no HD pack or recipe, say) is left out of Modern. Other rows,
such as frame rate and FOV, are left alone.

`SARUSARU_UI_SCRIPT` drives the launcher (and, once the game runs, its pause
menu: `pad guide` or `padchord leftstick rightstick` opens it) without a person, for checks with
each device: keys through SDL, buttons through an SDL virtual gamepad (which
can claim to be a PlayStation, Xbox or Switch pad), mouse and touch input
straight to Dear ImGui, file drops, and `capture FILE` to save a BMP of the
next frame; `padhold` and `axis` hold buttons and push sticks for binding
capture. The commands are listed in `host/ui/ui_script.hpp`, e.g.:

```sh
SARUSARU_UI_SCRIPT='wait 1; pad dpdown; pad east; wait 0.5; capture /tmp/settings.bmp; quit' \
  out/macos-arm64/bin/sarusaru --settings /tmp/test-settings.ini
```

The launcher writes the settings file it was given, so point developer runs at
a scratch file rather than the committed `resources/settings.ini`.

`--scale N` overrides the settings file's internal resolution for this run
(1-8 times 480x272, default 2). Default keys: arrows d-pad, Z circle
(confirm in this Japanese release), X cross,
A square, S triangle, Q/W L/R, Enter start, Backspace select, I/J/K/L the
analog stick, Tab (held) fast-forward, Escape the pause menu. Gamepads map by
button position; Guide (Home), or L3 and R3 pressed together where the system
keeps the Guide button for itself, opens the pause menu.

### Pause menu

The menu action pauses the game under a menu over its last picture: Resume,
Settings (the launcher's settings and bindings screens) and Quit game. Changes
are saved as they are made and apply when the game resumes, including the
resolution, frame rate, FOV, cel shading, HD textures and bindings; the game
text and skipping the intro logos say they apply the next time the game
starts. Making HD textures and downloading English stay in the launcher. A
settings file from an earlier version keeps its `quit` key for the menu. Saves go to the same per-user memory stick as the bootstrap's.

### Settings and rebinding

When launched directly without a settings override, the shell creates
`settings.ini` on its first normal launch. On macOS it lives
in `~/Library/Application Support/SaruSaruRecomp/`; on Windows in
`%APPDATA%/SaruSaruRecomp/`; on Linux in
`$XDG_CONFIG_HOME/SaruSaruRecomp/` (or `~/.config/SaruSaruRecomp/`).
`--settings FILE` selects another file and takes precedence over
`SARUSARU_SETTINGS`. Missing files are created with defaults. Invalid existing
files stop startup with a file/line error and are left intact.

The full template is [`resources/settings.ini`](resources/settings.ini), also
printed by `sarusaru --print-default-settings`. Edit the file and restart:

```ini
[graphics]
internal_resolution = 3
window_width = 1920
window_height = 1080
fullscreen = 0
frame_rate = 60
fov_degrees = 50
draw_distance = 1
cel_shading = 0

[input]
dead_zone = 0.2

[keyboard]
circle = C, V
fast_forward = Tab

[gamepad]
circle = b
analog_left = axis:rightx-
analog_right = axis:rightx+
analog_up = axis:righty-
analog_down = axis:righty+
```

Omitted settings retain their defaults. Keyboard names are SDL **physical
scancode** names, independent of the active keyboard layout; `scancode:N`
also works for keys whose names contain a comma or `#`. Multiple bindings are
comma-separated; `none` unbinds an action. Two gamepad buttons held together
are written `a+b` (the pause menu's default `leftstick+rightstick`); while
such a chord is held, its buttons do not also trigger their own actions, so
L3+R3 does not reset the camera as R3 alone does. Chords are set in the file;
the bindings screen shows them but captures single inputs. Both PSP controls and the host's
`menu`/`fast_forward` actions can be rebound.

SDL's gamepad string names use `a/b/x/y` for south/east/west/north, `dpup`,
`dpdown`, `dpleft`, `dpright`, `leftshoulder`, `rightshoulder`, `start`, `back`,
and other SDL buttons. Axis names are `leftx`, `lefty`, `rightx`, `righty`,
`lefttrigger`, `righttrigger`; `+` and `-` select a direction. Swapping those
directions inverts an axis. Triggers can bind any action. Buttons activate
above half travel; stick axes have a rescaled dead zone, while triggers use
their unipolar range. Connected controllers contribute together, and normal
hot-plugging remains supported. One source may intentionally trigger several
actions. The launcher edits all of these, bindings included.

`frame_rate` sets how often pictures are shown. The game always updates at its
native cadence (two PSP vblanks per frame, about 29.97 Hz), so guest vblank
timing, input sampling, render submissions, audio and movie timing are never
changed. 0 (Native) presents at every PSP vblank (about 59.94 Hz); 1 to 59 is a
**presentation cap** (the launcher offers 30), which in fast-forward still
selects frames on guest time, so it is not a wall-clock cap.

`frame_rate = 60` gives **60 FPS motion at the original game speed**.
Movement, animation, scripts, audio and movies keep their original timing;
every frame the game draws is pixel-identical to native. Between two game
frames the renderer draws one more picture: each draw call is matched with the
same draw of the previous frame (by vertex source, texture and vertex type;
instances by nearness), and its world/view/projection matrices, CPU-transformed
vertices (skinning, 2D layout), normals, colours and texture scrolling are
blended halfway. Draws without a match, or that moved too far to be one motion
(cuts, recycled sprites), appear as in the newer frame. The in-between picture
is shown at the vblank where the new frame first appears and the new frame one
vblank later, so the picture is about 17 ms later than native. Full-motion
video plays at its own rate. Measured on the title screen and the stage hub,
about 99% of draws are matched.

Settings files from earlier versions still load: `frame_interpolation = 1`
becomes `frame_rate = 60`, and `experimental_game_frame_rate` is ignored (the
developer `sarusaru_bootstrap` still reads `SARUSARU_EXPERIMENTAL_GAME_FRAME_RATE`
for experiments).

For headless investigation, the bootstrap accepts the same experimental setting
through `SARUSARU_EXPERIMENTAL_GAME_FRAME_RATE` and reports the number of
overridden game frames and the chosen interval. The windowed shell uses the
settings file instead. Both paths verify the supported executable before the
game-specific hook is installed.

The launcher’s **Settings → Graphics → Cel shading** preset (or
`cel_shading = 1` under `[graphics]`) gives 3D shading four distinct brightness
bands before textures are applied. It defaults to Off, preserves texture detail
and alpha, and applies to interpolated frames too. Screen-space HUD, menus and
framebuffer clears retain their original shading. It
offers **Banded** (`cel_shading = 1`) and **Outlined** (`cel_shading = 2`).
Outlined adds black silhouettes around characters and the world. Characters
use expanded back faces; world geometry uses narrow contours along boundaries,
silhouettes and sharp creases, without diagonal wireframe edges on planar
surfaces. This also supports scenery without normals and single-sided meshes.
Width stays at one native pixel (two at 2× resolution), with depth-tested
occlusion. Additive effects, screen-space HUD and menus, and texture cutouts
are excluded. The outline pass preserves framebuffer alpha and depth and also
runs on interpolated frames.

`fov_degrees` sets the vertical FOV of the gameplay camera (0 for native, about
36.5 degrees, or 30-120). It applies only while a player-following gameplay
camera drives the frame: in stages and in the stage hub. The title and main
menu, 3D menus such as stage select, the warp tunnel, stage introductions and
other cutscenes keep the native FOV. A pause during gameplay keeps the wide
view behind the pause menu. While it applies, the game camera's own frustum
is widened by the same factor for every lens, so zooms keep their proportion,
and the game culls objects for the view it draws: nothing at the screen edges
is missing at a wider FOV. The HUD and 2D menus are unchanged. See
[`docs/settings-investigation.md`](docs/settings-investigation.md).

`draw_distance` keeps actors (enemies, monkeys, "?" markers and other
characters) drawn farther from the camera: 1 is native, up to 8. The game
fades each actor out between 250 and 350 units from the camera, while level
geometry is drawn to the far plane; beyond 200 units the setting multiplies
the distance at which actors fade, so at 4 they fade between 400 and 800
units. Enemies that stop animating when far away (the robots) also keep
moving to that distance. It applies wherever actors update, and changes take
effect from the pause menu at once.

**Settings → Interface → Skip intro logos** skips the opening logos automatically
(default Off). The equivalent setting is `skip_intro_logos = 1` under `[game]`.
Press the game's **Start** button during the logos to skip them for this launch
(Enter on the default keyboard bindings, or the controller's Start/Menu button).
The press is consumed until release, so holding Start does not also activate the
title screen. Logo resources still initialise and clean up normally; the title
movie, autosave notice, menus and later cutscenes keep their normal behavior.

`window_width`/`window_height` set the window's size at startup in points
(default 960x544, twice the PSP's screen); the window stays resizable and the
picture is letterboxed at the PSP's aspect ratio. `fullscreen = 1` opens
fullscreen at the desktop's resolution, and Alt+Enter or F11 toggles it while
playing. `internal_resolution` sets the rendering resolution independently.

For unattended checks, `SARUSARU_CAPTURE=<directory>` saves the picture as
BMP files every `SARUSARU_CAPTURE_EVERY` seconds of game time (default 5),
starting at `SARUSARU_CAPTURE_FROM` seconds if set,
`SARUSARU_INPUT` scripts the pad as for the bootstrap, and
`SARUSARU_TRACE_DRAWS=<seconds>` prints the draw calls of two vblanks.

### Framework and input tests without an ISO

```sh
cmake -S . -B out/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DPSPRECOMP_PROFILE=sarusaru -DSARUSARU_GAME_MODULE=OFF
cmake --build out/macos-arm64 --parallel 2
ctest --test-dir out/macos-arm64 --output-on-failure
```

All public fixtures are invented data. The import regression suite generates
and natively executes a synthetic ELF to verify same-unit and cross-unit calls
reach registered PSP imports, with correct delay-slot and return-address state.
It currently runs on native non-MSVC builds, not cross-compilation builds.

## Releases and installing the game

Players get the launcher, not the game: the packages hold no game files and no
game code. The first time they choose **Play** with a supported disc, the
launcher's **Install the Game** screen builds the game module on their machine
from their own disc, with a progress bar and Cancel:

1. the executable is read from the verified disc (the same SHA-256 check);
2. the bundled code generator (`psp_recomp`) translates it to C++;
3. the platform's compiler builds the units in parallel (one job per 1.5 GiB of
   memory, up to one per core) and links `sarusaru_game` against the bundled
   runtime (`sdk/`, `lib/libpsprecomp_runtime`);
4. the module is checked to load and replaces any earlier one in
   `<user data>/game/<version hash>/`, with `install.log` beside it.

| Platform    | Compiler                                                                        | Install time measured                               |
| ----------- | ------------------------------------------------------------------------------- | --------------------------------------------------- |
| macOS arm64 | Xcode's clang (the Command Line Tools; the launcher asks macOS to install them) | about 3½ minutes, M-series, 10 jobs                 |
| Linux       | the system's `g++` or `clang++` (`$CXX` first)                                  | about 5½ minutes, 5 jobs in an 8 GB arm64 container |
| Windows x64 | llvm-mingw 20260922, downloaded once (190 MB, hash-checked)                     | not measured yet                                    |
| Android     | the clang and lld the APK ships, built from source ([`packaging/android`](packaging/android/README.md)) | about 8 minutes, OnePlus 13, 8 jobs |

`<user data>` is `~/Library/Application Support/SaruSaruRecomp` on macOS,
`$XDG_DATA_HOME/SaruSaruRecomp` (or `~/.local/share/SaruSaruRecomp`) on Linux
and `%APPDATA%\SaruSaruRecomp` on Windows. The version hash covers the
runtime headers, the code generator and the supported game, so an update that
changes any of them asks for a new install; others keep the installed module.
`sarusaru --install game.iso` does the same without a window.

The module must match the port's C++ ABI, so the Windows release is built with
the same llvm-mingw (`cmake/llvm-mingw.toolchain.cmake`, fetched with
`cmake -DHOST=windows -DDEST=<dir> -P profiles/sarusaru/cmake/fetch_llvm_mingw.cmake`);
an MSVC build of the port cannot install the game and needs
`-DSARUSARU_GAME_MODULE=ON`. The same toolchain cross-compiles the Windows
port from Linux (`-DHOST=linux-x64` or `linux-arm64`, with `glslang` installed
for the shaders).

`scripts/package.py` packages a build: a `.app` in a `.zip` on macOS (not
notarised: right-click and Open the first time), a `.zip` on Windows and a
`.tar.gz` on Linux, each with a player README and the licences; on Linux
`--format appimage` also makes an AppImage (with a pinned `appimagetool`, run
without FUSE). `packaging/linux/build_flatpak.sh <version> <out>` builds the
Flatpak (`io.github.sarusaru_recomp.SaruSaruRecomp`) from source with
`flatpak-builder` and bundles it as a `.flatpak`. Its runtime is the
Freedesktop SDK rather than the Platform, because the launcher compiles the game
inside the sandbox with the SDK's `g++`; the game installs to
`~/.var/app/io.github.sarusaru_recomp.SaruSaruRecomp/data/SaruSaruRecomp`, and
the disc image is chosen through the desktop's file chooser portal. It refuses to
package anything game-derived, any translation pack or catalog (English is a
separate download) or any binary that names the build directory.
Pushing a `v*` tag runs `.github/workflows/release.yml`, which builds, tests
and packages every host (Linux on Ubuntu 22.04 for older glibc, macOS for 13.0
and later) and attaches the packages and `SHA256SUMS.txt` to a **draft**
release to review and publish by hand.

## Local files (ignored by Git)

| Path                                   | Contents                                                   |
| -------------------------------------- | ---------------------------------------------------------- |
| `game/BOOT.elf`                        | Executable extracted from your own ISO                     |
| `game/input.json`                      | Source ISO path, disc metadata and file inventory          |
| `analysis/main.json`, `analysis/*.csv` | Analysis, imports and candidate functions                  |
| `analysis/bootstrap.log`               | Startup result from the build script                       |
| `generated/`                           | Game-derived C++ corpus; never commit or publish as source |
| `progress/`                            | Local lock and scratch work                                |
| `../../out/<host>/`                    | Native tools and diagnostic binary                         |

The ISO is opened read-only. Preparation stages inputs before publishing them
and refuses to overwrite an existing different executable or symlink target.
The inventory is read-only analysis; archive-derived paths are not extracted.

## Verified initial analysis

- Main module is a relocatable PSP PRX; all 69,286 relocations processed with
  zero invalid or unsupported relocations.
- 219 imported functions across 27 libraries.
- Automatic generation produces 163 source units and 219 import wrappers.
- Static analysis is conservative and includes candidate data/code overlap;
  its discovered-function and unsupported-instruction counts are not proof of
  correct code coverage. Runtime traces must guide further work.
- Two encrypted media-module candidates are present. Their runtime loading
  behavior, and any game code overlays, have not been established.
- Native Mach-O arm64 bootstrap compiles and executes startup through the
  explicitly modeled SDK/compiler configuration, early thread calls and the
  first partition allocation (`UserSbrk`, type 3, 0x01300000 bytes, aligned to
  0x1000 at 0x08AAA000). PSP control-block initialization (`$k0`) and bootstrap
  stack reservation have been verified; they resolve the earlier guest fault.
  Interrupt-mask state, thread sleep/wakeup (`0x9ACE131E`/`0xD59EAD2F`, per
  the public PSPSDK stubs) and UMD callback registration are modeled. Startup
  passes `sceUmdUser` drive activation, controller, display, thread and
  synchronization setup, starts five game threads, submits its first GE display
  lists, reads `disc0:/PSP_GAME/USRDIR/data/common.lzs`, then
  initialises its sound system, auto-loads its save (none on a fresh memory
  stick), streams its first ATRAC3plus music track and draws for 57 s of
  guest time (1,719 GE lists), loads its media modules and loops its
  title-screen movie. With `SARUSARU_INPUT` presses it loads stage 1-1 and
  runs there for hours of guest time without an unmodeled import.
  `SARUSARU_INPUT="65:START;65.2:-;66:CIRCLE;66.2:-"` scripts the pad
  (seconds of guest time, buttons joined with `+`, `-` releases). `PSPRECOMP_HLE_HISTOGRAM=1` lists the most-called imports;
  `SARUSARU_TRACE_MOVIE=1` traces the movie player. `SARUSARU_DISPATCH_LIMIT` raises the 100,000-dispatch budget; `SARUSARU_TRACE_SCHED=1` logs
  every thread switch and `SARUSARU_TRACE_IO=1` every file operation.
- The bootstrap takes the ISO as a second argument and reads it in place. The
  disc must name the supported disc in `UMD_DATA.BIN`, and its `BOOT.BIN`
  must be byte-identical to the prepared executable.
- All thirty-six CTest suites pass, including 17 ISO/preparation cases and seven
  generated-native-code cases. Repeat generation rewrites zero source units.

## Framework changes

The pinned upstream is recorded in `config/supported_game.json`.

- Fixed automatic generation looping indefinitely while emitting a cross-unit
  `JAL` to an import. Imports now take precedence over same-unit generated
  labels as well, preserving HLE dispatch rather than executing stub code.
- Added byte-buffer SHA-256 to the existing framework helper so the host hashes
  the exact executable bytes it loads, not a second filesystem read.
- Added the narrow `StartupHle` module for the observed SDK/compiler configuration
  imports. It records the guest-provided values and returns documented success;
  all other unmodeled imports still stop execution.
- Added a self-contained early kernel module for validated `sceKernelCreateThread`
  and `sceKernelStartThread` construction. It performs bounded name reads,
  validates entry/priority/stack inputs, owns aligned guest stacks and switches
  to higher-priority initial threads. It schedules the next ready thread on
  exit, but deliberately stops only when none remain; wait/delay scheduling is
  not implemented yet.
- Extended the early kernel with deterministic ready-thread handoff, thread
  control-block initialization, bounded partition allocation/free, callback and
  exit-callback creation/registration, power-callback slot registration, and
  interrupt-mask state, sleep/wakeup with priority preemption and UMD callback
  registration. A thread blocked inside an import resumes at its return address
  (previously a preempted `sceKernelStartThread` caller would have re-entered
  the import stub). If every thread sleeps, execution stops explicitly. It does
  **not** deliver callbacks, power/UMD events or guest interrupts.
- Added a read-only ISO9660 disc reader with the same structural limits as
  `prepare_game.py` (case-insensitive lookup, no `.`/`..` resolution, extents
  bounded by the volume), and a UMD drive model: medium present, synchronous
  activate/deactivate of `disc0:`, drive/error status. A drive-status wait that
  can never be satisfied stops execution. Eject/swap are not modeled.
- Added `sceCtrl` sampling mode, sampling cycle and idle-cancel threshold as
  validated state.
- Added a deterministic virtual guest clock to the early kernel. Guest code
  runs in zero guest time; when no thread is ready the clock jumps to the
  earliest timed wait. Vblank occurs at 60000/1001 Hz. `sceKernelDelayThread`
  and `sceKernelDelayThreadCB` (no callbacks are generated yet) are timed
  waits. `sceCtrlReadBufferPositive` returns the samples taken since the last
  read, or blocks until the next one; the pad is neutral until host input
  exists. A thread that never blocks never sees time pass.
- Added `sceDisplay` state on the virtual clock: mode (480x272 only),
  framebuffer address/width/format with sync-0 immediate and sync-1
  next-vblank latching, vcount, accumulated hcount (286 lines per frame),
  `IsVblank` (a 731 us blanking interval) and the vblank waits. Nothing is
  presented yet.
- Added thread lifecycle to the early kernel: exit (also by returning from
  the entry point), exit-and-delete, delete, terminate, exit status, wait for
  thread end, current thread ID/priority and the VFPU attribute, and system
  time. An ended thread is dormant and can be restarted.
- Added generic object waits: optional timeouts on the virtual clock (zeroed
  on timeout, time left written back on release), release by another thread
  with priority preemption, and a stop message naming every blocked thread
  when nothing can run.
- Added semaphores and event flags (`kernel/sync.cpp`): FIFO or priority wait
  queues, AND/OR waits with clear-all/clear-pattern, single or multiple
  waiters, overflow and count validation, and `WAIT_DELETE` on deletion.
- Added `sceKernelSuspendDispatchThread`/`ResumeDispatchThread`: while
  suspended nothing preempts the current thread and its waits fail with
  `CAN_NOT_WAIT`; resuming lets a thread made ready meanwhile preempt. Both
  fail while CPU interrupts are masked.
- Added interrupt delivery: queued guest handlers run on a priority-0
  interrupt pseudo-thread with its own stack and return through a kernel stub;
  waits from interrupt context fail with `CAN_NOT_WAIT`. `InterruptManager`
  registers, enables and releases sub-interrupt handlers; enabled vblank
  handlers run as the virtual clock crosses each vblank (only while
  interrupts are enabled), before threads released by that vblank. If only
  vblanks remain, 600 consecutive idle vblanks without a runnable thread
  stop execution.
- Added a headless GE (`ge/ge.cpp`): EDRAM address, callback registration,
  list enqueue (tail and head), stall-address updates, list and draw sync.
  Lists run in zero guest time; jump/call/return with BASE, OFFSET_ADDR and
  ORIGIN addressing are followed exactly; FINISH and SIGNAL (handler suspend
  and continue) raise GE interrupts, and a suspending signal pauses the list
  until its handler returns. Every other command's latest value is kept as GE
  state. Nothing is rasterised; bounding-box tests count as visible. Other
  signal behaviours, a bare END and runaway lists stop execution.
- Added `IoFileMgrForUser` file I/O (`io/io_manager.cpp`) over the disc
  image: open/read/seek/close, stat (`st_private[0]` holds the start sector),
  directory listing with `.` and `..`, `sce_lbn0x<sector>_size0x<bytes>`
  extents, and the async open/read/close with wait/poll. Async operations
  complete when issued; a failed async open still returns a descriptor
  carrying the error. Writes to stdout/stderr go to the host log.
  `sceIoDevctl` reports a memory stick inserted (`ms0:`/`fatms0:` command
  `0x02425823`); other devctl commands stop execution. Write
  access to the disc is refused; other devices and relative paths stop
  execution. Disc timestamps are not yet reported. The ISO9660 file ioctls
  for start sector (`0x01020006`) and size (`0x01020007`) are modeled; other
  ioctl commands stop execution.
- Added `sceRtcGetCurrentClockLocalTime`: host local time sampled once at
  boot plus virtual guest time, so the clock advances exactly with guest
  time. `SARUSARU_RTC_EPOCH` (Unix seconds) pins the boot time.
- Added thread callback delivery: a notified callback runs on its owning
  thread as callback(count, argument, common), below the interrupted frame,
  from `sceKernelCheckCallback` (returns 1 if any ran) or while the thread
  is in a `*CB` wait (`DelayThreadCB`, `WaitVblankStartCB`, sleep with
  callbacks), after which the wait resumes with its original deadline. A
  nonzero return deletes the callback. `sceUmdActivate`/`Deactivate` notify
  the UMD callback with the new drive status.
- Added headless `sceAudio` output (`audio/audio.cpp`): eight channels
  allocated from 7 downwards plus the Output2 channel, reserve/release,
  data length, format, volume and remaining length. A block plays for
  sample_count / 44100 s of virtual time and a channel accepts a block while
  at most two are in flight, so blocking output paces the audio thread.
  Samples are counted, not played; blocking output returns the sample count.
- Added `sceSasCore` (`sas/sas.cpp`), all 25 imports: VAG ADPCM decoding
  (five predictor filters, loop-start/end flags), noise voices, pitch
  resampling with linear interpolation (0x1000 = 1.0), ADSR envelopes
  including the packed `SetSimpleADSR` form, per-voice volumes, pause, end
  and envelope queries, and grain mixing for `__sceSasCore` and
  `__sceSasCoreWithMix`. The envelope curves follow the published reverse-
  engineered descriptions and are not hardware-verified. The multichannel
  output mode and the noise spectrum are not modeled.
- Applied SAS reverb (`sas::Reverb`): the voices' effect sends run through
  the PlayStation SPU reverb at 22050 Hz, written from nocash's psx-spx
  formula and preset table (PSP types 0-8: room, studio small/medium/large,
  hall, space, echo, delay and pipe; type 8 was wrongly rejected before).
  Echo and delay compute their taps and feedback from `sceSasRevParam`,
  which at delay 255 reproduces the PS1 table. `sceSasRevVON` gates the dry
  and wet outputs, `sceSasRevEVOL` scales the wet one, and `__sceSasInit`
  resets them. The game selects Hall (type 4) with both outputs on and the
  effect at half volume. Synthetic tests cover the echo registers, an
  impulse through Delay and the dry/wet gates; the result has not been
  compared against a PSP.
- Added `scePsmfPlayer` (`movie/psmf_player.cpp`) and `sceMpegInit/Finish`:
  Create/Delete, SetPsmf/ReleasePsmf (the movie is read from the disc),
  GetPsmfInfo, ConfigPlayer (loop, pixel type), Start/Stop/Update,
  GetCurrentStatus, GetVideoData (the latest picture whose time has come on
  the virtual clock, converted to 565/5551/4444/8888 at the game's stride)
  and GetAudioData (the next ATRAC3plus frame, 2048 stereo samples).
  Pictures are assumed to leave the decoder in presentation order at
  29.97 Hz; only start position 0 is modeled. Demuxing and H.264 decoding
  come from Yakumo (see `THIRD_PARTY.md`).
- Moved game setup into `GameSession` (`session.*`), shared by the headless
  bootstrap and the coming windowed shell. The kernel gained a host frame
  observer called at every vblank the virtual clock crosses, and `sceAudio`
  an optional sink receiving each queued block, for the shell's presentation
  and audio output; unset, behaviour is unchanged.
- Generated game code is now compiled at `-O1` (`SARUSARU_AOT_OPTIMIZATION`):
  at `-O0` its stack frames overflowed the native stack once AOT call
  chaining nested guest calls. `SARUSARU_CHAINING=1` runs the bootstrap in
  the chained mode the shell uses: about 28 times real time into stage 1.
- Adopted Yakumo's GE state machine and texture decoding (`gpu/`; see
  `THIRD_PARTY.md`). The GE walker keeps control flow, stalls and interrupts
  and hands every other command to `GeState`, which decodes draws for a
  renderer. GE block transfers (`TRXKICK`), previously ignored, are now
  copied in guest memory. The diagnostic summary reports decoded draws,
  vertices and unhandled commands.
- Added the host shell's dependencies (`cmake/Graphics.cmake`): SDL3 3.4.16,
  Vulkan-Headers, glslang 16.5.0 and, on macOS, MoltenVK 1.4.2, all pinned and
  hash-checked (see `THIRD_PARTY.md`). Vulkan is never linked; functions are
  loaded through SDL (`shell/vulkan_loader.*`). `sarusaru_vulkan_probe`
  lists the GPUs Vulkan sees. `-DSARUSARU_RENDERER=OFF` builds headless only.
- Added the windowed shell, `sarusaru` (`shell/`): an SDL window and Vulkan
  swapchain (`vulkan_context.*`), a GE renderer (`ge_renderer.*`) for the
  draws GeState decodes, using the adopted GE shaders compiled to SPIR-V at
  build time and embedded; audio through one SDL stream per `sceAudio`
  channel (`audio_output.*`); and keyboard and gamepad input (`input.*`).
  The kernel's frame observer drives it: at every guest vblank the shell
  polls events, presents the displayed framebuffer and sleeps until real
  time catches up with guest time. Findings while bringing it up: the
  game's CULL bit keeps the faces Vulkan sees as clockwise; its fog end
  and scale registers hold infinity and NaN, taken as +/-65535 (as
  PPSSPP does); the adopted fragment shader now honours the texture's
  colour-component setting (TCC) and the `add` texture function, and the
  GE's alpha-test codes 0 (never) and 1 (always) are swapped for it.
- SAS voices loop only at a repeating VAG end (flags 3); a plain end (1)
  stops even a looping voice. The game keys its one-shot effects on looping
  voices, so they repeated forever and exhausted its sound slots.
- Savedata LIST (mode 11): the game's saves whose names match a wildcard
  pattern, with their timestamps, written to the caller's ID list. The game
  lists `0*` in play and retried every frame while the mode was missing.
- The game's `stdout`/`stderr` is converted from Shift-JIS to UTF-8
  (`text/shift_jis.*`, through iconv, or code page 932 on Windows; bytes
  that are not valid Shift-JIS print as `\xNN`).
- Added scripted controller input for unattended runs: timed pad states
  that controller samples follow at their own virtual timestamps.
- `sceKernelUnloadModule` returns the module ID on success, as on the PSP;
  the game treats 0 as failure and retried forever when it returned 0.
- Added `sceKernelPowerTick` (counted; there are no idle timers to reset).
- Added `ModuleMgrForUser` (`modules/module_manager.cpp`): `psmf.prx` and
  `libpsmfplayer.prx`, Sony's media libraries whose exports the game imports
  and this profile provides by HLE, load and start as module IDs without
  code; any other module stops execution naming its path. Module ID queries
  report the main module; `sceKernelStopUnloadSelfModuleWithStatus` ends the
  run as a game exit.
- Added `sceAtrac3plus` (`audio/atrac.cpp`) on FFmpeg: GetAtracID/SetData/
  Release/Reinit, DecodeData with sample-accurate encoder delay, loop and end
  handling, GetRemainFrame, GetNextSample, SetLoopNum, and streaming through
  GetStreamDataInfo/AddStreamData. A streamed track's guest buffer is a
  staging area (refills always go to its start, where the PSP uses varying
  positions); supplied frames move to a host queue, and supply wraps from the
  loop-end frame to the loop-start frame. After a loop jump the decoder
  restarts without warm-up. Running out of streamed data is reported as
  `BUFFER_IS_EMPTY`. `SARUSARU_TRACE_ATRAC=1` traces it. FFmpeg 7.1.5 is
  built at configure time from a pinned, hash-checked source (see
  `THIRD_PARTY.md`).
- Added the savedata utility (adapted from Yakumo's MIT save-data code; see
  `THIRD_PARTY.md`): InitStart/GetStatus/Update/ShutdownStart with the
  INIT→VISIBLE→QUIT→FINISHED lifecycle, and AUTOLOAD/LOAD, AUTOSAVE/SAVE,
  LIST*, DELETE* and SIZES on PSP-layout folders under a per-user memory
  stick (`~/Library/Application Support/SaruSaruRecomp/memstick` on macOS;
  `SARUSARU_MEMSTICK` overrides). Saves are encrypted and signed as on a PSP
  with the public keys, in the mode the game's SDK selects (mode 3 here).
  List modes pick the first suitable entry; no dialog is drawn. Mode 3 has
  not yet been verified against a save made by a PSP. A save made outside
  the recomp (mode 5, flags 0x41, console hash filled with 01h, apparently
  from PPSSPP) auto-loads: the data file decrypts, its SAVEDATA_FILE_LIST
  hash matches, and re-signing its PARAM.SFO reproduces the mode 1 and
  mode 5 hashes byte for byte.
- Added `sceUtilityLoadModule`/`UnloadModule` for the PSPSDK utility module
  IDs. The libraries they load are provided by HLE at their import interface,
  so this records state only, with bad-ID, already-loaded and not-loaded
  errors.
- Lowered the VFPU integer packers `vi2uc`, `vi2c`, `vi2us` and `vi2s`, the
  random-number instructions `vrnds`, `vrndi`, `vrndf1` and `vrndf2`, and
  `vcrs.t` in the generator. The hardware random generator is undocumented;
  a seeded xorshift32 kept in VFPU control register RCX0 (saved per thread,
  as on the PSP) stands in. No unlowered instruction remains in the code
  region; the rest lie in data the analysis misreads as code.
- Function discovery now seeds code pointers from paired HI16/LO16
  relocations (each address-forming LO16 site with the preceding HI16 for the
  same segments, read after relocation). It finds pointers whose upper half
  sits in a callee-saved register far from where the lower half is added;
  here, 49 entries including a thread entry point that constant propagation
  missed. Covered by a synthetic PRX test.
- Mapped the PSP's 16 KiB scratchpad RAM (`0x00010000`) in the framework's
  `GuestMemory`, which previously faulted on it. Accesses use the checked
  slow paths; the main-RAM fast path is unchanged.
- Recorded `sceKernelSetGPO`, and bound the data-cache writeback calls as
  no-ops: host memory is coherent and nothing yet reads guest memory behind
  the CPU's back.
- Preserved checked VFPU scalar access for invalid register encodings instead
  of emitting uncompilable compile-time template operands.
- Used normal runtime dispatch for direct targets outside the generated entry
  map instead of referencing nonexistent native units. Missing code is reported
  explicitly, not replaced with a successful return.

These are game-neutral fixes; game identity and bootstrap choices stay here.

## Next work

The acceptance run with real input (new game, a level, save and reload),
then writing GPU results back to guest memory, the GE registers GeState does
not model yet, the rest of the launcher (save management, Android
document picking) and translation. For releases: the first run of the release
workflow, installing and playing on Windows and Linux x86-64, macOS signing and
notarisation, and a Windows build without a console window. See
[`../../PLAN.md`](../../PLAN.md) and [`YAKUMO_REUSE.md`](YAKUMO_REUSE.md).

Releases hold the launcher only; the game module is built by each player from
their own disc and never distributed. The public release policy for those
packages is still to be decided before the first release is published. No
permission to redistribute the original game is granted by this project's
software licence.
