# Follow

A tiny GTK4 music player whose layout is a re-implementation of [Sowon2](https://github.com/tsoding/hue_stretch)
(Tsoding's Jai + raylib "starting soon" player): near-black stage, a huge handwritten clock, cover + `♪ artist - title ♪`,
a single-line lyric subtitle and a QR code with the platform badge of the current track — all driven by the keyboard.

> Status: early development. Local and network audio (ffmpeg, incl. Bilibili), LRC lyrics, QR codes and the keyboard
> UI work; **remote cover art and lyrics still need to be downloaded by hand** (see [Roadmap](#roadmap)).

## Features

- **Immediate-mode UI on a single cairo canvas** — GTK4 only provides the window, keyboard and audio plumbing;
  everything on screen is redrawn by `render()` in [`src/core/main.c`](src/core/main.c), which is how raylib does it.
  There is no per-frame animation, so the canvas is only redrawn when the displayed second, the playback state or the
  window size changes (fullscreen stays just as cheap: ~1 ms per redraw at 1920×1080)
- **Animated background (Apple Music style)** — the cover is heavily blurred (downscale → 3× box blur) and laid
  under a mesh of seed colors extracted from the artwork (`src/core/palette.c`); three gradient blobs drift at
  different speeds so the background slowly "flows", then a scrim keeps the text readable.
  **Hardware acceleration**: the gradient is rendered into a tiny 256 px wide CPU texture (~2 ms) at ~20 fps and
  handed to a `GtkPicture`, so the upscale to 1080p is done by GTK's GL renderer instead of the CPU — the same
  trick as uploading a small video frame and letting the GPU stretch it
- **Custom note line** — `[note] text=` renders one dim, letter-spaced line above the big clock (per-track
  `note=` overrides it), sized like Sowon2's subtitle line so it reads at a glance
- **Bottom-left stats panel** — wall clock, total + today listening time, and a 12-week GitHub-style heatmap
  coloured with the artwork's seed colour. Playback seconds are accumulated per day and stored in
  `<cache>/follow/stats.dat` (plain text, `YYYY-MM-DD seconds`), saved every 30 s and on exit
- **Keyboard only** — no buttons at all; `F1` shows a help page listing every shortcut (the old on-screen hint
  is gone, the page is still one key away)
- **No audio-thread blocking** — the device callback only *tries* to take the decoder lock and never waits for the UI
  thread, so seeking / redrawing can't cause dropouts ([`src/core/media.c`](src/core/media.c))
- **Network audio** — decoding is done by FFmpeg (`libavformat` / `libavcodec` / `libswresample`), so local files,
  direct `http(s)` links and HLS all work; a Bilibili *page* URL (including `b23.tv` short links) is resolved through
  the public API ([`src/core/bilibili.c`](src/core/bilibili.c)) into its best DASH audio stream and played with the
  right `Referer` — no external `yt-dlp`/`ffmpeg` binary needed
- **Decode thread + ring buffer** — network I/O and decoding never touch the audio thread, which only drains a
  pre-filled SPSC ring buffer
- **LRC lyrics** — one line under the title, `[mm:ss.xx]` timestamps, `[offset:]`, multiple timestamps per line,
  inline `<mm:ss.xx>` stripped ([`src/core/lyrics.c`](src/core/lyrics.c)). Non-UTF-8 files are converted from
  GB18030 (most Chinese `.lrc` files are GBK), and when `lrc=` is missing/unreadable the player looks next to the
  audio file (`<name>.lrc`, `<name>*.lrc`, or the only `.lrc` in that folder). A path that really can't be
  resolved is reported in the subtitle line instead of only in the log
- **Lyric/audio version mismatch** — the timestamps come from the original release, the audio might be a
  Bilibili upload with an intro, so three things help: `lrc_offset=<ms>` in the ini, `[` / `]` to nudge by 250 ms
  while listening (`\` resets), and a startup diagnostic that logs when the audio and the lyrics are more than
  8 s apart (or when the lyrics are far longer than the audio, which means a different edit — switch source then,
  Bilibili's own CC subtitles are not available without a login)
- **Online lyric sources** — write the song info in the ini and the player fetches a properly timed LRC itself:
  `lrc=netease:李荣浩 耳朵`, `lrc=netease:186016` (song id), `lrc=qq:ナナヲアカリ ぴ`. NetEase and QQ Music are
  supported ([`src/core/lyricsrc.c`](src/core/lyricsrc.c)); the fetch runs on a worker thread and the result is
  cached under `g_get_user_cache_dir()/follow/` so repeat plays are instant and offline-safe
- **QR codes follow the link kind** — a *local* track shows no track QR at all; a *network* track shows one QR
  (its `page=`, else its own URL) with the platform badge: BILIBILI / APPLE MUSIC / YOUTUBE / SPOTIFY /
  SOUNDCLOUD / NETEASE / BANDCAMP. The glyphs are the official marks from
  [Simple Icons](https://simpleicons.org) (CC0-1.0, no attribution required), stored as plain SVG in
  [`src/icons/`](src/icons) and rendered with librsvg at 2× and masked white — drop another SVG in there and add
  its host to the table to support a new platform
  `[qrcode] link` adds a second QR for your own link (only when it is a network address), so a network track
  shows two codes side by side, each with its own capsule below. Both codes are rendered into the same square
  size, no matter how many modules each one needs
- **Adaptive layout** — every position and size is derived from the window size in `layout_compute()`: the right
  column is anchored to the right edge, the cover flexes to fill whatever vertical space is left, the clock is
  narrowed until it clears the right column, and a very short window scales the whole right block down instead of
  letting things overlap
- Local audio playback through [miniaudio](https://miniaud.io/) output + FFmpeg decode — play / pause / resume /
  stop / seek, auto-advance
- **Cover art from a local file *or* an `http(s)` URL** — remote covers are downloaded on a worker thread (no UI
  freeze) and cached as a pre-scaled surface; Bilibili's `hdslb.com` image suffixes are rewritten to a square JPEG
  and `.avif` links fall back to `.jpg`, because this GTK build has no AVIF loader
- **Title priority** — `title` / `artist` from the ini always win; the values resolved from a Bilibili link only
  fill in the fields you left out, and the file name is the last resort
- QR code drawn module-by-module from a configurable link ([libqrencode](https://fukuchi.org/works/qrencode/)),
  so it stays crisp at any window size
- INI based config: window geometry, QR code link, and an arbitrary number of `[audioN]` playlist entries
- libcurl wrapper ([`src/core/http.c`](src/core/http.c)) ready for the upcoming network features

## Keyboard

| Key | Action |
|---|---|
| `Space` | play / pause (starts the first playable track when nothing is loaded) |
| `←` / `→` | seek −5 s / +5 s (hold `Shift` for ±30 s) |
| `N` / `P` | next / previous track (wraps, skips unplayable entries) |
| `R` | restart the current track from the beginning |
| `S` | stop |
| `L` | repeat mode: off → all → one |
| `H` | toggle shuffle |
| `[` / `]` | shift the lyrics timeline 250 ms earlier / later (for videos with an intro) |
| `\` | reset the lyric offset back to the ini value |
| `F` / `F11` | toggle fullscreen (fullscreen hides the header bar — the Sowon2 look) |
| `F1` | toggle the shortcut page |
| `Q` | quit |
| `Esc` | close the shortcut page / leave fullscreen |

## Dependencies

| Library | Purpose |
|---|---|
| GTK 4 | window, widgets, image/texture loading |
| miniaudio | audio output device (header-only, vendored as `src/models/miniaudio.h`) |
| FFmpeg (`libavformat`, `libavcodec`, `libswresample`) | decoding local files and network streams |
| libqrencode | QR code generation |
| libcurl | HTTP(S) requests (Bilibili API) |
| JSON-GLib | parsing the Bilibili API responses |
| librsvg | rendering the brand icons (SVG) into cairo surfaces |
| GLib / GObject | config parsing (`GKeyFile`), threads, memory helpers |

### Install (MSYS2 / MinGW-w64)

```sh
pacman -S mingw-w64-x86_64-toolchain \
          mingw-w64-x86_64-gtk4 \
          mingw-w64-x86_64-libqrencode \
          mingw-w64-x86_64-curl
```

### Install (Debian / Ubuntu)

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libqrencode-dev libcurl4-openssl-dev                  libjson-glib-dev librsvg2-dev libavformat-dev libavcodec-dev libswresample-dev
```

miniaudio is header-only and already vendored — nothing to install.

## Build & run

```sh
make          # produces ./main.exe
./main.exe
```

`main.exe` reads its resources from the working directory, so **run it from the project root**:

```
src/style/style.css    UI theme
src/config.ini         configuration (created by hand, see below)
src/assets/*           whatever the config points at (audio, cover)
```

Logs on startup (parsed playlist, then one line per play / skip / failure):

```
** Message: config: loading src/config.ini (cwd = F:\...)
** Message: config: 1 track(s), window 800x600
** Message: track[0] audio1  李荣浩 - 耳朵  link=src/assets/耳朵-李荣浩.mp3  page=-  lrc=F:/lyrics/耳朵.lrc
** Message: lyrics: 11 lines from F:/lyrics/耳朵.lrc
** Message: media: opened src/assets/耳朵-李荣浩.mp3 — mp3float, 44100 Hz, 2 ch, 241.3s
** Message: play: track[0] audio1 -> src/assets/耳朵-李荣浩.mp3
```

These go through `g_message`, so they show up without extra env vars. Add
`G_MESSAGES_DEBUG=all ./main.exe` for the finer (DEBUG level) output.

`make clean` removes the binary.

## Configuration

`src/config.ini` is a [GKeyFile](https://docs.gtk.org/glib/struct.KeyFile.html) style INI — keys are case-sensitive, sections starting with `audio` + digits form the playlist.

```ini
[window]
width=800
height=600
fullscreen=false
title=Follow

[qrcode]
link=https://github.com/clevebitr

# one track per [audioN] section, ordered by N (audio2 < audio10)
[audio1]
link=src/assets/耳朵-李荣浩.mp3   # audio file: local path or http(s) URL, UTF-8
title=耳朵                  # optional; defaults to the file name
artist=李荣浩                # optional; shown as "artist - title"
#pic=F:/pics/cover.jpg       # cover art: local path or URL
#lrc=F:/lyrics/耳朵.lrc      # LRC subtitles: local path
#page=https://www.bilibili.com/video/BV1xx411c7mD   # QR code target (platform page)
#platform=bilibili          # optional; otherwise detected from the page URL

# a Bilibili page URL is resolved to its audio stream automatically
[audio2]
link=https://www.bilibili.com/video/BV1GJ411x7h7
#page=https://www.bilibili.com/video/BV1GJ411x7h7
#lrc=F:/lyrics/never-gonna-give-you-up.lrc
```

| Section | Key | Required | Notes |
|---|---|---|---|
| `window` | `width`, `height` | no | fallback `800×600`, non-positive values are ignored |
| | `fullscreen` | no | `true` / `false`, default `false` |
| | `title` | no | default `Follow` |
| `note` | `text` | no | one line shown above the big playback clock, e.g. a station name; empty = hidden |
| `playback` | `repeat` | no | `off` / `all` (default) / `one` |
| | `shuffle` | no | `true` / `false` (default `false`) |
| `qrcode` | `link` | no | "custom link" QR — shown only when it is an `http(s)` address |
| | `label` | no | text on the custom link capsule; defaults to the URL's host |
| `audioN` | `link` | **yes** | section is skipped when missing or empty |
| | `title`, `artist` | no | shown as `♪ artist - title ♪`; `title` falls back to the file name |
| | `note` | no | per-track override of `[note] text` |
| | `pic` | no | local path **or** `http(s)` URL (downloaded in the background) |
| | `lrc` | no | local `.lrc` path, **or** `netease:<keyword>` / `netease:<song id>` / `qq:<keyword>` / `qq:<songmid>` to fetch a timed LRC online; auto-discovers a sibling `.lrc` when omitted |
| | `lrc_offset` | no | milliseconds to shift the whole lyric timeline (positive = lyrics later); adjust live with `[` / `]` when a video has an intro |
| | `page` | no | platform page URL — becomes the track QR; a local track without `page` shows no track QR |
| | `platform` | no | `bilibili` / `applemusic` / `youtube` / `spotify` / `soundcloud` / `netease` / `bandcamp`; by default detected from `page` |

Everything falls back to a built-in default, so a missing or broken `src/config.ini` still starts the app.

Playback strictly follows the playlist: `Space` walks `[audio1]`, `[audio2]` … and plays the first usable entry,
`N` / `P` move through it, and when a track ends the next one starts automatically. When nothing is usable the
reason is logged per track (`not found` / `remote not implemented yet` / miniaudio's error) instead of silently
picking something else — there is no hidden fallback track.

Paths are read as UTF-8, so non-ASCII names such as `src/assets/耳朵-李荣浩.mp3` work. On Windows miniaudio's
narrow `char *` API interprets paths with the ANSI code page, so [`src/core/media.c`](src/core/media.c)
converts to UTF-16 and calls the `_w` variants (`ma_decoder_init_file_w`).

## Project layout

```
src/core/main.c      the whole stage: window, keyboard, cairo render(), F1 help page
src/core/config.c/.h INI parsing → APPconfig / AudioTrack playlist (title/artist/page/lrc)
src/core/media.c/.h  ma_device output + ffmpeg decode on a worker thread, SPSC ring buffer
src/core/bilibili.c/.h  Bilibili page URL → DASH audio URL (view + playurl APIs)
src/core/source.c/.h    link → playable URL + headers (+ resolved title/artist)
src/core/lyrics.c/.h    LRC parser + current-line lookup
src/core/platform.c/.h  platform detection + Simple Icons SVG rendering (librsvg)
src/icons/              brand SVGs from Simple Icons (CC0)
src/core/palette.c/.h   seed colors from the cover (hue histogram) for the blurred background
src/core/stats.c/.h     per-day listening seconds → bottom-left heatmap
src/core/cache.c/.h     shared disk cache dir (`<user cache>/follow`) for lyrics / covers / metadata
src/core/texture.c/.h   GdkTexture → RGBA pixels / pre-scaled cairo surface
src/core/qrcode.c/.h libqrencode (main.c draws the modules itself)
src/core/http.c/.h   libcurl GET (with custom headers) for the Bilibili API
src/style/style.css  only the GTK chrome is left: window background + the thin header bar
src/models/          vendored miniaudio.h
src/assets/          bundled audio + placeholder cover
```

## Roadmap

- [x] Remote audio: ffmpeg + Bilibili API (page URLs, `b23.tv` short links, direct links, HLS)
- [x] Cover art: local file → `GdkTexture`
- [x] Keyboard-only UI with a help page
- [x] Lyrics: LRC with a single subtitle line under the title
- [x] Cover art from a remote URL (background download + URL normalisation)
- [x] Online lyrics: NetEase / QQ Music with real timestamps (cached locally)
- [ ] More extractors (YouTube / SoundCloud) — only Bilibili has an API resolver today
- [x] Repeat (off/all/one) and shuffle playback
- [ ] Playlist page (Sowon2 has one with undo/redo) instead of one-track-at-a-time keys
- [ ] Persist window geometry back into `config.ini` on exit (`config_save_default` is only a template writer today)

## License

Not decided yet.
