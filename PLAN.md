# PLAN

> 由 `plan` 工具维护，请勿直接编辑。步骤级进度以 `todo` 工具为准；决策与踩坑写入 DECISIONS.md。

<!-- plan-bridge:state -->
current: 
auto-breakpoint: false
<!-- /plan-bridge:state -->

## Milestones

- [x] M1 — 2K 60fps：GPU 墙 + GPU 背景（带回退）
- [x] M2 — 命令面板 / 音量 / 输出设备 / 外框（已完成）
- [x] M3 — 性能检测层 + 修掉 fx_lyric 常驻重绘（详见 #21）

## Current

goal: 性能检测层（F12）+ fx_lyric 重绘修复
breakpoint: 已实现并实机验证；待提交，后续优化见 TODO P1
