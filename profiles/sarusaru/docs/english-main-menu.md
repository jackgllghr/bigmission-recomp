# English main-menu prototype

This implements the first title/menu slice of the
[translation plan](translation-investigation.md): the title logo, Press Start
prompt, and four main-menu entries. Other screens and audio retain Japanese.
It uses `include/image/SaruSaruBigMission.png` for the English logo. No ISO,
executable, generated guest code, save format or menu input behavior is patched.

## Build and select the pack

From the repository root, after the usual game preparation/build:

```sh
python3 -m venv out/translation-tools
out/translation-tools/bin/pip install -r profiles/sarusaru/tools/requirements-translation.txt
out/translation-tools/bin/python profiles/sarusaru/tools/build_menu_translation.py \
  '/path/to/SaruSaru.iso'
python3 profiles/sarusaru/scripts/build_and_run_macos.py -- \
  --translation-pack out/translations/english-menu.sstp
```

`--translation-pack FILE` selects English for that run. The supplied PNG is the
builder's default logo; `--logo FILE` selects another. `--catalog FILE` selects
an edited UTF-8 catalog and `--output FILE` selects the generated pack location.
The builder renders with Pillow's bundled default font rather than requiring a
platform font. Pillow 12.3.0 was used for this prototype.

To persist selection, edit the selected settings file and restart:

```ini
[translation]
language = en
pack = /absolute/path/to/english-menu.sstp
```

Relative pack paths resolve beside the settings file. `language = ja` selects
the original Japanese presentation, even if a pack path remains configured.
`language = en` requires a pack path; invalid packs stop startup with an error.
The default settings retain Japanese. Languages other than `ja`/`en` are not
implemented yet. The headless bootstrap does not apply these host GPU overrides.

The pack is generated locally from the user's source archive. It stores
replacement pixels and hashes, and contains no copied Japanese image payloads.
Generated packs, previews and captures live under ignored `out/`, `analysis/`,
`progress/` or `captures/` directories. The public source contains only tooling,
the English catalog and invented test fixtures, plus the already supplied logo.

## Verified resource mapping

The game loads `PSP_GAME/USRDIR/Minigames/Menkolzs2.pac` during startup. This
container holds individually LZSS-compressed TIM2 images, including the title
assets. Unlike the shared UI images in `data/common.lzs`, its menu resources
match the main menu observed in the running game.

| Stable pack ID | Archive member | Logical image | GE extent / stride | Replacement |
| --- | --- | --- | --- | --- |
| `title.logo` | `i_g_titl_l.lzs` | 352x144 | 512x256 / 352 | Supplied English logo, fitted with aspect ratio preserved |
| `title.press_start` | `e_nabi_a0.lzs` | 256x16 | 256x16 / 256 | Press START |
| `menu.labels` | `b_but_b0.lzs` | 256x96 | 256x128 / 256 | Four 256x24 slots: New Game, Continue, Options, Gachi King |

The decoder reads a four-byte output size followed by the observed 4 KiB LZSS
ring format. Indexed image data is swizzled in 16-byte by 8-row blocks. The
256-color TIM2 palettes used here need the observed index-bit 3/4 exchange when
CLUT type is 3. Cropped decoded images were compared byte-for-byte with textures
produced by the guest draw path. This verified decompression, swizzle and palette
handling for all three selected images, not every TIM2 or PAC variant.

Reproduce the local inventory and optionally view original source images with:

```sh
out/translation-tools/bin/python profiles/sarusaru/tools/inspect_menu_assets.py \
  '/path/to/SaruSaru.iso' --previews
```

The output defaults to ignored `profiles/sarusaru/analysis/menu-assets/`.
The readers deliberately reject unsupported layouts, overlapping archive members,
unsafe names, truncated data and unreasonable output sizes.

## Runtime adapter and pack format

The shell loads a pack before the first draw, after `GameSession` validates the
original executable/disc. It independently hashes each declared source file
from the ISO. On a texture cache miss, the renderer decodes the original pixels
and asks `TexturePack` for an exact replacement. Render-target sampling retains
its existing path. Interpolated draws resolve the same replaced texture as normal
draws, through the existing renderer cache.

