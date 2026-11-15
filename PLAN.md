# PLAN

> 由 `plan` 工具维护，请勿直接编辑。步骤级进度以 `todo` 工具为准；决策与踩坑写入 DECISIONS.md。

<!-- plan-bridge:state -->
current: M4
auto-breakpoint: false
<!-- /plan-bridge:state -->

## Milestones

- [x] M1 — 2K 60fps：GPU 墙 + GPU 背景（带回退）
- [x] M2 — 命令面板 / 音量 / 输出设备 / 外框（已完成）
- [x] M3 — 性能检测层 + 修掉 fx_lyric 常驻重绘（详见 #21）
- [ ] M4 — 主画布换纹理节点，消掉每帧 cairo 重生（详见 #22）
- [x] M5 — 构建/打包迁 CMake + CPack（详见 #1）
- [x] M6 — 分层重构：core/net/music/ui 静态库（#1）
- [x] M7 — 元数据后端与 SQLite 缓存（#2）
- [x] M8 — 歌曲详情页与首次指认（#3）

## Current: M4 — 主画布换纹理节点，消掉每帧 cairo 重生（详见 #22）

goal: 主画布纹理化，消掉每帧 cairo 重生
breakpoint: v0.1.1 已发布；follow.exe 命名与图标已完成
