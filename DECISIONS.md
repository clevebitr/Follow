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
