# HD textures

HD (4x, for about 1080p) and 4K (8x) textures are made **on the player's
device** from their own disc, unless the build offers a prebuilt HD pack
(see "Prebuilt pack" below). The port never ships pixels derived from the
game: what is distributed is a *recipe* (which textures to upscale, with which
model and settings), and the launcher runs a pinned upscaler locally. This
keeps the rule that applies to the generated game code (see
[`../../../PLAN.md`](../../../PLAN.md), Goal and decisions).

```
author:  play with --dump-textures      ->  dump/ (PNG + textures.tsv), kept private
         tools/build_texture_recipe.py  ->  recipe (hashes, disc locations, options; no pixels)
player:  launcher Settings > Textures   ->  downloads recipe + Real-ESRGAN, reads the
                                            player's disc, upscales on the device
                                        ->  <user data>/textures/<4x|8x>-<recipe>/
game:    [graphics] texture_pack        ->  renderer swaps textures by identity, and
                                            collects the ones the disc could not give
```

## Texture identity (`host/gpu/texture_hash.hpp`)

A texture is identified by what the game draws, not where it lives: the
SHA-256 of a version tag, the logical size, and the decoded RGBA of the
logical rectangle, cut to 128 bits (32 hex digits). The logical width is the
GE width, or the row stride (`buffer_width`) when that is narrower, because
columns past the stride wrap into the next row and are not the texture's own.
Two palettes giving the same pixels give the same identity, so one upscale
covers both. The identity is taken before any English translation pack has
replaced its regions, so dumps and packs hold the original texture. For a
translated texture, the pack's image is the art around the translation's
authored regions, and the translation's HD lettering is drawn in them.

It is computed only on a renderer cache miss, and only when a dump or HD pack
is active.

Known limit: the height is the GE's power-of-two height. A texture that uses
fewer rows than that includes whatever lies below it in memory, and if those
bytes change, the identity changes too. The dump shows such textures as near
duplicates. If they turn out to matter, the recipe can map several identities
to one source.

## Dump mode

```sh
out/macos-arm64/bin/sarusaru --dump-textures ~/sarusaru-dump "<disc>.iso"
```

Each texture the renderer decodes is written once as `<hash>.png` (the
logical rectangle in straight, unpremultiplied RGBA). It also gets a line in
`textures.tsv`:

```
hash  width  height  ge_width  ge_height  buffer_width  format  swizzled
```

`format` is the GE texture format number (`gpu::TextureFormat`). Running
again into the same directory adds only new textures, so a play-through can
be collected over many sessions. 45 seconds of the boot and title screens
give 55 textures.

## HD pack (`host/textures/hd_pack.hpp`)

A directory:

```
pack.ini        format = 1
                disc = UCJS10060
                scale = 4          (2-8; 4 for HD, 8 for 4K)
<hash>.png      the logical rectangle at scale times its size, straight RGBA
```

Select it with `[graphics] texture_pack = <dir>` in `settings.ini` (relative
to the settings file), or with `--texture-pack DIR` (or `none`) for one run.
Startup only lists the directory. An image is decoded when its texture is
first drawn, then uploaded at `scale` times the size. The shader samples with
normalized coordinates, so nothing else changes. An image that cannot be read,
or that has the wrong size, is reported once on stderr and the original
texture is drawn instead. Images are at most 4096 pixels on a side. Uploads
larger than the 32 MB per-frame staging buffer get a staging buffer of their
own.

### Alpha must stay straight

Nine of the first 55 dumped textures have colour under zero alpha: 80% of the
texels in one 512x512 background. The game draws them with texture alpha
ignored (TCC off), so that colour is visible. An upscaler that premultiplies
alpha (macOS `sips` does, for example) blacks these textures out. The upscale
step must scale colour and alpha as separate planes and keep the colour of
transparent texels. The launcher never relies on the upscaler for
this: it upscales colour and alpha as separate images (see below).

### Verification

- `sarusaru_hd_texture_tests`: identity (stride, shape, padding), manifest
  parsing, dump once per identity across runs, pack lookup, and rejecting
  images of the wrong size.
- In the game, a 4x pack whose images were mirrored drew mirrored glyph
  atlases. A nearest-neighbour 4x pack drew the boot screens pixel-identical
  to no pack, and replaced the title scene's 3D textures.

## Finding textures on the disc

