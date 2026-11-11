//! MCP 工具：把 uidbg 的命令包成「看一眼 / 点一下 / 等一等」。
//!
//! 原则：能用结构化文本说清楚的，就不要截图。`ui_screen` 一条命令就是
//! 几十行文字（1~3 KB），而一张 2K 截图要几万个 token。

use std::time::{Duration, Instant};

use serde_json::{json, Value};

use crate::client::Client;
use crate::fmt::{clock, num, one_line, yes};

fn arg_str(args: &Value, key: &str) -> Option<String> {
    args.get(key).and_then(Value::as_str).map(str::to_string)
}

fn arg_f64(args: &Value, key: &str) -> Option<f64> {
    args.get(key).and_then(Value::as_f64)
}

fn arg_i64(args: &Value, key: &str) -> Option<i64> {
    args.get(key).and_then(|v| v.as_i64())
}

fn arg_bool(args: &Value, key: &str, fallback: bool) -> bool {
    args.get(key).and_then(Value::as_bool).unwrap_or(fallback)
}

/// 输入注进去之后，界面要等下一帧才重画：给个短保险，
/// 紧接着的 ui_screen 就不会读到旧画面（动画类变化用 ui_wait 等）。
fn settle() {
    std::thread::sleep(Duration::from_millis(200));
}

fn text(s: impl Into<String>) -> Value {
    json!({ "type": "text", "text": s.into() })
}

/* ---------------- 工具表 ---------------- */

pub fn list() -> Vec<Value> {
    vec![
        json!({
            "name": "ui_state",
            "description": "读 Follow 的应用状态：视图（stage/wall/help）、播什么、进度/音量/输出设备、歌词行、GL 层是否可用、帧时间。比截图便宜得多，先看它。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "raw": { "type": "boolean", "description": "直接返回原始 JSON（默认压成几行短文本）" }
                }
            }
        }),
        json!({
            "name": "ui_screen",
            "description": "读当前画面上有什么：每段文字的坐标/字号/颜色/层级，加上 GTK 控件（命令面板那些）。用来替代截图判断「界面对不对」。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "contains": { "type": "string", "description": "只保留文字里含这个子串的项（不区分大小写）" },
                    "layer": { "type": "string", "description": "只保留某一层：wall / wall-chrome / stage / lyric / palette" },
                    "widgets": { "type": "boolean", "description": "是否包含 GTK 控件（默认 true）" },
                    "limit": { "type": "integer", "description": "最多返回多少行（默认 80）" }
                }
            }
        }),
        json!({
            "name": "ui_key",
            "description": "给应用注入一次按键：Tab / Space / Escape / Left / Page_Down / ctrl+shift+p 这种写法都行。命令面板开着时 Enter/↑/↓/Esc 会走面板自己的选中逻辑。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "key": { "type": "string", "description": "键名，可带修饰键，如 \"Tab\"、\"space\"、\"ctrl+shift+p\"" }
                },
                "required": ["key"]
            }
        }),
        json!({
            "name": "ui_seek",
            "description": "跳到指定播放位置（秒）。调歌词分句 / 行切换 / 空行间奏 / 扫描头时先用它落到固定位置，结果才可复现。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "seconds": { "type": "number", "description": "绝对位置（秒）" },
                    "ms": { "type": "number", "description": "绝对位置（毫秒），seconds 不给时用它" }
                }
            }
        }),
        json!({
            "name": "ui_click",
            "description": "在窗口坐标上点一下；也可以给 contains 让它先在画面上找那段文字再点它的中心。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "x": { "type": "number" },
                    "y": { "type": "number" },
                    "contains": { "type": "string", "description": "按文字定位：点这段文字的中心" },
                    "double": { "type": "boolean", "description": "双击" }
                }
            }
        }),
        json!({
            "name": "ui_scroll",
            "description": "滚轮 / 触摸板平移（歌曲墙用）。dy 为负是向下翻。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "dx": { "type": "number" },
                    "dy": { "type": "number" },
                    "x": { "type": "number" },
                    "y": { "type": "number" }
                }
            }
        }),
        json!({
            "name": "ui_type",
            "description": "往命令面板的输入框里写字（会触发过滤和重新选中第一项）。先 ctrl+shift+p 打开面板。",
            "inputSchema": {
                "type": "object",
                "properties": { "text": { "type": "string" } },
                "required": ["text"]
            }
        }),
        json!({
            "name": "ui_logs",
            "description": "最近的 g_log 记录（解析缓存命中、歌词来源、播放错误之类）。按 level 或 contains 过滤。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "limit": { "type": "integer", "description": "默认 20" },
                    "level": { "type": "string", "description": "all / message / info / warning / error" },
                    "contains": { "type": "string", "description": "消息里含这个子串" }
                }
            }
        }),
        json!({
            "name": "ui_perf",
            "description": "帧时间与渲染器：render_ms（这一帧 cairo 画了多久）、间隔 avg/p95、GL 三层是否可用。应用是按变化重画，不是持续 60fps。",
            "inputSchema": { "type": "object", "properties": {} }
        }),
        json!({
            "name": "ui_screenshot",
            "description": "需要看颜色/排版/模糊这类文字说不清的东西时才用：按 max_width 缩小后返回 PNG。默认整窗缩到 720 宽（约 40~80 KB）。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "max_width": { "type": "integer", "description": "输出宽度上限，默认 720；想看细节可以调大或配合 region" },
                    "region": {
                        "type": "array",
                        "items": { "type": "number" },
                        "description": "[x, y, w, h] 只截这一块（逻辑像素）"
                    }
                }
            }
        }),
        json!({
            "name": "ui_wait",
            "description": "等某个状态出现（比如加载完、歌词出来、墙打开），避免盲目 sleep。条件可以组合。",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "contains": { "type": "string", "description": "画面上出现这段文字" },
                    "view": { "type": "string", "description": "视图变成 stage / wall / help" },
                    "state": { "type": "string", "description": "播放状态变成 playing / paused / stopped / loading / finished / error" },
                    "palette_open": { "type": "boolean", "description": "命令面板是否打开" },
                    "timeout_ms": { "type": "integer", "description": "默认 5000" }
                }
            }
        }),
        json!({
            "name": "ui_launch",
            "description": "带调试通道启动 main.exe（FOLLOW_UIDBG=1），已经在跑就直接复用。改了代码想重开时，先 ui_key ctrl+q 退出再调它。",
            "inputSchema": { "type": "object", "properties": {} }
        }),
    ]
}

