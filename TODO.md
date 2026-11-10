# TODO

## P0 — 本轮收尾

- [ ] 把 `src/core/resolvecache.c` 接进 `src/core/source.c`
  - [ ] B 站 / YouTube 解析前先 `resolvecache_load()`
  - [ ] 解析成功后 `resolvecache_save()`
  - [ ] 实现 `source_cache_invalidate()`
  - [ ] 播放缓存直链失败时，`player.c` 失效缓存并自动重解析一次（只重试一次）
- [ ] 配置解析补完
  - [ ] `[cache] resolve_ttl`（默认 2700 秒，0 = 关闭）
  - [ ] `[cache] cover_max_mb`（默认 128，已用 `cache_prune_prefix()` 清理）
  - [ ] `[ui] animations`（已解析/使用）
  - [ ] 把 `[cache]` / `[ui]` 写进 `src/config.ini` 注释
- [ ] 退出时清理 `resolvecache` 过期文件（`resolvecache_prune()` 接到启动）
- [ ] 删除无用代码
  - [ ] `covercache_scaled()` 和 `CoverEntry.scaled/scaled_px`（已不再使用）
  - [ ] `config_audio_cache()` / `config_audio_max_bytes()` 暂未使用，先保留或标记“预留”
- [ ] 重新链接 `main.exe`（当前磁盘上的还是旧磁贴墙版本）
- [ ] 更新 README
  - [ ] 无限墙 / 自由拖动 / 滚轮 / 随机布局
  - [ ] 进出场、选中磁贴、封面淡入、`[ui] animations`
  - [ ] 解析缓存 TTL、封面缓存上限
- [ ] 清理测试产物
  - [ ] `build/follow-wall.exe`、`build/tmp/*` 测试脚本/截图
  - [ ] 检查 `git diff --check`

## P1 — 硬件加速（优先解决墙卡顿）

- [ ] 选型验证
  - [ ] 方案 A：`GtkGLArea` + OpenGL textured quad
    - [ ] 每首歌/每种尺寸用 cairo 预渲染成 `GdkTexture`
    - [ ] 相机变换、选中缩放、光晕用 shader / MVP
  - [ ] 方案 B：`GtkSnapshot` + `GdkTexture` 节点
    - [ ] 保留 cairo 生成磁贴纹理
    - [ ] 每帧只 push 纹理节点 + transform，让 GSK / GPU 合成
    - [ ] 改动比 GLArea 小，优先试这个
  - [ ] 不建议依赖 `GSK_RENDERER=gl` 加速 cairo：它只加速 GTK/GSK 合成，不加速 `GtkDrawingArea` 里的 cairo 绘制
- [ ] 从 `GtkDrawingArea` 迁出墙层（或者墙单独一层，stage 仍用 cairo）
- [ ] 保留 cairo 作为纹理生成器和无 GPU fallback
- [ ] 背景/压暗/叠色从每帧全屏 cairo 填充改成 GPU 层或缓存纹理
- [ ] 60fps 目标 + 帧时间统计
- [ ] 低配模式
  - [ ] `[ui] animations = false` 时直接 snap，不跑 60fps
  - [ ] 可选降低磁贴缓存分辨率 / 限制同屏数量

## P2 — 缓存

- [x] 封面磁盘缓存 + 上限清理（`cache_prune_prefix`）
- [x] 封面淡入 / 按尺寸缓存 / 磁贴内容缓存
- [ ] 封面缓存
  - [ ] `[cache] cover_max_mb` 配置解析
  - [ ] 同一封面并发请求合并（`cover.c` 与 `covercache.c` 避免重复下载）
  - [ ] 缓存损坏时自动删除重下
- [ ] 解析结果缓存（模块已写，待接入）
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
