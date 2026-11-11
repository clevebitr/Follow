//! 把 uidbg 的 JSON 压成「一行一条、人/模型都能读懂」的文本。

/// 压成单行、去掉多余空白、超长截断。
pub fn one_line(text: &str, max: usize) -> String {
    let mut s = String::with_capacity(text.len());

    for ch in text.chars() {
        match ch {
            '\n' => s.push_str("\\n"),
            '\r' => {}
            '\t' => s.push(' '),
            _ => s.push(ch),
        }
    }

    if s.chars().count() > max {
        let cut: String = s.chars().take(max).collect();

        return format!("{cut}…");
    }

    s
}

/// 保留 digits 位小数，去掉尾随 0（数字看起来更干净）。
pub fn num(v: f64, digits: usize) -> String {
    if !v.is_finite() {
        return "?".to_string();
    }

    let s = format!("{v:.digits$}");
    let s = if s.contains('.') {
        s.trim_end_matches('0').trim_end_matches('.').to_string()
    } else {
        s
    };

    if s == "-0" {
        "0".to_string()
    } else {
        s
    }
}

/// 秒 → "mm:ss"
pub fn clock(seconds: f64) -> String {
    let total = if seconds.is_finite() && seconds > 0.0 {
        seconds.round() as i64
    } else {
        0
    };

    format!("{:02}:{:02}", total / 60, total % 60)
}

pub fn yes(flag: bool) -> &'static str {
    if flag {
        "on"
    } else {
        "off"
    }
}