/* ---------------- 调用分发 ---------------- */

pub fn call(name: &str, args: &Value, client: &Client) -> Result<Vec<Value>, String> {
    match name {
        "ui_state" => ui_state(client, args),
        "ui_screen" => ui_screen(client, args),
        "ui_key" => ui_key(client, args),
        "ui_click" => ui_click(client, args),
        "ui_scroll" => ui_scroll(client, args),
        "ui_seek" => ui_seek(client, args),
        "ui_type" => ui_type(client, args),
        "ui_logs" => ui_logs(client, args),
        "ui_perf" => ui_perf(client, args),
        "ui_screenshot" => ui_screenshot(client, args),
        "ui_wait" => ui_wait(client, args),
        "ui_launch" => ui_launch(client),
        other => Err(format!("没有这个工具: {other}")),
    }
}

/* ---------------- ui_state ---------------- */

fn ui_state(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let data = client.call("state", json!({}))?;

    if arg_bool(args, "raw", false) {
        return Ok(vec![text(data.to_string())]);
    }

    let win = &data["window"];
    let play = &data["playback"];
    let lyr = &data["lyrics"];
    let gl = &data["gl"];
    let frames = &data["frames"];

    let mut out = String::new();

    out.push_str(&format!(
        "view={} wall={} help={} palette={} | {}x{} fs={} scale={}\n",
        data["view"].as_str().unwrap_or("?"),
        yes(data["wall_open"].as_bool().unwrap_or(false)),
        yes(data["help"].as_bool().unwrap_or(false)),
        yes(data["palette_open"].as_bool().unwrap_or(false)),
        win["w"].as_i64().unwrap_or(0),
        win["h"].as_i64().unwrap_or(0),
        yes(win["fullscreen"].as_bool().unwrap_or(false)),
        num(win["scale"].as_f64().unwrap_or(1.0), 2),
    ));

    out.push_str(&format!(
        "playback={} {}/{} vol={}% muted={} repeat={} shuffle={} device=\"{}\"\n",
        play["state"].as_str().unwrap_or("?"),
        clock(play["position"].as_f64().unwrap_or(0.0)),
        clock(play["duration"].as_f64().unwrap_or(0.0)),
        num(play["volume"].as_f64().unwrap_or(0.0) * 100.0, 0),
        yes(play["muted"].as_bool().unwrap_or(false)),
        play["repeat"].as_str().unwrap_or("?"),
        yes(play["shuffle"].as_bool().unwrap_or(false)),
        one_line(play["device"].as_str().unwrap_or("-"), 40),
    ));

    out.push_str(&format!(
        "track #{}/{} id={} \"{}\"\n",
        play["track"].as_i64().unwrap_or(-1),
        play["tracks"].as_i64().unwrap_or(0),
        play["id"].as_str().unwrap_or("-"),
        one_line(play["label"].as_str().unwrap_or("-"), 60),
    ));

    let mut flags = Vec::new();

    if play["source_pending"].as_bool().unwrap_or(false) {
        flags.push("source_pending");
    }
    if play["cached_source"].as_bool().unwrap_or(false) {
        flags.push("cached_source");
    }
    if let Some(err) = play["source_error"].as_str() {
        flags.push("source_error");
        out.push_str(&format!("source_error: {}\n", one_line(err, 200)));
    }
    if let Some(title) = play["resolved_title"].as_str() {
        out.push_str(&format!("resolved_title: {}\n", one_line(title, 120)));
    }
    if let Some(err) = play["error"].as_str() {
        out.push_str(&format!("media_error: {}\n", one_line(err, 200)));
    }
    if !flags.is_empty() {
        out.push_str(&format!("source flags: {}\n", flags.join(" ")));
    }

    out.push_str(&format!(
        "lyrics spec={} line=\"{}\" next=\"{}\" offset={}ms pending={}{}\n",
        one_line(lyr["spec"].as_str().unwrap_or("-"), 60),
        one_line(lyr["line"].as_str().unwrap_or(""), 80),
        one_line(lyr["next"].as_str().unwrap_or(""), 80),
        lyr["offset_ms"].as_i64().unwrap_or(0),
        yes(lyr["pending"].as_bool().unwrap_or(false)),
        lyr["error"]
            .as_str()
            .map(|e| format!(" error=\"{}\"", one_line(e, 120)))
            .unwrap_or_default(),
    ));

    out.push_str(&format!(
        "gl backdrop={} lyric={} wall={} | frames fps={} avg={}ms p95={}ms render={}ms\n",
        yes(gl["backdrop"].as_bool().unwrap_or(false)),
        yes(gl["lyric"].as_bool().unwrap_or(false)),
        yes(gl["wall"].as_bool().unwrap_or(false)),
        num(frames["fps"].as_f64().unwrap_or(0.0), 1),
        num(frames["avg_ms"].as_f64().unwrap_or(0.0), 1),
        num(frames["p95_ms"].as_f64().unwrap_or(0.0), 1),
        num(frames["render_ms"].as_f64().unwrap_or(0.0), 1),
    ));

    Ok(vec![text(out.trim_end().to_string())])
}

