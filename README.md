# Follow

A tiny GTK4 desktop media player: cover art + QR code + playback controls, configured entirely from a plain INI file.

> Status: early development. Local audio playback, QR code rendering and the playlist config parser work; **remote audio / cover / lyrics loading is not implemented yet** (see [Roadmap](#roadmap)).

## Features

- GTK4 UI (header bar, custom CSS theme, flat dark style)
- Local audio playback through [miniaudio](https://miniaud.io/) — play / pause / resume / stop / seek
- Draggable progress bar with a custom, handle-less scale style ([`src/style/style.css`](src/style/style.css))
- QR code rendered from a configurable link ([libqrencode](https://fukuchi.org/works/qrencode/) → `GdkTexture`)
- INI based config: window geometry, QR code link, and an arbitrary number of `[audioN]` playlist entries
- libcurl wrapper ([`src/core/http.c`](src/core/http.c)) ready for the upcoming network features

## Dependencies

| Library | Purpose |
|---|---|
| GTK 4 | window, widgets, image/texture loading |
| miniaudio | audio output + decoding (header-only, vendored as `src/models/miniaudio.h`) |
| libqrencode | QR code generation |
| libcurl | HTTP(S) downloads |
| GLib / GObject | config parsing (`GKeyFile`), memory helpers |

### Install (MSYS2 / MinGW-w64)

```sh
pacman -S mingw-w64-x86_64-toolchain \
          mingw-w64-x86_64-gtk4 \
          mingw-w64-x86_64-libqrencode \
          mingw-w64-x86_64-curl
```

### Install (Debian / Ubuntu)

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libqrencode-dev libcurl4-openssl-dev
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
src/assets/bgm.mp3     fallback audio file
```

Debug logs (parsed playlist, skipped remote tracks…):

```sh
G_MESSAGES_DEBUG=all ./main.exe
```

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
link=src/assets/bgm.mp3      # audio file: local path or http(s) URL
#pic=F:/pics/cover.jpg       # cover art: local path or URL
#lrc=F:/lyrics/bgm.lrc       # lyrics: local path or URL

[audio2]
link=https://example.com/song.mp3
pic=https://example.com/cover.jpg
lrc=https://example.com/song.lrc
```

| Section | Key | Required | Notes |
|---|---|---|---|
| `window` | `width`, `height` | no | fallback `800×600`, non-positive values are ignored |
| | `fullscreen` | no | `true` / `false`, default `false` |
| | `title` | no | default `Follow` |
| `qrcode` | `link` | no | default project URL |
| `audioN` | `link` | **yes** | section is skipped when missing or empty |
| | `pic` | no | may be omitted or left empty |
| | `lrc` | no | may be omitted or left empty |

Everything falls back to a built-in default, so a missing or broken `src/config.ini` still starts the app.

## Project layout

```
src/core/main.c      GTK application, UI wiring, playback controls, tick timer
src/core/config.c/.h INI parsing → APPconfig / AudioTrack playlist
src/core/media.c/.h  miniaudio wrapper (play, pause, stop, seek, position, duration)
src/core/qrcode.c/.h libqrencode → GdkTexture
src/core/http.c/.h   libcurl synchronous GET (not used by the UI yet)
src/style/style.css  theme: window, header bar, window controls, scale
src/models/          vendored miniaudio.h
src/assets/          bundled audio
```

## Roadmap

- [ ] Remote audio: download with libcurl, then feed miniaudio
- [ ] Cover art: local file + remote URL → `GdkTexture`
- [ ] LRC lyrics: parse `[mm:ss.xx]` timestamps, highlight the current line
- [ ] Track switching UI (prev / next / play list)
- [ ] Persist window geometry back into `config.ini` on exit (`config_save_default` is only a template writer today)

## License

Not decided yet.
