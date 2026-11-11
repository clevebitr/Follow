//! Follow 的 uidbg 调试通道客户端。
//!
//! 应用侧（`src/ui/uidbg.c`）在 `FOLLOW_UIDBG=1` 时监听 127.0.0.1:7878，
//! 一行一个 JSON 请求 / 一行一个 JSON 响应；这里只做「发一条命令、拿回 data」。

use std::io::{BufRead, BufReader, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::Duration;

use serde_json::{json, Value};

pub fn port() -> u16 {
    std::env::var("FOLLOW_UIDBG_PORT")
        .ok()
        .and_then(|v| v.trim().parse::<u16>().ok())
        .filter(|p| *p > 0)
        .unwrap_or(7878)
}

pub struct Client {
    port: u16,
}

impl Client {
    pub fn new() -> Self {
        Self { port: port() }
    }

    pub fn port(&self) -> u16 {
        self.port
    }

    fn connect(&self) -> Result<TcpStream, String> {
        let addr = ("127.0.0.1", self.port)
            .to_socket_addrs()
            .map_err(|e| format!("解析 127.0.0.1:{} 失败: {e}", self.port))?
            .next()
            .ok_or_else(|| "没有可用地址".to_string())?;

        let stream = TcpStream::connect_timeout(&addr, Duration::from_millis(900)).map_err(|e| {
            format!(
                "连不上 Follow 的调试通道 127.0.0.1:{}（{e}）。\n\
                 应用可能没在跑，或者启动时没设 FOLLOW_UIDBG=1。\n\
                 可以用 ui_launch 直接带调试通道把应用起起来。",
                self.port
            )
        })?;

        stream
            .set_read_timeout(Some(Duration::from_secs(20)))
            .ok();
        stream
            .set_write_timeout(Some(Duration::from_secs(10)))
            .ok();

        Ok(stream)
    }

    /// 发一条命令；返回响应里的 `data` 对象。
    pub fn call(&self, cmd: &str, args: Value) -> Result<Value, String> {
        let mut stream = self.connect()?;
        let request = json!({ "id": 1, "cmd": cmd, "args": args });

        let mut line = request.to_string();
        line.push('\n');

        stream
            .write_all(line.as_bytes())
            .map_err(|e| format!("发送失败: {e}"))?;

        let mut reader = BufReader::new(stream);
        let mut response = String::new();

        reader
            .read_line(&mut response)
            .map_err(|e| format!("读取响应失败: {e}"))?;

        if response.trim().is_empty() {
            return Err("应用没回话（连接被关掉了？）".to_string());
        }

        let parsed: Value =
            serde_json::from_str(response.trim()).map_err(|e| format!("响应不是 JSON: {e}"))?;

        if parsed.get("ok").and_then(Value::as_bool) != Some(true) {
            let err = parsed
                .get("error")
                .and_then(Value::as_str)
                .unwrap_or("未知错误");

            return Err(format!("uidbg 拒绝了 {cmd}: {err}"));
        }

        Ok(parsed.get("data").cloned().unwrap_or(Value::Null))
    }

    /// 通道在不在（不报错）
    pub fn alive(&self) -> bool {
        self.call("ping", json!({})).is_ok()
    }
}
