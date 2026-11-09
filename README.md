# Follow

一个 GTK4 的极简音乐播放器，版面复刻 [Sowon2](https://github.com/tsoding/hue_stretch)
（Tsoding 用 Jai + raylib 写的 "starting soon" 播放器）：近黑舞台、左上巨大的手写时钟、
时钟下面一行歌词，右列是封面（带进度条）/ `♪ 艺人 - 曲名 ♪` / 二维码 + 平台徽标。
本地和网络音频都能放，全键盘操作。

```
  Follow                      ← [note] / 曲目 note=：时钟上方那一行，左对齐
  00:00:26                    ← 播放位置，固定 00:00:00 位数，宽度不跳
  ┌ 披上伪装 开始秘密行动 ┐      ← 当前歌词：深色衬底 + 自动换行（最多 3 行）
  └──────────────────────┘

                    ┌─────────────┐  ← 封面 + 深色衬底
                    │    cover    │
                    │  ────────   │  ← 进度条：封面下方 5px、和封面同宽，不显示总时长
                    └─────────────┘
                    ♪ 李荣浩 - 耳朵 ♪
                        ▛▀▜  QR
                       [BILIBILI]   ← 胶囊也有深色衬底

  LRC -7500ms                 ← 只在非默认状态出现（循环 / 随机 / 歌词偏移）
  ┌ 22:41  LISTENED 3h 20m · TODAY 12m ┐  ← 左下角统计，同样有衬底
  │ ▦▦▦▦▦▦▦▦▦▦▦▦ 12 周热力图           │
  └────────────────────────────────────┘
```

## 版面是怎么算的

所有位置和字号都在 [`src/ui/layout.c`](src/ui/layout.c) 里，比例直接取自 1920×1080 的
参考截图（大时钟 ≈ 0.15×高、封面 0.267×高、二维码 0.18×高、右列中心 62% 宽……），
改那一个文件就等于调整整屏观感：

- 时钟太长会自动缩小字号，永远压不到右列
- 窗口太矮时右列整块等比缩小；两个二维码并排时列宽跟着变宽（二维码本身不变小）
- 封面 / 歌词 / 胶囊 / 统计都画在同一套深色圆角衬底上，花背景上也不影响可读性；
  歌词衬底 ≈ 内容宽度的 1.5 倍，左下角统计 ≈ 1.22 倍，左右留白对称
- 胶囊高度固定（不再跟着内容自适应缩放），两个并排时一模一样高
- 歌词按宽度自动换行，最多 3 行；放不下才省略

## 功能

- **即时模式 UI**：GTK 只提供窗口 / 键盘 / 音频，整屏由 [`src/ui/stage.c`](src/ui/stage.c)
  的 `stage_render()` 用 cairo 画出来。没有逐帧动画，只在「秒数 / 歌词行 / 状态 / 尺寸」变化时重画
- **动态背景**：封面模糊打底 + 三团从封面取的种子色缓慢漂移 + 压暗。每帧只画一张
  约 256px 宽的纹理，交给 `GtkPicture` 由 GTK 的 GL 渲染器放大到整屏 —— CPU 每帧 < 1ms
- **过渡（退潮 / 撤桌重上）**：换封面时旧背景整块左移退出，右边缘带一条渐隐带滑走，
  新背景从底下露出来（潮水退去露出海底），种子色同时插值；
  换曲目时整屏组件"撤桌重上"：旧画面左移淡出、新画面从右侧推入。
  两层**故意不同步**：背景 1.10s 立刻起步，组件 0.85s 且晚 0.15s 才动
  （`fx_begin` / `fx_begin_later`，都是 ease-out-cubic），所以看起来是背景先退潮、
  桌面随后被撤走换上新的，不会像一整块搬走那么死板。
  只有过渡期间才跑 16ms 帧循环，其余时间仍然只在内容变化时重画
- **两种字体**：界面文字是 Iosevka（拉丁 / 数字）+ Noto Sans SC（中文），两个都是开源字体；
  只有时间用手写体（大时钟、左下角墙钟）。`src/ui/theme.h` 里的家族列表用逗号分隔且
  **不能带空格** —— Pango 不 trim，带了空格中文就会变豆腐块
- **网络音频**：FFmpeg 解码，所以本地文件、`http(s)` 直链、HLS 都能放；Bilibili *页面*
  链接（含 `b23.tv` 短链）会自动解析成 DASH 音频流并带上 Referer
- **不阻塞音频线程**：设备回调只 *尝试* 拿解码锁，网络 I/O 和 FFmpeg 都在解码线程，
  音频线程只从 SPSC 环形缓冲里搬样本
- **封面**：本地文件或 `http(s)` 地址（后台线程下载 + 缓存，B 站图床的尺寸后缀会被规范化）
- **LRC 歌词**：`[mm:ss.xx]`、`[offset:]`、一行多时间戳、行内 `<mm:ss.xx>`；非 UTF-8
  自动按 GB18030 转；没写 `lrc=` 就在音频旁边找同名 `.lrc`
- **在线歌词源**：`lrc=netease:李荣浩 耳朵`、`lrc=qq:186016` 之类会在后台线程取回带时间轴
  的 LRC 并缓存，重复播放不再联网；取回来还会诊断「音频和歌词时长差太多」并提示换源
- **歌词偏移**：`lrc_offset` 固定，`[` / `]` 边听边调 250ms，`\` 复原（视频带片头时用）。
  写毫秒或秒都行：`2500` / `2500ms` / `2.5s` 都是 2.5 秒（没有后缀时 |值| ≥ 100 当毫秒，
  否则当秒 —— 所以手写 `-7.5` 也能用）
- **二维码跟着链接类型走**：本地曲目不显示曲目二维码；网络曲目显示一张（下面是平台徽标，
  用的是各家**官方字标**：BILIBILI / APPLE MUSIC / YOUTUBE / SPOTIFY / SOUNDCLOUD / BANDCAMP ——
  品牌名不再自己用字体排，`src/icons/wordmark-<slug>.svg` 就是官方 logo 文件；
  网易云音乐和自定义链接没有字标，才退回"品牌色图标 + 文字"）；
  `[qrcode] link` 是网络地址就再并排一张（下面是地址）
- **播放**：播放 / 暂停 / 停止 / seek / 上一首下一首 / 循环（off·all·one）/ 随机（带历史，
  所以"上一首"能回到真的上一首）/ 自动续播
- **左下角统计**：墙钟、累计和今日收听时长、12 周热力图（颜色取自封面），
  按天存在 `<cache>/follow/stats.dat`（纯文本 `YYYY-MM-DD 秒数`），每 30 秒和退出时落盘

## 快捷键

| 按键 | 作用 |
|---|---|
| `Space` | 播放 / 暂停（还没放歌时从第一首开始） |
| `←` / `→` | 后退 / 前进 5 秒（按住 `Shift` = 30 秒） |
| `N` / `P` | 下一首 / 上一首 |
| `R` | 当前曲目从头重放 |
| `S` | 停止 |
| `L` | 循环：关 → 列表 → 单曲 |
| `H` | 随机播放开关 |
| `[` / `]` | 歌词整体提前 / 延后 250 ms |
| `\` | 歌词偏移恢复成 ini 里的值 |
| `F` / `F11` | 全屏（全屏时没有标题栏，就是 Sowon2 那个观感） |
| `F1` | 快捷键页 |
| `Q` | 退出 |
| `Esc` | 关掉快捷键页 / 退出全屏 |

## 依赖

| 库 | 用途 |
|---|---|
| GTK 4 | 窗口、图片 / 纹理、CSS |
| miniaudio | 音频输出设备（header-only，已内置 `src/models/miniaudio.h`） |
| FFmpeg (`avformat`/`avcodec`/`swresample`) | 解码本地文件和网络流 |
| libqrencode | 生成二维码 |
| libcurl | HTTP(S)，Bilibili API |
| JSON-GLib | 解析 Bilibili 接口 |
| librsvg | 把品牌 SVG 渲染成 cairo surface |
| GLib / GObject | 配置（GKeyFile）、线程、内存管理 |

MSYS2 / MinGW-w64：

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk4 \
          mingw-w64-x86_64-libqrencode mingw-w64-x86_64-curl
```

Debian / Ubuntu：

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libqrencode-dev \
     libcurl4-openssl-dev libjson-glib-dev librsvg2-dev \
     libavformat-dev libavcodec-dev libswresample-dev libavutil-dev
```

## 构建与运行

```sh
make          # 目标文件在 build/，产物是 ./main.exe
./main.exe    # 必须在项目根目录跑：资源路径是相对的
```

```
src/style/style.css   只剩 GTK 外壳（窗口底色 + 极细的标题栏）
src/config.ini        配置（手写，见下）
src/assets/*          配置指向的音频 / 封面
```

`make clean` 清掉 `build/` 和 `main.exe`。

启动日志（`g_message`，不需要额外环境变量）：

```
** Message: config: loading src/config.ini (cwd = F:\...)
** Message: config: 1 track(s), window 800x600
** Message: track[0] audio1  李荣浩 - 耳朵  link=src/assets/耳朵-李荣浩.mp3  page=-  lrc=netease:李荣浩 耳朵
** Message: lyrics: 11 lines (netease:李荣浩 耳朵)
** Message: media: opened src/assets/耳朵-李荣浩.mp3 — mp3float, 44100 Hz, 2 ch, 241.3s
** Message: play: track[0] audio1 -> src/assets/耳朵-李荣浩.mp3
```

`G_MESSAGES_DEBUG=all` 可以看到更细的 DEBUG 输出。

## 配置

`src/config.ini` 是 [GKeyFile](https://docs.gtk.org/glib/struct.KeyFile.html) 格式，
`[audioN]` 按 N 排序组成播放列表。

```ini
[window]
width=1920
height=1080
fullscreen=false
title=Follow

[qrcode]
link=https://live.bilibili.com/1840990819
#label=MY LINK          # 胶囊上的文字，默认取主机名

[note]
text=Follow              # 时钟下面那行大字，可被单曲 note= 覆盖

[playback]
repeat=all               # off / all / one
shuffle=false

[audio1]
link=src/assets/耳朵-李荣浩.mp3   # 本地路径或 http(s) URL，UTF-8
title=耳朵                        # 可省，默认取文件名
artist=李荣浩
pic=src/assets/耳朵.jpg           # 封面：本地路径或 URL
lrc=netease:李荣浩 耳朵            # 本地 .lrc / netease:... / qq:...
#lrc_offset=-7.5                  # 歌词整体偏移（秒或毫秒，正数 = 歌词延后）
#page=https://www.bilibili.com/video/BV1xx411c7mD   # 二维码目标 + 平台徽标
#platform=bilibili                # 可省，默认按 page 域名识别
#note=STAFF CREDITS               # 覆盖 [note] text

[audio2]
link=https://www.bilibili.com/video/BV1GJ411x7h7    # 页面链接会自动解析
```

| 组 | 键 | 说明 |
|---|---|---|
| `window` | `width` / `height` / `fullscreen` / `title` | 缺省 800×600、窗口模式、`Follow` |
| `note` | `text` | 时钟下面的副标题；空则这一行让给歌词 |
| `playback` | `repeat` / `shuffle` | `off`/`all`（默认）/`one`，随机默认关 |
| `qrcode` | `link` / `label` | 只对 `http(s)` 地址显示；`label` 默认是主机名 |
| `audioN` | `link` | **必填**，空的组会被跳过 |
| | `title` / `artist` / `note` / `pic` / `lrc` / `lrc_offset` / `page` / `platform` | 见上 |

任何一项缺失都回落到内置默认值，所以 ini 写坏了也能启动。

路径按 UTF-8 读，所以 `耳朵-李荣浩.mp3` 这种文件名没问题（miniaudio 在 Windows 上走的是
`_w` 变体）。

## 目录结构

```
src/main.c              生命周期 + 键盘 + 100ms 定时器（唯一的胶水层）
src/core/               与 UI 无关的部分
  config.c               INI → APPconfig / AudioTrack 播放列表
  media.c                miniaudio 输出 + FFmpeg 解码线程 + SPSC 环形缓冲
  source.c / bilibili.c  link → 可播放的 URL + 请求头（B 站页面链接解析）
  lyrics.c               LRC 解析（时间轴 → 当前行）
  lyricsrc.c / http.c    在线歌词源（网易云 / QQ）、libcurl 封装
  palette.c              从封面提取种子色（色相直方图）
  stats.c                按天累计收听秒数
  cache.c / texture.c    磁盘缓存目录、RGBA → cairo surface / 盒式模糊
  platform.c             平台识别 + 品牌图标（src/icons 里的 Simple Icons SVG）
src/ui/                 画面
  ui.h                   App 全局状态（整个程序只有一份）
  theme.c                字体 / 颜色 / 全部文字绘制原语
  layout.c               一次绘制的全部几何量（比例照截图量）
  stage.c                主绘制：左列时钟 / 副标题 / 歌词 / 进度，右列封面 / 曲名 / 二维码，左下角统计
  cover.c                封面加载（本地 / 后台下载 + 缓存）与绘制
  backdrop.c             动态背景（模糊封面 + 漂移渐变 → 小纹理）+ 退潮过渡
  fx.c                   过渡进度（开始时间 + 时长 → ease-out-cubic，可随时打断）
  qrcard.c               二维码槽 + 平台胶囊
  help.c                 F1 快捷键页
  player.c               播放控制、随机 / 循环、网络解析、曲目显示名
  lyricview.c            歌词加载（本地 / 在线）、当前行、偏移
src/style/style.css      GTK 外壳样式
src/models/              内置的 miniaudio.h
src/assets/              示例音频 + 封面
```

## Roadmap

- [x] 网络音频：FFmpeg + Bilibili API（页面链接、`b23.tv`、直链、HLS）
- [x] 封面、歌词（本地 + 在线）与二维码
- [x] 循环 / 随机 / 自动续播
- [ ] 更多音源解析（YouTube / SoundCloud）—— 目前只有 Bilibili
- [ ] 播放列表页（Sowon2 那种带撤销 / 重做的）
- [ ] 退出时把窗口尺寸写回 `config.ini`

## License

尚未决定。
