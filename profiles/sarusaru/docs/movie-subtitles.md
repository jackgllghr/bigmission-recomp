# Movie subtitles

The game's 19 cutscene movies (`PSP_GAME/USRDIR/movie/d01-d03.pmf` and
`m01-m16.pmf`) have no subtitle track, so their script comes from their
audio. The shell draws subtitles over the movie picture at window resolution
while it plays; **Settings → Interface → Movie Subtitles** turns them off
(`[translation] subtitles = 0` in settings.ini).

## Files

One SubRip file per movie, named after the movie file and the language:
`m01.en.srt` for `m01.pmf`. Times count from the movie's first picture.

```
1
00:00:03,200 --> 00:00:05,900
Look, a monkey!

2
00:00:06,100 --> 00:00:09,000
Two lines are fine,
and long lines wrap.
```

Tags such as `<i>` are dropped, WebVTT-style `00:00:03.200` times are
accepted, and a file that does not parse is reported on stderr and skipped.

The shell takes them from the first of these that has them:

1. the folder `SARUSARU_SUBTITLES` names (for working on them),
2. the English translation pack, which carries them for players,
3. `subtitles/` beside the English translation pack,
4. `subtitles/` beside settings.ini.

Subtitles are translation, so like the pack they are never in the launcher
package (`scripts/package.py` refuses `.srt` files). They are kept in
sarusaru-translations and bundled into the pack when it is built:

```sh
out/translation-tools/bin/python profiles/sarusaru/tools/build_menu_translation.py \
  '/path/to/SaruSaru.iso' \
  --gameplay-catalog ../sarusaru-translations/english-gameplay-v1.json \
  --subtitles ../sarusaru-translations/subtitles \
  --output out/translations/english-v1.sstp
```

Every `<movie>.en.srt` in the folder goes in (at most 64 movies, 256 KiB each
and 2 MiB in all); a lowercase movie name and UTF-8 text are required. A file
that does not parse is reported on stderr when the game starts and left out.

## Making them

1. Dump the soundtracks from your disc. The tool uses the game's own demuxer
   and ATRAC3plus decoder, so its timings match the game's:

   ```bash
   out/macos-arm64/bin/sarusaru_dump_movie_audio path/to/disc.iso movie-audio
   ```

   It writes `<movie>.wav` (44.1 kHz stereo) and lists each movie's length.
   The `sound` column is the share of the movie above -40 dBFS; all of them
   have music throughout, so it does not show which ones have speech.
   `d01-d03` look like attract demos played from the title screen.

2. Transcribe offline with Whisper, for example
   [whisper.cpp](https://github.com/ggml-org/whisper.cpp) with a large model.
   Japanese first, then translate:

   ```bash
   whisper-cli -m ggml-large-v3.bin -l ja -osrt -of m01.ja m01.wav
   ```

   `--translate` gives a rough English draft directly, but expect it to need
   rewriting. Background music hurts accuracy; isolating the voice first (e.g.
   with Demucs) helps.

3. Review against the movie: character names (matching the English dialogue
   translation), made-up words and timing. Keep lines to about 40 characters and two lines, and
   each cue on screen for at least a second.

4. Check in the game: put the files in a folder, then run

   ```bash
   SARUSARU_SUBTITLES=path/to/subtitles out/macos-arm64/bin/sarusaru path/to/disc.iso
   ```

   With `SARUSARU_CAPTURE` set, frames with a subtitle are also saved as
   `subtitled_*.bmp`.