/* ---------------- ui_screen ---------------- */

fn item_line(tag: &str, item: &Value) -> String {
    let cx = item["x"].as_f64().unwrap_or(0.0) + item["w"].as_f64().unwrap_or(0.0) * 0.5;
    let cy = item["y"].as_f64().unwrap_or(0.0) + item["h"].as_f64().unwrap_or(0.0) * 0.5;

    if let Some(label) = item.get("type").and_then(Value::as_str) {
        // GTK 控件
        let css = item["css"].as_str().unwrap_or("");
        let focus = if item["focused"].as_bool().unwrap_or(false) {
            " *"
        } else {
            ""
        };

        return format!(
            "[{tag}] @{},{} {}x{} {}{}{}\"{}\"",
            num(cx, 0),
            num(cy, 0),
            num(item["w"].as_f64().unwrap_or(0.0), 0),
            num(item["h"].as_f64().unwrap_or(0.0), 0),
            label,
            if css.is_empty() {
                String::new()
            } else {
                format!(".{css} ")
            },
            focus,
            one_line(item["t"].as_str().unwrap_or(""), 120),
        );
    }

    format!(
        "[{}] @{},{} {}x{} {}px a{} {} \"{}\"",
        item["layer"].as_str().unwrap_or("?"),
        num(cx, 0),
        num(cy, 0),
        num(item["w"].as_f64().unwrap_or(0.0), 0),
        num(item["h"].as_f64().unwrap_or(0.0), 0),
        num(item["px"].as_f64().unwrap_or(0.0), 0),
        num(item["a"].as_f64().unwrap_or(1.0), 2),
        item["color"].as_str().unwrap_or("?"),
        one_line(item["t"].as_str().unwrap_or(""), 160),
    )
}

