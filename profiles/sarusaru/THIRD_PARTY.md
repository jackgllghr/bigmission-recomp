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

## tiny-AES-c (Unlicense)

`third_party/tiny_aes/aes.c` and `aes.h` are unmodified copies of
[tiny-AES-c](https://github.com/kokke/tiny-AES-c) at commit
`23856752fbd139da0b8ca6e471a13d5bcc99a08d`, as vendored by Yakumo; released
into the public domain under the Unlicense (`third_party/tiny_aes/UNLICENSE`).
They are compiled as C++.
