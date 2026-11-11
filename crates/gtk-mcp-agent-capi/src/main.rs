//! Follow 的 MCP 调试服务（stdio）。
//!
//! 架构：
//!
//! ```text
//! Follow (GTK/C)                    gtk-mcp-agent (Rust)          pi
//! ┌───────────────┐  TCP JSONL    ┌──────────────────┐  stdio  ┌────┐
//! │ src/ui/uidbg.c│◄─────────────►│ tools.rs / client│◄───────►│ MCP│
//! │ 127.0.0.1:7878│               │ 压缩成短文本      │         └────┘
//! └───────────────┘               └──────────────────┘
//! ```
//!
//! 应用侧的 C 调试通道是「一行一个 JSON」的请求/响应；这里把它翻成 MCP 工具，
//! 并把 JSON 压成省 token 的短文本。截图只是兜底手段。

mod client;
mod fmt;
mod tools;

use std::io::{self, BufRead, Write};

use serde_json::{json, Value};

const SERVER_NAME: &str = "gtk-mcp-agent";
const SERVER_VERSION: &str = env!("CARGO_PKG_VERSION");
const PROTOCOL_VERSION: &str = "2024-11-05";

const INSTRUCTIONS: &str = "\
调试 Follow 的 GTK 界面时优先用 ui_state / ui_screen 看结构化状态，不要一上来就截图：
- ui_screen 返回每段文字的坐标/字号/颜色和 GTK 控件，几 KB 就够描述一整屏
- 需要点哪里，用 ui_click 的 contains 按文字定位，不用自己算坐标
- 改完代码用 ui_key ctrl+q 退出，再 ui_launch 重开（自动带 FOLLOW_UIDBG=1）
- 只有颜色、模糊、排版这类文字说不清的东西才用 ui_screenshot，并保持较小的 max_width
应用启动时如果不是 ui_launch 拉起来的，需要自己设 FOLLOW_UIDBG=1 调试通道才在。";

fn main() {
    let client = client::Client::new();
    let stdin = io::stdin();
    let mut input = stdin.lock();
    let stdout = io::stdout();
    let mut output = stdout.lock();

    let mut line = String::new();

    loop {
        line.clear();

        match input.read_line(&mut line) {
            Ok(0) => break,
            Ok(_) => {}
            Err(_) => break,
        }

        let trimmed = line.trim();

        if trimmed.is_empty() {
            continue;
        }

        let request: Value = match serde_json::from_str(trimmed) {
            Ok(value) => value,
            Err(err) => {
                let _ = writeln!(
                    output,
                    "{}",
                    json!({
                        "jsonrpc": "2.0",
                        "id": Value::Null,
                        "error": { "code": -32700, "message": format!("JSON 解析失败: {err}") }
                    })
                );
                let _ = output.flush();
                continue;
            }
        };

        let id = request.get("id").cloned();
        let method = request.get("method").and_then(Value::as_str).unwrap_or("");
        let params = request.get("params").cloned().unwrap_or(Value::Null);

        // 通知（没有 id）不需要回包
        let Some(id) = id else { continue };

        let response = match dispatch(method, &params, &client) {
            Ok(result) => json!({ "jsonrpc": "2.0", "id": id, "result": result }),
            Err((code, message)) => {
                json!({ "jsonrpc": "2.0", "id": id, "error": { "code": code, "message": message } })
            }
        };

        let _ = writeln!(output, "{response}");
        let _ = output.flush();
    }
}

fn dispatch(
    method: &str,
    params: &Value,
    client: &client::Client,
) -> Result<Value, (i64, String)> {
    match method {
        "initialize" => Ok(json!({
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": { "tools": {} },
            "serverInfo": { "name": SERVER_NAME, "version": SERVER_VERSION },
            "instructions": INSTRUCTIONS,
        })),
        "ping" => Ok(json!({})),
        "tools/list" => Ok(json!({ "tools": tools::list() })),
        "tools/call" => {
            let name = params
                .get("name")
                .and_then(Value::as_str)
                .ok_or((-32602, "tools/call 缺少 name".to_string()))?;

            let args = params
                .get("arguments")
                .cloned()
                .unwrap_or_else(|| json!({}));

            match tools::call(name, &args, client) {
                Ok(content) => Ok(json!({ "content": content, "isError": false })),
                Err(message) => Ok(json!({
                    "content": [{ "type": "text", "text": message }],
                    "isError": true
                })),
            }
        }
        other => Err((-32601, format!("不支持的方法: {other}"))),
    }
}