fn ui_screen(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let data = client.call("screen", json!({}))?;
    let state = client.call("state", json!({})).unwrap_or(Value::Null);

    let contains = arg_str(args, "contains").map(|s| s.to_lowercase());
    let layer = arg_str(args, "layer");
    let want_widgets = arg_bool(args, "widgets", true);
    let limit = arg_i64(args, "limit").unwrap_or(80).clamp(1, 400) as usize;

    let mut rows: Vec<String> = Vec::new();
    let mut total = 0usize;
    let mut per_layer: Vec<(String, usize)> = Vec::new();

    for group in ["text", "rects", "widgets"] {
        if group == "widgets" && !want_widgets {
            continue;
        }

        let Some(items) = data[group].as_array() else {
            continue;
        };

        for item in items {
            let label = item["t"].as_str().unwrap_or("");

            // layer 过滤：rects / widgets 也要过，因为调用方只想看某一层
            if let Some(want) = &layer {
                let got = match group {
                    "widgets" => "gtk",
                    _ => item["layer"].as_str().unwrap_or("?"),
                };

                if !want.eq_ignore_ascii_case(got) {
                    continue;
                }
            }

            if group == "rects" {
                if contains.is_some() {
                    continue;
                }

                total += 1;
                let rect = format!(
                    "[rect:{}] @{},{} {}x{} {}",
                    item["tag"].as_str().unwrap_or("?"),
                    num(item["x"].as_f64().unwrap_or(0.0) + item["w"].as_f64().unwrap_or(0.0) * 0.5, 0),
                    num(item["y"].as_f64().unwrap_or(0.0) + item["h"].as_f64().unwrap_or(0.0) * 0.5, 0),
                    num(item["w"].as_f64().unwrap_or(0.0), 0),
                    num(item["h"].as_f64().unwrap_or(0.0), 0),
                    item["color"].as_str().unwrap_or("?"),
                );

                if rows.len() < limit {
                    rows.push(rect);
                }

                continue;
            }

            if group == "text" {
                let lname = item["layer"].as_str().unwrap_or("?");

                match per_layer.iter_mut().find(|(n, _)| n == lname) {
                    Some((_, count)) => *count += 1,
                    None => per_layer.push((lname.to_string(), 1)),
                }
            }

            if let Some(needle) = &contains {
                if !label.to_lowercase().contains(needle) {
                    continue;
                }
            }

            total += 1;

            if rows.len() < limit {
                let tag = if group == "widgets" {
                    "gtk"
                } else {
                    item["layer"].as_str().unwrap_or("?")
                };

                rows.push(item_line(tag, item));
            }
        }
    }

    let size = &data["size"];
    let mut head = format!(
        "{}x{}",
        size["w"].as_i64().unwrap_or(0),
        size["h"].as_i64().unwrap_or(0)
    );

    if !state.is_null() {
        head.push_str(&format!(
            " view={} palette={}",
            state["view"].as_str().unwrap_or("?"),
            yes(state["palette_open"].as_bool().unwrap_or(false)),
        ));
    }

    let layer_note = if per_layer.is_empty() {
        String::new()
    } else {
        let parts: Vec<String> = per_layer
            .iter()
            .map(|(name, count)| format!("{name}:{count}"))
            .collect();

        format!(" [{}]", parts.join(" "))
    };

    let view = state["view"].as_str().unwrap_or("");
    let stale_note = if view == "wall"
        && per_layer
            .iter()
            .any(|(name, _)| name == "stage" || name == "lyric")
    {
        "\n（stage / lyric 那几条是墙下面的主界面，没重画，看墙只看 wall-chrome）"
    } else {
        ""
    };

    let mut out = format!("{head}{layer_note} | {} 项\n", rows.len());

    out.push_str(&rows.join("\n"));
    out.push_str(stale_note);

    let omitted_texts = total.saturating_sub(rows.len());

    if omitted_texts > 0 {
        out.push_str(&format!(
            "\n… 还有 {omitted_texts} 条没显示（加 contains / layer 过滤，或调大 limit）"
        ));
    }

    if rows.is_empty() {
        if let Some(want) = &layer {
            out.push_str(&format!(
                "\n（{want} 层现在是空的：刚切视图的话等一帧再看，或先用 ui_wait 等 view）"
            ));
        }
    }

    Ok(vec![text(out.trim_end().to_string())])
}