Investigated on UCJS10060 (`host/textures/disc_textures.hpp`, mirrored in the
tool). Under `PSP_GAME/USRDIR` the disc holds 694 loose `.tm2` files and
261 `.lzs` files (4 KiB-ring LZSS), many of them PAC archives, plus a few `.pac`
files whose members are themselves `.lzs`. Searching all of them for `TIM2`
finds about 11,000 real pictures: version 4, one picture each, 4- or 8-bit
indexed, swizzled in 16x8-byte blocks, 32-bit palettes, some with mipmaps.
(About 1,900 more hits are the bytes "TIM2" inside other data.)

Decoding the top mip level gives the runtime identity exactly, provided that
the 8-bit pictures with CSM1 palettes (`clut_type` bit 6 clear) have bits 3
and 4 of each index swapped, as the game does when it loads them. 17 of the 55
dumped boot/title textures matched this way: Pipo models, the title scene, the
font atlas and the sky. Two kinds can't be found on the disc:

- **Text drawn at runtime** (23 of the 55, all 256x64 4-bit): a recipe should
  skip these.
- **Textures assembled or re-paletted at runtime, or stored in other formats**
  (15): at best 86% of their pixels match a disc picture of the same size, and
  three have no picture of their size at all.

A picture whose height isn't a power of two can't be matched either, because
the GE draws it with rows of other data below.

A **source locator** names where a picture is: a disc path, then `!lzs`
(decompress) and `|MEMBER` (take a PAC member) steps, then `#` and the hex
offset of the TIM2 in the result. For example,
`PSP_GAME/USRDIR/data/title.lzs!lzs|sora_umi.tm2#0`. The launcher decodes the
picture, recomputes its identity and upscales it only if that equals the
recipe's hash. A wrong locator, or a different printing of the disc, therefore
never produces a wrong texture; such a texture falls back to being collected.

## Recipe (`host/textures/recipe.hpp`)

```
sarusaru-texture-recipe  1
disc      UCJS10060
defaults  model=realesrgan-x4plus-anime  scale=8  alpha=model  edge=clamp
scan      PSP_GAME/USRDIR/data
texture   <hash>  <source locator, or ->  [key=value...]
skip      <hash>
```

- `model`: `realesrgan-x4plus` (photographic), `realesrgan-x4plus-anime` (flat
  shading), `realesr-animevideov3` (light), or `nearest` (no model: pixel
  art, fonts).
- `scale`: the most the model may enlarge this texture (1, 2, 4 or 8). A pack
  with a larger scale finishes with nearest-neighbour.
- `alpha`: `model` (the alpha plane goes through the model too), `linear`,
  `nearest`, or `opaque` (checked; a texture that isn't opaque falls back to
  `linear`).
- `edge`: `clamp`, or `wrap` for tiling textures. The model sees 8 texels of
  border taken from the far side, so tiles meet without a seam.
- `scan` also upscales every picture found under a disc directory, with the
  defaults. This is broad coverage without a play-through; a whole-disc scan
  is about 11,000 textures and hours of upscaling, so it is the author's call.
- `texture HASH -` names a texture that isn't on the disc. The launcher writes
  it to the pack's `wanted.txt`, the game saves it to `collected/` when it is
  drawn, and the launcher's Textures screen then offers to upscale what was
  collected.

`tools/build_texture_recipe.py` writes a recipe from one or more dumps and the
maintainer's disc. It locates each dumped texture (about 40 s for the 55),
marks fully opaque ones `alpha=opaque`, and applies an overrides file
(`<hash> key=value...` or `<hash> skip`). It prints the recipe's SHA-256 and
size for configuring a build:

```sh
python3 tools/build_texture_recipe.py --iso "<disc>.iso" --dump ~/sarusaru-dump \
    --overrides overrides.txt -o sarusaru-textures.recipe
cmake -S . -B out/macos-arm64 -DSARUSARU_TEXTURE_RECIPE_URL=https://.../sarusaru-textures.recipe \
    -DSARUSARU_TEXTURE_RECIPE_SHA256=<sha256> -DSARUSARU_TEXTURE_RECIPE_SIZE=<bytes>
```

A `file://` URL works for trying a local recipe; CMake warns about it. Like
the English pack, the recipe is published apart from the port. It holds
hashes and disc paths, never pixels.

## Making a pack on the device (`host/install/texture_build.hpp`)

