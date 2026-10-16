# Saru Get You: Saru Saru Daisakusen Recompilation

![Saru Get You: Saru Saru Daisakusen](include/image/SaruSaruBigMission.png)

This is a recompilation project of **Saru Get You: Saru Saru Daisakusen**,
with the aim of making the game available on Windows, macOS, Linux, and Android
with improved performance and configurability, and providing a foundation for
translating the game.

See the [project plan](PLAN.md) and the
[profile instructions](profiles/sarusaru/README.md).

## What's working

| Feature                                                                               | Status          |
| ------------------------------------------------------------------------------------- | --------------- |
| Windows(x86), macOS(arm64), Linux(x86, arm64) and Android                             | ✅ Working      |
| Resolution scaling                                                                    | ✅ Working      |
| FOV adjustment                                                                        | ✅ Working      |
| Translation-ready UI: most menus, dialogue, stage and character names, control hints  | ✅ Working      |
| Subtitles for FMVs                                                                    | ✅ Working      |
| Right analog stick camera controls                                                    | ✅ Working      |
| Controller-specific prompts (Switch, Xbox, PlayStation, keyboard)                     | ✅ Working      |
| Intro and cutscene skip                                                               | ✅ Working      |
| PSP save compatibility                                                                | ✅ Working      |
| 60 FPS (interpolated)                                                                 | 🧪 Experimental |
| Texture packs ([HD texture pack](https://github.com/jackgllghr/sarusaru-hd-textures)) | 🧪 Experimental |
| Save states                                                                           | 🧪 Experimental |
| English machine translation of most menus and dialogue                                | 🧪 Experimental |

> [!TIP]
> Open the launcher in game with **L3 + R3** on a controller, **Esc** on a
> keyboard, or **Back** on Android.

Releases contain only the launcher: the first time you press **Play**, it
builds the game on your machine from your own disc. No game files or game code
are distributed. An unofficial English machine translation of the game's text
is an optional download from the launcher's settings, published separately in
[sarusaru-translations](https://github.com/jackgllghr/sarusaru-translations).

## Roadmap

What's still to come. See [PLAN.md](PLAN.md) for detailed progress and
acceptance criteria.

- **Gameplay accuracy:** a full play-through check (new game, a level, save,
  restart and reload), plus the missing GPU features, utility dialogs and audio
  effects.
- **Saves:** saves checked against PPSSPP and real PSP hardware, plus import,
  export and backups in the launcher.
- **60 FPS:** check the interpolation (and FOV) in every kind of stage,
  cutscene and dialog.
- **HD textures:** extend the HD pack to cover a full play-through.
- **Translation:** publish the first English pack in
  [sarusaru-translations](https://github.com/jackgllghr/sarusaru-translations)
  and extend it beyond the most common menus and dialogue.
- **Android:** touch controls, lifecycle handling and support for phones that
  can't build the game themselves.
- **Releases:** macOS signing and notarisation, a Windows build without a
  console window, and installs from release packages checked on Windows and
  Linux x64.

## PSPRecomp framework

The project is built on PSPRecomp. The upstream framework documentation follows below.

PSPRecomp is a static recompilation framework for PSP software. It reads an Allegrex/MIPS executable, analyzes guest code, emits C++ translation units, and runs them through a native host runtime. An opt-in interpreter supports code outside the compiled corpus; normal gameplay uses the compiled functions.

The [PPSSPP save-state foundations](profiles/sarusaru/docs/ppsspp-save-states.md)
include inspection and repacking tools. Native quick states can be saved from
the launcher's **Saves** screen and its in-game counterpart, with five thumbnail
slots. Select + R1 saves and
Select + L1 loads; keyboard defaults are F5 and F8. PPSSPP gameplay-state
interchange remains unfinished.

The repository is split between the reusable framework and the game-specific
SaruSaru profile (`profiles/sarusaru`).

## Repository layout

```text
include/psprecomp/   Public runtime and Allegrex interfaces
src/                 ELF/PRX loading, decoder, memory, runtime and support code
tools/               Generic analyzer, recompiler and reverse-engineering helpers
tests/               Framework regression tests
configs/             Generic examples and PSP NID data
profiles/            Game-specific hosts, generated code, configuration and tests
  sarusaru/          Saru Get You: Saru Saru Daisakusen profile
```

Game-specific addresses, HLE behavior, native fast paths, renderer integration and generated AOT code belong under a profile. The framework should remain usable without any profile selected.

## Requirements

- CMake 3.20 or newer and Ninja
- A C compiler for the profile's bundled Zstandard codec, and a C++20 compiler
  (Apple clang, GCC 13, Clang 17 or Visual Studio 2022)
- Python 3.10 or newer for the profile's scripts

See the [profile requirements](profiles/sarusaru/README.md#requirements) for
each host's packages.

## Build the framework only

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE=""
cmake --build out/framework --config Release
ctest --test-dir out/framework -C Release --output-on-failure
```

This builds `psprecomp_core`, `psp_analyze`, `psp_recomp`, `dump_function` and the framework tests.

## Build a profile

Profiles are selected with `PSPRECOMP_PROFILE`:

```bash
cmake -S . -B out/sarusaru -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPSPRECOMP_PROFILE=sarusaru -DSARUSARU_GAME_MODULE=OFF
cmake --build out/sarusaru
```

This builds the launcher and tests without game files. To build and play the
game from your own disc, use the profile's scripts:

```bash
python3 profiles/sarusaru/scripts/build_and_run.py --iso '/path/to/game.iso'
```

## Create another profile

See [`docs/PROFILE_GUIDE.md`](docs/PROFILE_GUIDE.md). A new title normally provides its own generated corpus, HLE/profile host, configuration, tests and optional native fast paths without modifying the framework for game-specific addresses.

## Game files

No commercial game executable or asset is included. PSPRecomp does not ship an EBOOT decryption implementation. Profiles expect files obtained from the user's own copy in the format documented by that profile.

## Source provenance

See [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) for the project rules around independently written code, profile boundaries, decryption and third-party source.

## Third-party code

The framework is MIT licensed. Individual profiles may include separately licensed dependencies or assets; their notices stay beside those files. The SaruSaru profile lists its bundled dependencies in `profiles/sarusaru/THIRD_PARTY.md`.

## License

PSPRecomp framework code is distributed under the MIT License. See [`LICENSE`](LICENSE).

## Credits

- [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) — the recompilation framework this project builds on.
- [SDL3](https://www.libsdl.org/) — windowing, input and audio output.
- [FFmpeg](https://ffmpeg.org/) — music and movie decoding.
- [Vulkan](https://www.vulkan.org/) and [MoltenVK](https://github.com/KhronosGroup/MoltenVK) — rendering.
- [tiny-AES-c](https://github.com/kokke/tiny-AES-c) — the installer and PSP save-data support.
- [Dear ImGui](https://github.com/ocornut/imgui) — the launcher's interface.
- [Luckiest Guy](https://fonts.google.com/specimen/Luckiest+Guy) and
  [M PLUS Rounded 1c](https://github.com/coz-m/MPLUS_FONTS) — the launcher's fonts.
- [TeamGDB](https://github.com/TeamGDB) — their Yakumo project served as inspiration.

## Legal disclaimer

This is an unofficial, independent project and is not affiliated with, endorsed
by, or sponsored by Sony Interactive Entertainment.

Saru Get You (サルゲッチュ / Ape Escape), the franchise to which **Saru Get You:
Saru Saru Daisakusen** belongs, is a trademark of **Sony Interactive Entertainment
Inc.** See the [official PlayStation trademark attribution](https://www.playstation.com/ja-jp/games/ape-escape/).
The original game and its artwork remain the property of their respective
rights holders. All other trademarks belong to their respective owners.

Users must supply their own legally obtained copy of the game. The project's
source-code license does not grant any rights to the original game, its assets
or its trademarks.