/* ---------------- 输入 ---------------- */

fn ui_key(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let key = arg_str(args, "key").ok_or("缺少 key（例如 \"Tab\" 或 \"ctrl+shift+p\"）")?;
    let data = client.call("key", json!({ "key": key }))?;
    let handled = data["handled"].as_bool().unwrap_or(false);

    settle();

    let note = if handled {
        "已处理"
    } else {
        "没有处理（这个视图里可能没绑这个键）"
    };

    Ok(vec![text(format!("key {key}: {note}"))])
}

fn ui_seek(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let seconds = arg_f64(args, "seconds")
        .or_else(|| arg_f64(args, "ms").map(|ms| ms / 1000.0))
        .ok_or("缺少 seconds（或 ms）")?;

    let data = client.call("seek", json!({ "seconds": seconds }))?;

    settle();

    let at = data["seconds"].as_f64().unwrap_or(seconds);

    Ok(vec![text(format!("seek -> {at:.2}s"))])
}

fn ui_type(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let value = arg_str(args, "text").ok_or("缺少 text")?;
    let data = client.call("text", json!({ "value": value }))?;

    if data["palette_open"].as_bool() != Some(true) {
        return Ok(vec![text(
            "命令面板没开，文字没写进去。先 ui_key ctrl+shift+p。".to_string(),
        )]);
    }

    settle();

    Ok(vec![text(format!("命令面板输入框现在是 \"{value}\""))])
}

fn find_click_target(client: &Client, needle: &str) -> Result<(f64, f64, String), String> {
    let data = client.call("screen", json!({}))?;
    let needle = needle.to_lowercase();
    let mut best: Option<(usize, f64, f64, String)> = None;

    // 层级越靠上越优先：palette > lyric > stage > wall-chrome > wall
    fn rank(layer: &str) -> usize {
        match layer {
            "palette" => 0,
            "lyric" => 1,
            "stage" => 2,
            "wall-chrome" => 3,
            _ => 4,
        }
    }

    let mut candidates: Vec<&Value> = Vec::new();

    for group in ["text", "widgets"] {
        if let Some(items) = data[group].as_array() {
            candidates.extend(items.iter());
        }
    }

    for item in candidates {
        let label = item["t"].as_str().unwrap_or("");

        if !label.to_lowercase().contains(&needle) {
            continue;
        }

        let layer = item["layer"].as_str().unwrap_or("gtk");
        let cx = item["x"].as_f64().unwrap_or(0.0) + item["w"].as_f64().unwrap_or(0.0) * 0.5;
        let cy = item["y"].as_f64().unwrap_or(0.0) + item["h"].as_f64().unwrap_or(0.0) * 0.5;
        let rank = rank(layer);

        if best.as_ref().map(|(r, ..)| rank < *r).unwrap_or(true) {
            best = Some((rank, cx, cy, label.to_string()));
        }
    }

    match best {
        Some((_, cx, cy, label)) => Ok((cx, cy, label)),
        None => Err(format!("画面上找不到含 \"{needle}\" 的文字")),
    }
}

fn ui_click(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let twice = arg_bool(args, "double", false);

    let (x, y, note) = match (arg_f64(args, "x"), arg_f64(args, "y")) {
        (Some(x), Some(y)) => (x, y, String::new()),
        _ => {
            let needle = arg_str(args, "contains")
                .ok_or("要么给 x/y，要么给 contains（按文字定位）")?;
            let (cx, cy, label) = find_click_target(client, &needle)?;

            (cx, cy, format!("（点在 \"{}\" 中心）", one_line(&label, 40)))
        }
    };

    client.call(
        "click",
        json!({ "x": x, "y": y, "double": twice }),
    )?;

    settle();

    Ok(vec![text(format!(
        "click @{},{} {}{}",
        num(x, 0),
        num(y, 0),
        if twice { "（双击）" } else { "" },
        note
    ))])
}

