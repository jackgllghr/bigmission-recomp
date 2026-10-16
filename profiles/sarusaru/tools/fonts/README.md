# In-game replacement label font

`MPLUSRounded1c-ExtraBold-ASCII.ttf` is a printable-ASCII subset of M PLUS
Rounded 1c ExtraBold, used by the offline image-label authoring tool. The
font is licensed under SIL OFL 1.1; see [OFL.txt](OFL.txt).

Source: google/fonts commit `e9678a5385322c63730122ba464765baad83cfd8`,
`ofl/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf` (the same pinned source as
`cmake/Ui.cmake`). Source SHA-256:
`8e7c15901dca87f1451b356dda594f7d092ba252a5dcc47da74523a242493c36`.
Glyph outlines are unchanged; the subset includes U+0020 through U+007E,
matching the label catalog's supported characters.

Regenerate with fontTools 4.67.0 (needed only to regenerate this font):

```sh
pyftsubset MPLUSRounded1c-ExtraBold.ttf --unicodes=U+0020-007E \
  --layout-features='*' --name-IDs='*' --name-languages='*' \
  --no-recalc-timestamp --output-file=MPLUSRounded1c-ExtraBold-ASCII.ttf
```

Pack authoring needs only Pillow and this checked-in subset, with no font
download or launcher build required.
