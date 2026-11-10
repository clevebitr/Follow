<div align="center">

<img src="src/icons/logo.svg" width="104" alt="Follow">

# Follow

**一个把「正在听什么」摆到屏幕上给观众看的极简音乐播放器。**

</div>

<img src="gallery/app.png" alt="Follow_app">


## 功能

### 播放

- 本地文件、`http(s)` 直链、HLS 都能放（FFmpeg 解码）
- **Bilibili / YouTube 链接直接粘**：`BV` 号、`av` 号、`b23.tv` 短链、`youtu.be`、
  `watch?v=` / `shorts` / `music.youtube.com` 都会自动解析出音频轨（YouTube 需要代理）
- 播放 / 暂停 / 停止 / 拖动进度 / 上一首下一首
- 循环（关 · 列表 · 单曲）、随机（带历史，「上一首」真的能回去）、自动续播
- 全键盘操作，没有按钮也没有菜单

### 画面

- **动态背景**：封面模糊打底 + 从封面里取的三团种子色缓慢漂移，随换曲一起换色
- **换曲过渡**：旧背景「退潮」左移滑走，新组件「撤桌重上」从右侧推入；两层故意不同步，看起来像两次动作而不是整块搬走
- **自适应版面**：字号和位置全部按窗口尺寸算，1920×1080 到小窗口、窗口宽度窄到两栏要撞时都会自动收，时钟太长会自己缩字号
- 两种字体：界面是 Iosevka + Noto Sans SC，只有时间用手写体

### 歌词

- 标准 LRC：`[mm:ss.xx]`、`[offset:]`、一行多时间戳、行内 `<mm:ss.xx>`
- 自动识别编码（GB18030 的老 LRC 也能读）；没写 `lrc=` 就在音频旁边找同名 `.lrc`
- **在线歌词**：`lrc=netease:李荣浩 耳朵` / `lrc=qq:186016`，取回带时间轴的歌词并缓存，重复播放不再联网
- 取回后还会检查「音频和歌词时长差太多」并提示换源
- **歌词偏移**：`[` / `]` 边听边调 250 ms，`\` 复原 —— 视频带片头的情况很好用

### 二维码

- 网络曲目自动生成一张，下面挂**各家官方字标**（BILIBILI / APPLE MUSIC / YOUTUBE / SPOTIFY / SOUNDCLOUD / BANDCAMP）
- 本地曲目不显示；`[qrcode] link` 是网络地址就再并排一张（扫码进直播间 / 个人主页）
- 两个二维码并排时同一个尺寸，胶囊高度一致

### 左下角统计

- 墙钟、累计收听时长、今日收听时长
- 近 12 周热力图，颜色取自当前封面
- 按天记在 `<cache>/follow/stats.dat`，纯文本，随便改

### 快捷键

| 按键 | 作用 |
|---|---|
| `Space` | 播放 / 暂停（还没放歌时从第一首开始） |
| `←` / `→` | 后退 / 前进 5 秒（按住 `Shift` = 30 秒） |
| `N` / `P` | 下一首 / 上一首 |
| `R` | 当前曲目从头重放 |
| `S` | 停止 |
| `L` | 循环：关 → 列表 → 单曲 |
| `H` | 随机播放开关 |
| `[` / `]` | 歌词提前 / 延后 250 ms |
| `\` | 歌词偏移恢复成 ini 里的值 |
| `F` / `F11` | 全屏（没有标题栏，就是参考布局那个观感） |
| `F1` | 快捷键页 |
| `Q` | 退出 |
| `Esc` | 关掉快捷键页 / 退出全屏 |

## 开始使用

需要 GTK 4、FFmpeg、libcurl、JSON-GLib、libqrencode、librsvg，以及一个 C 编译器。

```sh
# MSYS2 / MinGW-w64
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk4 \
          mingw-w64-x86_64-libqrencode mingw-w64-x86_64-curl

# Debian / Ubuntu
sudo apt install build-essential pkg-config libgtk-4-dev libqrencode-dev \
     libcurl4-openssl-dev libjson-glib-dev librsvg2-dev \
     libavformat-dev libavcodec-dev libswresample-dev libavutil-dev
```

```sh
make          # 产物是 ./main.exe
./main.exe    # 在项目根目录跑：资源路径是相对的
```

配置文件是 `src/config.ini`（[GKeyFile](https://docs.gtk.org/glib/struct.KeyFile.html) 格式，手写），改完直接重启：

```ini
[window]
width=1920
height=1080
title=Follow

[note]
text=Follow                       # 时钟上方那行副标题，可被单曲 note= 覆盖

[qrcode]
link=https://live.bilibili.com/1840990819

[cache]
#dir=cache                        # 缓存目录，默认 <用户缓存>/follow；支持 ~ 和相对路径

[net]
#user_agent=Mozilla/5.0 ...       # 留空就用内置的浏览器 UA
#timeout=15                       # 整次请求超时（秒）
#connect_timeout=5                # 建连超时（秒）
#proxy=http://127.0.0.1:7890      # 代理：YouTube 必填，curl 和 FFmpeg 都走它

[audio1]
link=src/assets/xxx.mp3     # 本地路径或 http(s) URL
title=耳朵
artist=李荣浩
pic=src/assets/耳朵.jpg            # 封面：本地路径或 URL
lrc=netease:李荣浩 耳朵            # 本地 .lrc / netease:... / qq:...
#lrc_offset=-7.5                  # 歌词整体偏移（正数 = 歌词延后）
#page=https://www.bilibili.com/video/BV1xx411c7mD   # 二维码目标 + 平台徽标
#note=STAFF CREDITS               # 覆盖 [note] text

