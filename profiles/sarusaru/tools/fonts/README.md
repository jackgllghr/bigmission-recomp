# In-game replacement label font

The `MPLUSRounded1c-{Bold,ExtraBold}-ASCII.ttf` files are printable-ASCII
subsets of M PLUS Rounded 1c. Control hints use Bold; other image labels
use ExtraBold. The fonts are licensed under SIL OFL 1.1; see [OFL.txt](OFL.txt).

Source: google/fonts commit `e9678a5385322c63730122ba464765baad83cfd8`,
`ofl/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf` (the same pinned source as
`cmake/Ui.cmake`). Source SHA-256:
`8e7c15901dca87f1451b356dda594f7d092ba252a5dcc47da74523a242493c36`.
The Bold source is `ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf`, with SHA-256:
`c358630584e8e2d8fbd6121d0f4693255ffef6d1e6d4f3441fd6e5a963a11f9e`.
Glyph outlines are unchanged; the subset includes U+0020 through U+007E,
matching the label catalog's supported characters.

Regenerate with fontTools 4.67.0 (needed only to regenerate this font):

```sh
pyftsubset MPLUSRounded1c-ExtraBold.ttf --unicodes=U+0020-007E \
  --layout-features='*' --name-IDs='*' --name-languages='*' \
  --no-recalc-timestamp --output-file=MPLUSRounded1c-ExtraBold-ASCII.ttf
```

Run the same command with `Bold` in place of `ExtraBold` for the control-hint subset.

Pack authoring needs only Pillow and these checked-in subsets, with no font
download or launcher build required.
