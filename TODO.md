# TODO

## P0 — 歌词动效（本轮）

- [x] `lyrics`: 逐字时间轴
  - [x] 增强 LRC：`[00:12.00]<00:12.00>词<00:12.50>词` → 每个字的绝对时间（`char_ms`）
  - [x] 网易云 `yrc`：`[行起,行时长](词起,词长,0)字...` 解析 + 优先取用（`lyricsrc.c`）
  - [x] 没有词级时间戳的行 → `char_ms = NULL`（渲染时整行一起亮）
- [x] `lyricgl`: 动效复刻 AMLL
  - [x] 当前行 `scale 1.0`、非当前行 `0.97`，用 `mass 2 / damping 25 / stiffness 100` 的弹簧
  - [x] 非当前行叠 `blur(1 + distance)` px（mipmap LOD 实现），切到当前行时衰减到 0
  - [x] 唱完的当前行：先缩到 0.90（0.26s），再向上滑出 0.55 行高 + 淡出（0.42s）
  - [x] 预显示只铺一行（`STACK_MAX = 1`）
  - [x] 逐字高亮只在有 `char_ms` 的行启用；否则整行高亮
- [x] 三行版式：上=正在滑出的上一句、中=当前句、下=下一句（当前句上方预留一行）
- [x] 当前句放大 1.06、其余 0.94（原来 1.0 / 0.97 差距太不明显）
- [x] 模糊换成 9 抽头高斯（σ 连续变化），去掉 mipmap 抽稀的块状感
- [x] 退场行重新蒙上模糊（2px），滑出距离改成整整一行
- [x] 上台动效：待演出行先原地缩到 0.88（0.12s，y 先不动），再上滑并带过冲放大到 1.06
- [x] 退场提速：0.28s 内走完（缩 0.12s → 滑出 0.22s），不再占位挡住下一句
- [x] 预显示扩到两行；远近两档（94%/2px/72% 和 90%/3.2px/44%），当前句放大到 1.12
- [x] 新版式做深度梯度，避免两行预显示规格一样显得突然
- [x] 新预演行从下方 1.6em 浮上来 + 0.22s 慢淡入，不再“凭空冒出来”
- [x] 布局给底边渐隐留位置，第二行预显示不会被切掉半截
- [x] 四列版式：预推出列 / 当前 / 预演一 / 预演二（预推出列就是刚唱完挂在上面的那句）
- [x] 当前句用封面种子色当高亮色（`geom.accent`，stage 里 `seed_accent()` 拉亮提饱和）
- [x] 交错弹簧：每往下一行起跑晚 50ms，上滑时间隙先拉开再收回
- [x] 模糊分级：预推出 1.0 / 预演一 1.4（减轻）/ 预演二 4.2（加重）
- [x] 上台 0.38s、退场 0.20s（原来 0.52s / 0.28s），位置弹簧加硬到 0.9/17/120
- [x] 加诊断日志：每次换行打一条 `lyricgl: line -> N`（排查“某段时间没有歌词”）
- [x] 修：退场动画结束后会被 `was_focus` 重新拉起（无限循环）→ 启动即清标记
- [x] 编译 + 实机验证（弹簧数值、模糊、退场曲线、逐字时间轴解析）

## P0 — 歌词排版重做

- [x] `theme`: 新增 `text_wrap()`，按宽度自动折行、不限行数、不省略
  - [x] 每个视觉行给字节区间 + 相对顶部偏移（`pango_layout_line_get_extents` 的 y 是相对行自身的，需要累加）
- [x] `theme`: `text_mask_pair()` 改成单行 + `order_base/order_total`，折行后逐字时间坐标仍然跨行连续
- [x] `theme`: 删掉 `text_glow_draw()` / 辉光遮罩缓存
- [x] `lyricgl`: 重写——左对齐、当前行贴区域顶、往下铺待唱行、逐行绘制
  - [x] 去掉模糊 / 辉光 / 缩放（不再有玻璃效果）
  - [x] 去掉“最多几行 + 省略号”，长句自动分段（彻底解决被截断后不显示）
  - [x] 行距：行内折行紧贴，句与句之间留 gap
- [x] `stage`: 接新 `LyricViewGeom`（无 align/max_lines/glow），cairo 回退也改左对齐 + 自动折行
- [x] 删掉 `LYRIC_MAX_LINES` / `LYRIC_NEXT_MAX_LINES`
- [x] 编译 + 实机验证（b 站曲目：折行、逐字亮起、左对齐、无辉光）

## P0 — 上一轮收尾

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

## P1.5 — 2K 60fps / GPU 背景（本轮完成）

- [x] Windows 启动时自动启用 `GDK_DEBUG=dcomp`，让 GSK 用 GL 合成（`FOLLOW_DCOMP=0` / `GSK_RENDERER=cairo` 可强制回退）
- [x] 动态背景 GPU 层：模糊底图纹理 + shader 光斑 + 加色粒子 sprite + scrim，`FOLLOW_BACKDROPGL=0` 退回 CPU 小纹理
- [x] 墙的 frame clock tick 在 map / unmap 时重新挂载，开墙 → 关墙 → 再开墙不会停帧
- [x] 16ms 泵只在 pending 时 `queue_draw`，chrome 只在 focus / 时间变化 / 进场退场时重画
- [x] 封面解码全部移到后台线程（本地 / 远程缓存命中也不阻塞 UI）
- [x] 磁贴 cairo 纹理改低优先级队列生成，打开墙不再同步卡 100ms+；focus 磁贴同步预热
- [x] 关墙不再清空磁贴缓存，二次打开 0–4ms
- [x] HiDPI：墙 viewport 按设备像素设置，磁贴 / overlay 按 scale 渲染
- [x] 纹理缓存按字节预算淘汰（96MB 磁贴 / 24MB overlay）
- [x] 2K + DComp 实测持续 60–80fps（1080p 100fps+），GL 帧时间 < 0.5ms

## P1.6 — 命令面板 / 音量 / 输出设备（本轮完成）

- [x] 窗口外框：去掉 CSD 12px 阴影（DComp 下会变黑）+ DWM 圆角 / 边框，1px 细边 + 自定义标题栏拖动 / 四边缩放
- [x] `Ctrl+Shift+P` 命令面板：右下角、输入框在底部、建议向上、VSCode 风格
- [x] 命令：重载 ini、音量 +/-/50/100/静音、输出设备、可视化、动效、循环/随机、歌词、墙、帮助、全屏、退出
- [x] 音量 / 静音 / 输出设备写入 `<cache>/follow/settings.ini`，启动时恢复
- [x] 输出设备枚举 / 切换（miniaudio context + device reinit），切换失败回退
- [x] `Reload config.ini`：重读曲目 / 按键 / 可视化 / 动效，清磁贴缓存并刷新墙
- [x] 命令面板支持模糊过滤、↑/↓、PageUp/PageDown、Enter、Esc、Backspace 返回、点外部关闭

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