[audio2]
link=https://www.bilibili.com/video/BV1GJ411x7h7    # B 站页面链接会自动解析

[audio3]
link=https://www.youtube.com/watch?v=YudHcBIxlYw    # YouTube 页面链接同样会解析
#page=                                             # 二维码目标，默认空则不显示
```

> YouTube 那一路走的是 InnerTube 接口，**国内直连不可用**，需要先在 `[net] proxy` 里填代理。
> 另外 YouTube 会按出口 IP 判人机验证：机房 IP / 公共代理常被挡（界面上会提示
> 「YouTube 要人机验证」），换成住宅 IP 或换个节点即可。
>
> 播放这一路也不走 FFmpeg 自带的 http：googlevideo 现在只接受**定长 Range** 请求，
> FFmpeg 默认发的开放式 `Range: bytes=0-` 会被 403。所以 YouTube 音源改由
> `src/core/httpstream.c` 用 libcurl 按 2MB 分块拉流再喂给解码器，seek 也仍然正确。

`[audioN]` 按 N 排序就是播放列表，任何一项缺失都会回落到默认值，ini 写坏了也能启动。完整的键说明见下方「配置项」。

## 技术栈

一句话：**GTK 4 只负责窗口和输入，整屏画面自己用 cairo 画；音频用 miniaudio 输出、FFmpeg 解码。**

| C + GLib | 全部代码 |
|---|---|
| GTK 4 | 窗口、GtkPicture 纹理放大、CSS |
| cairo + Pango | 整屏绘制与排版 |
| miniaudio | 音频输出设备（已内置，header-only） |
| FFmpeg | 本地 / 网络音频解码 |
| libcurl + JSON-GLib | 网络请求与接口解析（B 站 / YouTube / 歌词源）、YouTube 分块拉流 |
| libqrencode + librsvg | 二维码与品牌矢量图标 |

---

## 免责声明 / Disclaimer

**本项目仅供学习与技术研究使用。**

- 作者**不提供、不托管、不分发**任何音视频内容。播放的内容全部由使用者自己提供或指定，版权归各自权利人所有。
- `src/core/bilibili.c` 使用 B 站网页端的**非公开接口**并附带 Referer 头来获取音频流地址；
  `src/core/youtube.c` 调用 YouTube 的 InnerTube 接口并伪装成官方客户端；
  `src/core/lyricsrc.c` 同理访问网易云 / QQ 音乐。这些做法**可能违反对应平台的服务条款**，接口也可能随时失效。请仅用于个人学习，不要用于分发、转售或商业用途，也不要下载 / 保存 / 二次分发内容。
- 若需要合规使用，请走各平台官方开放接口，或直接使用你有权访问的音频直链（`http(s)` / HLS）——这条路径不涉及任何平台。
- 使用者须自行遵守所在地法律法规与平台条款，**因使用本项目产生的一切后果由使用者自行承担**。

> This project is for **learning and research only**. It hosts no media content. Third‑party
> services are accessed through undocumented endpoints and may violate their Terms of Service.
> Use at your own risk; the author takes no responsibility for any consequences.

## 配置项

| 组 | 键 | 说明 |
|---|---|---|
| `window` | `width` / `height` / `fullscreen` / `title` | 缺省 800×600、窗口模式、`Follow` |
| `note` | `text` | 时钟上面那行副标题；空则这一行让给歌词 |
| `playback` | `repeat` / `shuffle` | `off`/`all`（默认）/`one`，随机默认关 |
| `qrcode` | `link` / `label` | 只对 `http(s)` 地址显示；`label` 默认取主机名 |
| `cache` | `dir` | 歌词 / 封面 / B 站元数据 / 收听统计的落地目录，默认 `<用户缓存>/follow` |
| `net` | `user_agent` / `timeout` / `connect_timeout` / `proxy` | 网络默认值；UA 留空用内置浏览器 UA，超时单位秒，`proxy` 同时作用于 curl 与 FFmpeg |
| `audioN` | `link` | **必填**，空的组会被跳过 |
| | `title` / `artist` / `note` / `pic` / `lrc` / `lrc_offset` / `page` / `platform` | 同上 |

## 目录结构

```
src/core/    播放器内核：配置、解码与输出、音源解析、歌词、封面取色、收听统计
src/ui/      画面：版面计算、cairo 绘制、动态背景、二维码、快捷键页
src/icons/   品牌字标 + 本项目 logo
src/style/   GTK 外壳样式（窗口底色 + 极细的标题栏）
src/assets/  示例音频与封面（不入库，按 config.ini 放自己的文件）
src/models/  内置的 miniaudio.h
```

## Roadmap

- [x] 网络音频：FFmpeg + Bilibili / YouTube 解析（页面链接、`b23.tv`、直链、HLS）
- [x] 封面、歌词（本地 + 在线）、二维码、平台徽标
- [x] 循环 / 随机 / 自动续播、收听统计与热力图
- [ ] 更多音源解析（SoundCloud）
- [ ] 播放列表页（带撤销 / 重做）
- [ ] 退出时把窗口尺寸写回 `config.ini`

## License

代码尚未选定开源许可证。在许可证确定之前，**上面那段免责声明同样构成使用条件**：
你可以读、可以自己跑、可以学习和修改，但请自行承担风险，也不要把它当作任何平台内容的获取工具。