**Settings > Graphics > Textures** offers Original, HD (4x) and 4K (8x). The
choice is stored as the pack's directory in `[graphics] texture_pack`. If the
chosen pack isn't made yet, or the game has collected textures for it, the
**HD Textures** screen explains what will happen and makes the pack, with
progress and cancellation. It goes through these steps:

1. Downloads the recipe (kept as `<user data>/textures/recipe-<sha16>.txt`)
   and the pinned upscaler, Real-ESRGAN ncnn-vulkan 20220424
   ([v0.2.5.0](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0),
   BSD-3-Clause). It is about 50 MB for macOS (universal), Windows x64 and
   Linux x86-64, each checked by SHA-256, and is unpacked into
   `<user data>/tools/` with tar, or unzip, bsdtar or Python on Linux. There
   is no build for Linux on Arm or Android, and the screen says so.
2. Reads the recipe's textures from the player's disc, the pictures in its
   scanned directories, and the textures in `collected/`, skipping any already
   in the pack.
3. Splits each texture into an opaque colour image and a grey alpha image,
   both padded, and runs the upscaler once per pass over a directory of every
   texture with the same plan: 4x with the chosen model, then
   `realesr-animevideov3` 2x for 8x.
4. Crops the padding, finishes with nearest-neighbour if the texture's scale
   is capped, rejoins the planes, and writes `<hash>.png`, `wanted.txt` and,
   last, `pack.ini`. A pack that is cut short keeps the textures it finished,
   and the next run makes only the rest.

`build_texture_pack()` is the part without downloads.
`sarusaru_texture_recipe_tests` runs it on an invented disc, with
`sarusaru_fake_upscaler` (nearest-neighbour, the same command line) standing
in for the model.

### Verified on macOS (Apple M4)

- From the launcher, with a local recipe of the 55 dumped textures (23 text
  textures skipped): the download, unpacking, reading 17 textures from the
  disc and upscaling 25 images took about 30 s. HD (4x) was then selected, and
  `wanted.txt` listed the 15 textures that aren't on the disc.
- In the game, the pack's 17 textures replaced the title scene's (smoothed by
  the anime model; the sand may suit `realesrgan-x4plus` better). All 15
  waiting textures were collected during the boot and title screens, and the
  launcher's "Upscale the textures collected while playing" made them, giving
  32 textures and nothing waiting.
- The developer logo's background, white under zero alpha and drawn with
  texture alpha ignored, stays white, so the planes stay straight.
- 8x makes 4096x4096 images for 512x512 textures, up to 33 MB of PNG each.

## Prebuilt pack (`install::PrebuiltPackDownload`)

A build configured with `SARUSARU_HD_PACK_URL`, `_SHA256` and `_SIZE` offers
a finished pack instead of making HD on the device. Unlike the recipe, this
**is** game-derived pixels (upscaled menus, icons, backgrounds and game
textures), so it is published apart from the port, in its own repository,
the way the English pack (which also carries translated images) is; the port
and its packages still contain none.

- The zip holds `pack.ini` and `<hash>.png`, at its top or in its only
  folder. `tools/package_hd_pack.py PACK_DIR -o pack.zip` writes one (stored,
  `pack.ini` and hash-named images only) and prints its SHA-256 and size.
- Choosing HD (4×), from the Textures row or the Modern preset, opens the
  HD Textures screen with a download button. The launcher downloads the zip
  with curl, keeps it only if the SHA-256 matches, unpacks it with the
  system's tools (`unpack_zip`, shared with the upscaler), and installs it as
  `<user data>/textures/4x-prebuilt-<sha16>/` only if its `pack.ini` loads
  for the supported disc. A refused or cancelled download leaves nothing.
- The pack has no `wanted.txt`, so nothing is collected while playing.
- 4K (8×) still uses the recipe and the device's upscaler.
- `sarusaru_texture_recipe_tests` installs a zip of an invented pack and
  refuses one for another disc. Checked in the launcher with a `file://` pack.

## Next

- Publish a first recipe built from a full play-through, with a skip list
  for runtime text and per-texture models.
- To upscale collected textures, the player now has to switch Textures away
  and back. The launcher could offer it from the main menu instead.
- Windows and Linux runs of the download and unpacking path (not yet tried).
- Renderer: decode images off the render thread, and keep a small CPU cache
  so textures evicted after 600 frames do not decode again. Consider BC7 for
  8x packs to save VRAM.
