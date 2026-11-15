# DECISIONS

> 决策与踩坑日志，只追加，不参与日常上下文。可用 grep 检索。条目按文件顺序编号（第一个 `##` 是 #1）。

## 2026-11-15 — 分层重构：静态库 + 公开头文件

把源码拆成 core / net / music / ui 四个独立静态库 + app 薄层，依赖单向 app→ui→music→net→core。目录统一 <module>/{include,src}：include 是模块公开头，src 是实现；CMake 里 follow_module() 自动加 include 目录、依赖和 -Wall -Wextra，并用 follow::<module> ALIAS。include 保持扁平（"config.h"），所以搬文件不用改旧 include。umbrella 头在 src/include/follow/（follow.h 以及每层一个）。media.h 自己 include miniaudio.h，因此 src/models 的 SYSTEM include 必须是 PUBLIC 才能传播到 ui。单独编译：cmake --build build --target follow_core/net/music/ui。新增功能按层放置，参考 details.h + metadata.h 这条链路。

## 2026-11-15 — MusicBrainz + SQLite 元数据后端

数据源 MusicBrainz WS/2 + Cover Art Archive。曲目 key 优先 page，其次 link，最后 title|artist，所以重排 [audioN] 不会丢映射。流程：metadata_load_track → 本地 meta 命中直接 READY；否则候选缓存 → PICK（离线可指认）；否则后台搜索。搜索/详情/封面都在 g_thread 里跑，用 gen 丢弃过期结果。完成结果不走 g_idle_add：实测详情页打开后 GTK 会留下长期 ready 的默认优先级 source，DEFAULT_IDLE 被饿死（定时器还在跑），所以工作线程把结果放进 GMutex+GQueue，由 metadata_init 注册的 60ms 定时器在 UI 线程取。MB 限速 1 req/s，客户端节流 1.3s；MB 会随机回 503（实测约 1/5），对 502/503/504 做 3 次退避重试。封面单独一个 job 下载（CAA 会 307 到 archive.org，慢的时候十几秒，不能挡住 READY），完成后回写 DB 并 bump version。SQLite 表：meta（完整记录+cover_url/cover_path）与 candidates（track_key+mbid 主键）。

## 2026-11-15 — 歌曲详情页与封面优先级

details.c 整屏 cairo 页面，风格复用 theme.h 的字体/卡片/种子色；stage_render 在 details_is_open() 时整屏接管，ui_sync_layers 隐藏歌词 GL 层。按键：i 打开/关闭，Esc 关闭，PICK 时 ↑↓/Home/End 选、Enter 确认、R 重搜，READY 时 R 重新识别。字段：Recording MBID、Release MBID、专辑、艺人、专辑艺人、发行时间、国家、厂牌、目录号、条码、曲序、时长、标签、来源。封面优先级：config.ini 的 pic 永远优先；没写 pic 且 [metadata] cover=true 时用数据库封面（metadata_cover_path），cover_set 在无 pic 时立即尝试，元数据稍后就绪由 on_tick 的 version 变化触发 cover_apply_metadata。首次识别只有单一候选时自动选中，多候选必须用户指认；[metadata] enabled=false 完全不联网。新增 KEY_DETAILS 默认 "i"，帮助页、config.ini、README 同步。

## 2026-11-15 — 首次识别始终由用户确认

最初的实现是「搜索只返回 1 个候选就自动选中」，但需求是「第一次需要用户指认」，且搜索条件过宽时单候选也可能是错的。改为第一次搜索后一律进入 PICK 页：即使只有 1 个候选也要用户看一眼再按 Enter。候选列表同时落库，离线也能完成这次指认；确认后才写 meta 表。

## 2026-11-15 — 注释风格：只保留关键技术约束

全项目注释统一为简体技术说明：删除口语、历史叙述、实测数字、情绪化描述和重复代码含义的注释；保留文件头（含免责声明）、模块职责、线程 / 内存所有权、单位与取值范围、平台与协议限制、以及容易踩错的实现约束。section 分隔条、枚举值逐项解释、显而易见的字段注释全部移除。C 注释块数量从约 2470 行降到约 360 个块；CMake 与 Rust 调试工具同步精简。

## 2026-11-15 — 许可证选型与 CI

最终选择 GPL-3.0-or-later。原因：MSYS2 的 FFmpeg 使用 --enable-gpl --enable-version3 构建，PolyForm Noncommercial 的非商业限制与 GPLv3 不兼容；GPL-3.0 同时兼容 GTK / librsvg / json-glib 的 LGPL、libcurl 的 MIT 类许可、SQLite 的公有领域以及 miniaudio 的公有领域。LICENSE 使用 FSF 的 GPLv3 全文并附版权声明；源码文件头统一加 SPDX-License-Identifier: GPL-3.0-or-later；README 增加许可证章节并删除与 GPL 冲突的“禁止商用”表述。CI 使用 GitHub Actions + MSYS2 MINGW64 构建，并上传包含 DLL / 资源的 dist 目录。

## 2026-11-15 — Linux CI 方案

Linux CI 采用原生 ubuntu-24.04 runner + apt 依赖，而不是容器或自建 GTK。核对后 GtkWindowHandle 自 GTK 3.98.5 就存在（此前误以为需要 4.18），Ubuntu 24.04 的 GTK 4.14 足够覆盖项目使用的 API；FFmpeg 6.1 也包含代码使用的 AVChannelLayout / swr_alloc_set_opts2。CMake 在 UNIX 上补充 m/dl/pthread 链接。CI 增加 linux job：apt 安装 build-essential/cmake/ninja/pkg-config/gtk4/gdk-pixbuf/epoxy/qrencode/curl/json-glib/sqlite/librsvg/ffmpeg 开发包，Release 构建。若未来用到 GTK 4.16+ API，再切换到 Arch 容器即可。

## 2026-11-15 — Release workflow 与版本号

Release 由 .github/workflows/release.yml 负责，触发条件为 push v* tag 或手动 workflow_dispatch（输入版本）。版本号通过 -DFOLLOW_VERSION=<tag去掉v> 传给 CMake，project(VERSION) 与 CPack 文件名同步。Windows job 生成 NSIS 安装包（build/Follow-<ver>-win64.exe）和便携 zip（Follow/<ver>/main.exe + src + DLL）；Linux job 在 Ubuntu 24.04 构建后生成 Follow-<ver>-linux-x86_64.tar.gz（main + src + LICENSE + README + follow 启动脚本）。publish job 下载两个 artifact，用 softprops/action-gh-release 直接创建公开 release，generate_release_notes=true，正文附安装说明。普通 build workflow 只在分支 push/PR 运行，避免 tag 时重复构建。

## 2026-11-15 — 通用配置与个人配置隔离

仓库只保留通用模板 src/config.ini（无个人曲目、无个人链接，全部 [audioN] 以注释示例给出）。个人配置放在 src/config.local.ini，已加入 .gitignore；程序启动时按 FOLLOW_CONFIG > src/config.local.ini > src/config.ini 的顺序选择配置，命令面板的 Open Config File / Reload config.ini 也使用当前生效的路径。CMake install 只安装 src/config.ini，因此 NSIS、portable zip、Linux tar 与源码归档都不会包含个人配置。已验证：本地配置 12 首、通用模板 0 首；安装目录只有 src/config.ini，无 config.local.ini。
