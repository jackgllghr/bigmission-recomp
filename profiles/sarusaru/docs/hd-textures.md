# HD textures

HD (4x, for about 1080p) and 4K (8x) textures are made **on the player's
device** from their own disc. The port never ships pixels derived from the
game: what is distributed is a *recipe* (which textures to upscale, with which
model and settings), and the launcher runs a pinned upscaler locally. This
keeps the rule that applies to the generated game code (see
[`../../../PLAN.md`](../../../PLAN.md), Goal and decisions).

```
author:  play with --dump-textures  ->  dump/ (PNG + textures.tsv)
         tune models per texture    ->  recipe (hashes + settings, no pixels)   [not yet]
player:  launcher downloads recipe + upscaler, collects and upscales textures
         from the player's disc     ->  <user data>/textures/<pack>/           [not yet]
game:    [graphics] texture_pack    ->  renderer swaps textures by identity
```

This first slice has the texture identity, the dump mode and the HD pack the
renderer loads. The recipe, the upscaler download and the launcher screens come
next.

## Texture identity (`host/gpu/texture_hash.hpp`)

A texture is identified by what the game draws, not where it lives: the
SHA-256 of a version tag, the logical size, and the decoded RGBA of the
logical rectangle, cut to 128 bits (32 hex digits). The logical width is the
GE width, or the row stride (`buffer_width`) when that is narrower, because
columns past the stride wrap into the next row and are not the texture's own.
Two palettes giving the same pixels give the same identity, so one upscale
covers both. The identity is taken after any English translation pack has
replaced its regions, so translated menus get their own HD images.

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
transparent texels. Real-ESRGAN's ncnn build handles alpha separately, but the
recipe tooling should still check for this.

### Verification

- `sarusaru_hd_texture_tests`: identity (stride, shape, padding), manifest
  parsing, dump once per identity across runs, pack lookup, and rejecting
  images of the wrong size.
- In the game, a 4x pack whose images were mirrored drew mirrored glyph
  atlases. A nearest-neighbour 4x pack drew the boot screens pixel-identical
  to no pack, and replaced the title scene's 3D textures.

## Next

- Recipe format and authoring tool: per-hash model, scale, alpha mode and
  skip list, plus a way to find each texture on the disc (TIM2 in the
  LZS/PAC archives) so a player does not have to play through to collect
  them. Textures not found that way can be upscaled lazily: dump on a miss,
  upscale in the background, and use the result on a later run.
- Launcher: a Textures setting (Original / HD / 4K), downloading the recipe
  and a pinned `realesrgan-ncnn-vulkan`, upscaling with progress and
  cancellation, and writing `pack.ini`.
- Renderer: decode images off the render thread, and keep a small CPU cache
  so textures evicted after 600 frames do not decode again. Consider BC7 for
  8x packs to save VRAM.
