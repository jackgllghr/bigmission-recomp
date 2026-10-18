# In-game replacement label font

The `MPLUSRounded1c-{Bold,ExtraBold}-ASCII.ttf` files are printable-ASCII
subsets of M PLUS Rounded 1c. Control hints and yellow/orange image labels use Bold. ExtraBold remains
available for older pack builds. The fonts are licensed under SIL OFL 1.1; see [OFL.txt](OFL.txt).

Source: google/fonts commit `e9678a5385322c63730122ba464765baad83cfd8`,
`ofl/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf` (the same pinned source as
`cmake/Ui.cmake`). Source SHA-256:
`8e7c15901dca87f1451b356dda594f7d092ba252a5dcc47da74523a242493c36`.
The Bold source is `ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf`, with SHA-256:
`c358630584e8e2d8fbd6121d0f4693255ffef6d1e6d4f3441fd6e5a963a11f9e`.
Glyph outlines are unchanged; both subsets include U+0020 through U+007E.
Bold additionally includes U+00A9 for the main-menu copyright footer and is
used by white body text as well as control hints.

Regenerate with fontTools 4.67.0 (needed only to regenerate this font):

```sh
pyftsubset MPLUSRounded1c-ExtraBold.ttf --unicodes=U+0020-007E \
  --layout-features='*' --name-IDs='*' --name-languages='*' \
  --no-recalc-timestamp --output-file=MPLUSRounded1c-ExtraBold-ASCII.ttf
```

Run the same command with `Bold` in place of `ExtraBold` and
`--unicodes=U+0020-007E,U+00A9` for the control-hint/body subset.

Pack authoring needs only Pillow and these checked-in subsets, with no font
download or launcher build required.
