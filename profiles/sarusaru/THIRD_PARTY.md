# Third-party material in the SaruSaru profile

## Zstandard (BSD-3-Clause)

The PPSSPP state-container codec statically links Zstandard 1.5.7, downloaded
and hash-checked by `cmake/SaveState.cmake`. Its BSD license is copied beside
the built tools as `bin/resources/Zstandard-LICENSE.txt`.

PPSSPP 1.20.4 is a wire-format reference for `host/save_state/`; no PPSSPP
source is included or linked. Snappy decoding is independently implemented
from [Google's raw-format specification](https://github.com/google/snappy/blob/main/format_description.txt).

## Yakumo save-data code (MIT)

Adapted from [Yakumo](https://github.com/TeamGDB/Yakumo) at revision
`39ac05f1a166703430d18826bc5877f4feb6b511` (`profiles/mhp3rd/host/save_data`,
`host/hle/utility_dialog.hpp`, `host/hle/hle_savedata.cpp` and
`tests/savedata_tests.cpp`):

- `host/save_data/` (AES-128 wrapper, PARAM.SFO, save-data crypto, PSP-layout
  store): namespace renamed; the store's crypto mode became a parameter
  (Yakumo's game uses mode 5, this one mode 3); a Yakumo path helper replaced.
- `host/utility/dialog.hpp` and `host/utility/savedata_dialog.*`: rewritten
  against this profile's runtime, without Yakumo's save import/transfer
  feature; the crypto mode follows the game's compiled SDK version.
- `tests/savedata_tests.cpp`: trimmed to the AES/CMAC vectors, PARAM.SFO,
  encryption and store tests, plus a mode 3 store check.

The save-data crypto embeds the publicly circulated KIRK and save-data keys
that the PSP uses for save encryption.

```
MIT License

Copyright (c) 2026 PSPRecomp contributors
Copyright (c) 2026 Yakumo contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Yakumo ATRAC and FFmpeg build code (MIT)

Also adapted from Yakumo at the same revision:

- `cmake/FFmpeg.cmake` (from `profiles/mhp3rd/cmake/FFmpeg.cmake`): option and
  target names changed to `SARUSARU_*`/`sarusaru_*`; a parallel-jobs default added.
- `host/audio/atrac_decoder.*` (FFmpeg frame decoder): namespace and macro renamed.
- `host/audio/atrac.cpp`: WAVE header parsing, the sample-position convention
  and all-on-memory decoding from `host/hle/hle_atrac.cpp`; the streaming API
  this game uses is new.
- `host/movie/psmf_demuxer.*` and `host/movie/avc_decoder.*` (PSMF pack
  demuxing, H.264 decoding on FFmpeg): namespace and macro renamed.
  `host/movie/psmf_player.*` (the scePsmfPlayer library) is new.
- `host/gpu/ge_state.*`, `texture_decode.*`, `triangle_indices.hpp`,
  `alpha_test.hpp`, `host/gpu/shaders/ge.vert`, `ge.frag`,
  `tools/embed_shaders.py`, `tests/ge_state_tests.cpp` and
  `tests/texture_decode_tests.cpp`: namespace and environment-variable prefix
  renamed; Yakumo's profiling hooks removed; `GeState::process` added so this
  profile's GE walker keeps the control flow and hands it every other command.

## FFmpeg (LGPL-2.1-or-later, dynamically linked, not in the repository)

Configuring the build downloads FFmpeg 7.1.5 from ffmpeg.org, checks its
SHA-256 (pinned in `cmake/FFmpeg.cmake`) and builds only `libavcodec` and
`libavutil` with the ATRAC3, ATRAC3plus and H.264 decoders; the build fails if
the configuration is not LGPL-only. The shared libraries are placed in
`bin/lib/` next to the executable with `FFmpeg-COPYING.LGPLv2.1.txt` and
`FFmpeg-SOURCE.txt` (source URL, hash and configure flags). Releases must ship
those notices and keep FFmpeg dynamically linked and replaceable. On Windows
x64 a pinned prebuilt LGPL shared build is used instead (LGPL-3.0-or-later).
`-DSARUSARU_FFMPEG=system` uses an installed FFmpeg; `OFF` builds without
music or movies.

## Host shell dependencies (downloaded at configure time, not in the repository)

`cmake/Graphics.cmake` downloads each of these at a pinned version, checks its
SHA-256 and builds or places it with the project. Releases must carry their
notices.

| Component | Version | Licence | Use |
| --- | --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.16 | zlib | window, input, audio output, Vulkan loading; shared library |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | vulkan-sdk-1.4.357.0 | Apache-2.0 OR MIT | headers only |
| [glslang](https://github.com/KhronosGroup/glslang) | 16.5.0 | BSD-3-Clause and others (its `LICENSE.txt`) | build-time shader compiler; not shipped |
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (macOS) | 1.4.2 | Apache-2.0 | Vulkan on Metal; prebuilt `libMoltenVK.dylib` with `MoltenVK-LICENSE.txt` in `bin/lib/` |

## Launcher interface (downloaded at configure time, not in the repository)

`cmake/Ui.cmake` downloads each of these at a pinned version and checks its
SHA-256. Dear ImGui is compiled unmodified into the executable; the fonts and
all three licences are placed in `bin/resources/` beside it. Releases must
carry those notices.

| Component | Version | Licence | Use |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.9b | MIT (`DearImGui-LICENSE.txt`) | launcher widgets, SDL3 and Vulkan backends |
| [Luckiest Guy](https://github.com/google/fonts/tree/main/apache/luckiestguy) | google/fonts `e9678a5` | Apache-2.0 (`fonts/LuckiestGuy-LICENSE.txt`) | titles and big buttons |
| [M PLUS Rounded 1c](https://github.com/coz-m/MPLUS_FONTS) Bold, ExtraBold, Black | google/fonts `e9678a5` | SIL OFL 1.1 (`fonts/MPLUSRounded1c-OFL.txt`, from the M+ project at `eb60490`) | text, and Japanese titles |

The launcher's own code (`host/ui/`) is new. Yakumo's Dear ImGui layer was
read as a reference for its device handling (see `YAKUMO_REUSE.md`), not copied.

## llvm-mingw (Windows; downloaded, not in the repository)

[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20260922 (ucrt), pinned
and hash-checked in `cmake/LlvmMingw.cmake`, builds the Windows release, and a
player's launcher downloads the same release once to build the game module
(`host/install/`). Its C++ runtime, `libc++.dll` and `libunwind.dll`, ships
unmodified beside `sarusaru.exe` with `llvm-mingw-LICENSE.txt` (Apache-2.0
with LLVM exceptions; the toolchain's other parts are under their own
licences in that file).

The game module itself is never distributed: each player's launcher builds it
from their own disc on their own machine.

## Real-ESRGAN ncnn-vulkan (HD textures; downloaded by the launcher, not in the repository or packages)

[Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) ncnn-vulkan 20220424
(release v0.2.5.0, BSD-3-Clause; built on [ncnn](https://github.com/Tencent/ncnn),
BSD-3-Clause), pinned per platform with its SHA-256 in
`host/install/texture_build.cpp`. When a player chooses HD textures, their
launcher downloads it into the per-user data directory and runs it,
unmodified, with its bundled models, on textures read from the player's own
disc (docs/hd-textures.md).

## tiny-AES-c (Unlicense)

`third_party/tiny_aes/aes.c` and `aes.h` are unmodified copies of
[tiny-AES-c](https://github.com/kokke/tiny-AES-c) at commit
`23856752fbd139da0b8ca6e471a13d5bcc99a08d`, as vendored by Yakumo; released
into the public domain under the Unlicense (`third_party/tiny_aes/UNLICENSE`).
They are compiled as C++.

## Application icon (Flaticon license, attribution required)

`include/image/logo.png` and `resources/sarusaru.ico` (the same image at 16 to
256 pixels) are the [Banana icon](https://www.flaticon.com/free-icon/banana_7246727)
created by [Magnific](https://www.flaticon.com/authors/magnific) -
[Flaticon](https://www.flaticon.com/), used under Flaticon's free license with
this attribution.