fn ui_scroll(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let dx = arg_f64(args, "dx").unwrap_or(0.0);
    let dy = arg_f64(args, "dy").unwrap_or(0.0);
    let x = arg_f64(args, "x").unwrap_or(0.0);
    let y = arg_f64(args, "y").unwrap_or(0.0);

    let data = client.call(
        "scroll",
        json!({ "x": x, "y": y, "dx": dx, "dy": dy }),
    )?;

    if data["ok"].as_bool() != Some(true) {
        return Ok(vec![text(
            "滚轮只在歌曲墙里生效，当前没有开墙。".to_string(),
        )]);
    }

    settle();

    Ok(vec![text(format!(
        "scroll dx={} dy={}",
        num(dx, 1),
        num(dy, 1)
    ))])
}

/* ---------------- 日志 / 性能 ---------------- */

fn ui_logs(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let limit = arg_i64(args, "limit").unwrap_or(20).clamp(1, 200);
    let level = arg_str(args, "level").unwrap_or_else(|| "all".to_string());
    let contains = arg_str(args, "contains").unwrap_or_default();

    let data = client.call(
        "logs",
        json!({ "limit": limit, "level": level, "contains": contains }),
    )?;

    let empty = Vec::new();
    let logs = data["logs"].as_array().unwrap_or(&empty);

    if logs.is_empty() {
        return Ok(vec![text("（没有匹配的日志）".to_string())]);
    }

    let mut out = String::new();

    for rec in logs {
        out.push_str(&format!(
            "-{}s [{}] {} {}\n",
            num(rec["ago"].as_f64().unwrap_or(0.0), 1),
            rec["level"].as_str().unwrap_or("?"),
            rec["domain"].as_str().unwrap_or(""),
            one_line(rec["msg"].as_str().unwrap_or(""), 300),
        ));
    }

    Ok(vec![text(out.trim_end().to_string())])
}

fn ui_perf(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let _ = args;
    let data = client.call("perf", json!({}))?;

    Ok(vec![text(format!(
        "renderer={} {}x{} | render={}ms | 间隔 avg={}ms p95={}ms | gl backdrop={} lyric={} wall={}\n\
         应用是按需重画（不是持续 60fps）：avg/p95 是「相邻两次重画的间隔」，render 才是这一帧真正画了多久。",
        data["renderer"].as_str().unwrap_or("?"),
        data["w"].as_i64().unwrap_or(0),
        data["h"].as_i64().unwrap_or(0),
        num(data["render_ms"].as_f64().unwrap_or(0.0), 1),
        num(data["avg_ms"].as_f64().unwrap_or(0.0), 1),
        num(data["p95_ms"].as_f64().unwrap_or(0.0), 1),
        yes(data["gl_backdrop"].as_bool().unwrap_or(false)),
        yes(data["gl_lyric"].as_bool().unwrap_or(false)),
        yes(data["gl_wall"].as_bool().unwrap_or(false)),
    ))])
}

/* ---------------- 截图 ---------------- */

fn ui_screenshot(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let max_width = arg_i64(args, "max_width").unwrap_or(720).clamp(160, 3840);
    let region = args.get("region").cloned().unwrap_or(Value::Null);

    let data = client.call(
        "screenshot",
        json!({ "max_width": max_width, "region": region }),
    )?;

    let png = data["png"].as_str().ok_or("截图没有返回数据")?;
    let meta = format!(
        "{}x{} ({} KB, scale={}){}",
        data["w"].as_i64().unwrap_or(0),
        data["h"].as_i64().unwrap_or(0),
        data["bytes"].as_i64().unwrap_or(0) / 1024,
        num(data["scale"].as_f64().unwrap_or(1.0), 3),
        if data["scale"].as_f64().unwrap_or(1.0) < 1.0 {
            " 已缩小"
        } else {
            ""
        },
    );

    Ok(vec![
        text(format!(
            "截图 {meta}。只有颜色/排版这类文字说不清的东西才值得看；细节优先用 ui_screen。"
        )),
        json!({ "type": "image", "data": png, "mimeType": "image/png" }),
    ])
}

/* ---------------- 等待 ---------------- */

