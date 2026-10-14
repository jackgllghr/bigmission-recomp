# Third-party material in the SaruSaru profile

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

## tiny-AES-c (Unlicense)

`third_party/tiny_aes/aes.c` and `aes.h` are unmodified copies of
[tiny-AES-c](https://github.com/kokke/tiny-AES-c) at commit
`23856752fbd139da0b8ca6e471a13d5bcc99a08d`, as vendored by Yakumo; released
into the public domain under the Unlicense (`third_party/tiny_aes/UNLICENSE`).
They are compiled as C++.
