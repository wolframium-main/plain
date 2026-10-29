//! What the user typed into the command line, and other pure logic that
//! needs no window: addresses, searches, commands, download file names.

use std::path::{Path, PathBuf};

/// One line of input.
#[derive(Debug, PartialEq, Eq)]
pub enum Input {
    /// Nothing but spaces.
    Empty,
    /// An address or a search, already turned into a URL.
    Open(String),
    /// `:name arg`
    Command { name: String, arg: String },
}

pub fn parse(line: &str) -> Input {
    let line = line.trim();
    if line.is_empty() {
        return Input::Empty;
    }
    if let Some(body) = line.strip_prefix(':') {
        let body = body.trim();
        let (name, arg) = match body.split_once(char::is_whitespace) {
            Some((name, arg)) => (name, arg.trim()),
            None => (body, ""),
        };
        return Input::Command { name: name.to_owned(), arg: arg.to_owned() };
    }
    Input::Open(url_from_input(line))
}

/// An address, a local file, or otherwise a Google search.
pub fn url_from_input(text: &str) -> String {
    let text = text.trim();
    // An explicit scheme always means an address, spaces or not.
    if has_scheme(text) {
        return text.to_owned();
    }
    if text.chars().any(char::is_whitespace) {
        return search_url(text);
    }
    if text.starts_with('/') {
        return format!("file://{text}");
    }
    if let Some(rest) = text.strip_prefix("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            return format!("file://{}/{rest}", Path::new(&home).display());
        }
    }
    if looks_like_host(text) {
        return format!("https://{text}");
    }
    search_url(text)
}

fn has_scheme(text: &str) -> bool {
    ["about:", "data:", "file:", "view-source:", "javascript:"]
        .iter()
        .any(|s| text.starts_with(s))
        || text
            .split_once("://")
            .map(|(scheme, _)| {
                !scheme.is_empty()
                    && scheme.chars().next().is_some_and(|c| c.is_ascii_alphabetic())
                    && scheme.chars().all(|c| c.is_ascii_alphanumeric() || "+.-".contains(c))
            })
            .unwrap_or(false)
}

fn looks_like_host(text: &str) -> bool {
    let host = text.split(['/', '?', '#']).next().unwrap_or("");
    let host = host.rsplit_once(':').map_or(host, |(h, port)| {
        if port.chars().all(|c| c.is_ascii_digit()) { h } else { host }
    });
    host == "localhost"
        || (host.contains('.')
            && !host.starts_with('.')
            && !host.ends_with('.')
            && host.chars().all(|c| c.is_alphanumeric() || c == '.' || c == '-'))
}

pub fn search_url(text: &str) -> String {
    let mut query = String::new();
    for byte in text.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                query.push(byte as char)
            }
            b' ' => query.push('+'),
            _ => query.push_str(&format!("%{byte:02X}")),
        }
    }
    format!("https://www.google.com/search?q={query}")
}

/// `"3"` → `Some(2)` when there are at least three tabs.
pub fn tab_index(arg: &str, tabs: usize) -> Option<usize> {
    match arg.trim().parse::<usize>() {
        Ok(n) if n >= 1 && n <= tabs => Some(n - 1),
        _ => None,
    }
}

/// `"125"` or `"125%"` → `Some(1.25)`, within 25–500 %.
pub fn zoom_level(arg: &str) -> Option<f64> {
    let percent: u32 = arg.trim().trim_end_matches('%').trim().parse().ok()?;
    (25..=500).contains(&percent).then(|| f64::from(percent) / 100.0)
}

/// A path in `dir` that does not exist yet: `file.txt`, `file (1).txt`, …
pub fn unique_path(dir: &Path, name: &str) -> PathBuf {
    let name = if name.trim().is_empty() { "download" } else { name };
    // Never let a server put a file outside the downloads folder.
    let name = Path::new(name).file_name().and_then(|n| n.to_str()).unwrap_or("download");
    let candidate = dir.join(name);
    if !candidate.exists() {
        return candidate;
    }
    let (stem, ext) = match name.rsplit_once('.') {
        Some((stem, ext)) if !stem.is_empty() => (stem, Some(ext)),
        _ => (name, None),
    };
    (1..)
        .map(|n| match ext {
            Some(ext) => dir.join(format!("{stem} ({n}).{ext}")),
            None => dir.join(format!("{stem} ({n})")),
        })
        .find(|p| !p.exists())
        .expect("an unused name")
}

/// `/home/me/Загрузки/a.pdf` → `~/Загрузки/a.pdf`
pub fn short_path(path: &Path) -> String {
    if let Some(home) = std::env::var_os("HOME") {
        if let Ok(rest) = path.strip_prefix(&home) {
            return format!("~/{}", rest.display());
        }
    }
    path.display().to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn commands() {
        assert_eq!(parse("  "), Input::Empty);
        assert_eq!(
            parse(":close 3"),
            Input::Command { name: "close".into(), arg: "3".into() }
        );
        assert_eq!(parse(":tabs"), Input::Command { name: "tabs".into(), arg: "".into() });
        assert_eq!(
            parse(" :find  two words "),
            Input::Command { name: "find".into(), arg: "two words".into() }
        );
    }

    #[test]
    fn addresses() {
        assert_eq!(url_from_input("github.com"), "https://github.com");
        assert_eq!(url_from_input("doc.qt.io/qt-6/"), "https://doc.qt.io/qt-6/");
        assert_eq!(url_from_input("localhost:8080"), "https://localhost:8080");
        assert_eq!(url_from_input("http://example.com"), "http://example.com");
        assert_eq!(url_from_input("/tmp/a.html"), "file:///tmp/a.html");
        // A scheme wins over spaces: data URLs are addresses.
        assert_eq!(url_from_input("data:text/html,<b>two words</b>"), "data:text/html,<b>two words</b>");
    }

    #[test]
    fn searches() {
        assert_eq!(url_from_input("маятник"), search_url("маятник"));
        assert_eq!(url_from_input("qt webengine"), "https://www.google.com/search?q=qt+webengine");
        assert_eq!(url_from_input("rust"), "https://www.google.com/search?q=rust");
        assert!(url_from_input("маятник Фуко").starts_with("https://www.google.com/search?q=%D0%BC"));
    }

    #[test]
    fn numbers() {
        assert_eq!(tab_index("2", 3), Some(1));
        assert_eq!(tab_index("4", 3), None);
        assert_eq!(tab_index("0", 3), None);
        assert_eq!(tab_index("x", 3), None);
        assert_eq!(zoom_level("150"), Some(1.5));
        assert_eq!(zoom_level("125%"), Some(1.25));
        assert_eq!(zoom_level("9000"), None);
        assert_eq!(zoom_level("abc"), None);
    }

    #[test]
    fn download_names() {
        let dir = std::env::temp_dir().join(format!("plain-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        assert_eq!(unique_path(&dir, "a.txt"), dir.join("a.txt"));
        std::fs::write(dir.join("a.txt"), "").unwrap();
        assert_eq!(unique_path(&dir, "a.txt"), dir.join("a (1).txt"));
        std::fs::write(dir.join("a (1).txt"), "").unwrap();
        assert_eq!(unique_path(&dir, "a.txt"), dir.join("a (2).txt"));
        assert_eq!(unique_path(&dir, "../../etc/passwd"), dir.join("passwd"));
        assert_eq!(unique_path(&dir, ""), dir.join("download"));
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