fn ui_wait(client: &Client, args: &Value) -> Result<Vec<Value>, String> {
    let contains = arg_str(args, "contains").map(|s| s.to_lowercase());
    let view = arg_str(args, "view");
    let want_state = arg_str(args, "state");
    let palette = args.get("palette_open").and_then(Value::as_bool);
    let timeout = Duration::from_millis(
        arg_i64(args, "timeout_ms")
            .unwrap_or(5000)
            .clamp(100, 120_000) as u64,
    );

    if contains.is_none() && view.is_none() && want_state.is_none() && palette.is_none() {
        return Err("至少给一个条件：contains / view / state / palette_open".to_string());
    }

    let started = Instant::now();

    loop {
        let mut ok = true;
        let mut seen = Vec::new();

        if view.is_some() || want_state.is_some() || palette.is_some() {
            let state = client.call("state", json!({}))?;

            if let Some(want) = &view {
                let got = state["view"].as_str().unwrap_or("?");
                seen.push(format!("view={got}"));

                if !want.eq_ignore_ascii_case(got) {
                    ok = false;
                }
            }

            if let Some(want) = &want_state {
                let got = state["playback"]["state"].as_str().unwrap_or("?");
                seen.push(format!("state={got}"));

                if !want.eq_ignore_ascii_case(got) {
                    ok = false;
                }
            }

            if let Some(want) = palette {
                let got = state["palette_open"].as_bool().unwrap_or(false);
                seen.push(format!("palette_open={got}"));

                if got != want {
                    ok = false;
                }
            }
        }

        if let Some(needle) = &contains {
            let screen = client.call("screen", json!({}))?;
            let mut found = false;

            for group in ["text", "widgets"] {
                if let Some(items) = screen[group].as_array() {
                    for item in items {
                        if item["t"]
                            .as_str()
                            .map(|t| t.to_lowercase().contains(needle))
                            .unwrap_or(false)
                        {
                            found = true;
                            break;
                        }
                    }
                }

                if found {
                    break;
                }
            }

            seen.push(format!("contains(\"{needle}\")={found}"));

            if !found {
                ok = false;
            }
        }

        let elapsed = started.elapsed();

        if ok {
            return Ok(vec![text(format!(
                "等到了（{}ms）：{}",
                elapsed.as_millis(),
                seen.join(" ")
            ))]);
        }

        if elapsed >= timeout {
            return Err(format!(
                "等超时（{}ms）：现在是 {}",
                timeout.as_millis(),
                seen.join(" ")
            ));
        }

        std::thread::sleep(Duration::from_millis(150));
    }
}

/* ---------------- 启动应用 ---------------- */

fn repo_root() -> std::path::PathBuf {
    std::env::var("FOLLOW_ROOT")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| {
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../..")
                .canonicalize()
                .unwrap_or_else(|_| std::path::PathBuf::from("."))
        })
}

fn ui_launch(client: &Client) -> Result<Vec<Value>, String> {
    if client.alive() {
        return Ok(vec![text(format!(
            "应用已经在跑，调试通道 127.0.0.1:{} 有响应。",
            client.port()
        ))]);
    }

    let root = repo_root();
    let app = std::env::var("FOLLOW_APP")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| root.join("main.exe"));

    if !app.exists() {
        return Err(format!("找不到 {}（先 make 出 main.exe）", app.display()));
    }

    // MSYS2 的 GTK 运行时：不把 mingw64/bin 加进 PATH 会缺 DLL
    let gtk_bin = std::env::var("FOLLOW_GTK_BIN")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("F:/msys64/mingw64/bin"));

    let mut path = std::ffi::OsString::new();

    if gtk_bin.exists() {
        path.push(gtk_bin.as_os_str());
        path.push(";");
    }

    if let Some(old) = std::env::var_os("PATH") {
        path.push(old);
    }

    std::process::Command::new(&app)
        .current_dir(&root)
        .env("FOLLOW_UIDBG", "1")
        .env("PATH", path)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| format!("启动 {} 失败: {e}", app.display()))?;

    let started = Instant::now();

    while started.elapsed() < Duration::from_secs(25) {
        if client.alive() {
            return Ok(vec![text(format!(
                "已启动 {}（cwd={}），调试通道 {}ms 后可用 (127.0.0.1:{})。",
                app.display(),
                root.display(),
                started.elapsed().as_millis(),
                client.port()
            ))]);
        }

        std::thread::sleep(Duration::from_millis(300));
    }

    Err(format!(
        "启动了 {}，但 25s 内调试通道 127.0.0.1:{} 没起来（看应用窗口里的报错）。",
        app.display(),
        client.port()
    ))
}
