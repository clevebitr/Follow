# TODO

## P0 — 本轮收尾

- [x] 把 `src/core/resolvecache.c` 接进 `src/core/source.c`
  - [x] B 站 / YouTube 解析前先 `resolvecache_load()`
  - [x] 解析成功后 `resolvecache_save()`
  - [x] 实现 `source_cache_invalidate()`
  - [x] 播放缓存直链失败时，`player.c` 失效缓存并自动重解析一次（只重试一次）
- [x] 配置解析补完
  - [x] `[cache] resolve_ttl`（默认 2700 秒，0 = 关闭）
  - [x] `[cache] cover_max_mb`（默认 128，已用 `cache_prune_prefix()` 清理）
  - [x] `[ui] animations`（已解析/使用）
  - [x] 把 `[cache]` / `[ui]` 写进 `src/config.ini` 注释
- [x] 启动时清理 `resolvecache` 过期文件（`resolvecache_prune()` 已接到启动）
- [x] 删除无用代码
  - [x] `covercache_scaled()` 和 `CoverEntry.scaled/scaled_px`（已不再使用）
  - [x] `config_audio_cache()` / `config_audio_max_bytes()` 暂未使用，保留并标记“预留”
- [x] 重新链接 `main.exe`
- [x] 更新 README
  - [x] 无限墙 / 自由拖动 / 滚轮 / 随机布局
  - [x] 进出场、选中磁贴、封面淡入、`[ui] animations`
  - [x] 解析缓存 TTL、封面缓存上限
- [x] 清理测试产物
  - [x] `build/follow-wall.exe`、`build/tmp/*` 测试脚本/截图
  - [x] 检查 `git diff --check`

## P1 — 硬件加速（优先解决墙卡顿）

- [x] 选型验证
  - [x] 方案 A：`GtkGLArea` + OpenGL textured quad
    - [x] 每首歌/每种尺寸用 cairo 预渲染成 ARGB32，再上传成 GL 纹理（按曲目 / 尺寸 / 封面版本缓存）
    - [x] 相机平移、选中缩放用 MVP / quad 变换；光晕用径向纹理 + 颜色 tint
  - [ ] 方案 B：`GtkSnapshot` + `GdkTexture` 节点（暂不采用，A 已满足，保留作备选）
    - [ ] 保留 cairo 生成磁贴纹理
    - [ ] 每帧只 push 纹理节点 + transform，让 GSK / GPU 合成
    - [ ] 改动比 GLArea 小，优先试这个
  - [x] 不建议依赖 `GSK_RENDERER=gl` 加速 cairo：它只加速 GTK/GSK 合成，不加速 `GtkDrawingArea` 里的 cairo 绘制
- [x] 从 `GtkDrawingArea` 迁出墙层：墙单独一层 GL，stage / chrome 仍用 cairo
- [x] 保留 cairo 作为纹理生成器和无 GPU fallback（`FOLLOW_WALLGL=0` 可强制回退）
- [x] 背景/压暗/叠色：背景用 glClear、压暗用单 quad，封面叠色烘进磁贴纹理
- [x] 帧时间统计（`FOLLOW_WALL_DEBUG=1` 时每秒打印帧数 / ms / 上传数）
- [x] 封面置色：Folia 式双径向柔光铺满整屏（含 SONGS 顶栏），focus 封面未加载时保持上一色
- [x] 磁贴间距按 pitch = cell + gap 计算，block 接缝不再漏掉 gap
- [ ] 低配模式
  - [x] `[ui] animations = false` 时直接 snap，不跑 60fps
  - [ ] 可选降低磁贴缓存分辨率 / 限制同屏数量

## P2 — 缓存

- [x] 封面磁盘缓存 + 上限清理（`cache_prune_prefix`）
- [x] 封面淡入 / 按尺寸缓存 / 磁贴内容缓存
- [ ] 封面缓存
  - [x] `[cache] cover_max_mb` 配置解析
  - [ ] 同一封面并发请求合并（`cover.c` 与 `covercache.c` 避免重复下载）
  - [x] 缓存损坏时自动删除重下
- [x] 解析结果缓存（已接入 `source.c`，失效自动重解析）
- [ ] 可选音频文件缓存（`[cache] audio = true`）
  - [ ] 后台下载完整音频到 `<cache>/follow/audio`
  - [ ] YouTube 定长 Range 分块下载
  - [ ] `audio_max_mb` 上限 + 启动/下载后清理
  - [ ] 音频缓存命中时直接本地播放，不再请求 API
  - [ ] 标题/艺人 sidecar（缓存命中时仍能显示解析出来的元数据）
  - [ ] 缓存损坏时删除并回退到在线解析
  - [ ] README 免责声明补充：缓存仅供个人重复播放，不建议分发

## P3 — 歌曲墙体验

- [ ] 拖动结束 / 滚轮结束后，选中焦点自动落到视野内最近磁贴（拖动已做，滚轮待确认）
- [ ] 鼠标单击磁贴立即播放并返回
- [ ] 双击 / 中键行为确认
- [ ] 键盘空间导航的边界情况
  - [ ] 当前 focus 在屏幕外时方向键的落点
  - [ ] PageUp / PageDown 目标磁贴
  - [ ] Home / End 在当前区域内的最近实例
- [ ] 触摸板双指平移
- [ ] 磁贴长标题 / 小尺寸时的省略与排版
- [ ] 窗口 resize 后 focus / camera 的重新对中

## P4 — 文档 / 测试 / 清理

- [ ] `README.md` 功能、快捷键、配置项、技术栈更新
- [ ] `src/config.ini` 示例更新
- [ ] 测试矩阵
  - [ ] 4 / 6 / 12 / 20 首
  - [ ] 本地封面 / 远程封面 / 无封面
  - [ ] `[ui] animations = false`
  - [ ] 拖动 / 滚轮 / 方向键 / Enter / Esc / Tab
  - [ ] 解析缓存命中、过期、直链失效重试
- [ ] 最终 `make -j4` 无 warning
- [ ] 运行 `main.exe` 冒烟测试
- [ ] 确认 Folia block 思路的许可证说明：只用布局思路，槽位坐标/实现为自有变体