Identification uses logical image dimensions, GE extents, buffer stride and a
full SHA-256 of every RGBA byte inside the logical image. It does not use guest
addresses or the renderer's address-dependent/sampled cache key. The game uses
power-of-two GE extents larger than its logical images; the padded area can
contain neighboring assets. Hashing the whole GE extent would produce unstable
identities. Replacements modify only the logical rectangle and preserve padding.

The `SSTEXPK1` binary container uses little-endian integers:

1. Eight-byte magic/version, nine-byte disc ID, 64 lowercase hexadecimal bytes
   of executable SHA-256, and two-byte locale (`en`).
2. `u32` source count, followed by source records: `u16` path length, ASCII path,
   64-byte file SHA-256. Paths must be safe ISO-relative paths.
3. `u32` replacement count, followed by replacement records: `u16` ID length,
   ASCII ID, five `u16` dimensions (logical width/height, GE width/height,
   buffer stride), 64-byte logical source RGBA SHA-256, `u32` payload length,
   and exactly logical width times height times four RGBA bytes.

Limits: 32 MiB per pack/source, 16 sources, 64 replacements, GE extents no larger
than 512, IDs up to 128 bytes and paths up to 255 bytes. The reader rejects
trailing bytes, duplicate IDs/identities, incompatible executable/source hashes
and malformed dimensions or payload lengths. A single-file pack has no asset
extraction or external replacement paths to escape.

Fallback is at **texture-entry granularity**: omitted/unmatched texture entries
retain Japanese pixels. The supplied builder requires all five labels because
four share one atlas; it rejects incomplete catalogs rather than erasing missing
labels. Per-label mixed-language fallback, generic string/control-code catalogs,
font metrics, arbitrary image formats, IO overlays, movie subtitles, pack
installation/version migration and other language glyph coverage remain future
work. The labels currently use printable ASCII and enforce their pixel budgets;
Gachi King is a transliteration of the named minigame, pending editorial review.

## Verification

Verified on macOS arm64 on 2026-10-14:

- A clean worktree CMake build, including the native AOT game and Vulkan shell,
  succeeded. Existing pinned dependency sources and the FFmpeg install were
  reused locally; game inputs/generated code were copied into ignored directories.
- All 33 CTest suites passed. Translation-reader fixtures cover exact pixel
  replacement, Japanese fallback, changed guest addresses/padding, wrong stride,
  incompatible game/source hashes, duplicate identities, truncation and unsafe
  paths. Settings tests cover selection and round-trip behavior.
- Seven separate Pillow/asset-tool tests passed, including catalog rendering,
  overflow, missing labels, overlap, unsupported glyphs, output/input collision,
  LZSS overlap and malformed data, archive bounds, swizzle and palette mapping.
  Pillow authoring tests are optional/skipped when Pillow is absent from CTest's
  Python interpreter; they were explicitly run using the authoring virtualenv.
- Repeated pack generation produced byte-identical 317,914-byte packs.
- The supplied logo and Press START appeared in the actual Vulkan title-screen
  capture; New Game, Continue and Options appeared in the actual main menu.
  Captures were checked at both 480x272 and 960x544, with native game cadence and
  frame interpolation enabled. No clipping or aspect-ratio distortion was seen.
- Scripted Down/Down/Circle input entered the original Options screen, which
  retained Japanese. Japanese mode independently displayed the original title
  logo and labels. A private copy of the supplied save loaded successfully and
  selected Continue by default. All runs used separate memory-stick directories.
- The fourth atlas label (Gachi King) is included, but the tested fresh/provided
  saves do not unlock that menu entry. Its unlocked in-game state remains
  unverified. Other platforms, gameplay completion and all other translations
  remain outside this prototype's verification.

Local evidence is ignored: `progress/translation-build.log`,
`progress/translation-ctest.log`, `progress/menu-textures.tsv`, capture logs and
PNG/BMP files under `captures/english/`, `captures/english-native/`,
`captures/japanese-native/` and `captures/english-save/`.
The initial draw/decompression probe is `progress/menu_probe.cpp`.
Representative native menu images are
`captures/english-native/frame_0006_00048031ms.png` and
`captures/japanese-native/frame_0006_00048031ms.png`.
