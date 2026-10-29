//! Built-in ad blocker without settings: one list (EasyList), updated every
//! four days, turned into WebKit content-blocker rules. Only rules for whole
//! domains are used: better to let an ad through than to break a site.

use std::cell::RefCell;
use std::collections::BTreeSet;
use std::path::PathBuf;
use std::rc::Rc;
use std::time::{Duration, SystemTime};

use gtk::{gio, glib};

pub const LIST_URL: &str = "https://easylist.to/easylist/easylist.txt";
const FILTER_ID: &str = "easylist";
const MAX_AGE: Duration = Duration::from_secs(4 * 24 * 3600);
const CHECK_EVERY_SECS: u32 = 6 * 3600;

/// Every kind of load except the page itself: what the user navigated to is
/// never blocked.
const BLOCKED_TYPES: &str = r#"["image","style-sheet","script","font","raw","svg-document","media","popup","ping","fetch","websocket","other"]"#;

/// The domains of an EasyList: blocked everywhere, and blocked only when
/// loaded by another site.
#[derive(Debug, Default, PartialEq)]
pub struct Domains {
    pub everywhere: BTreeSet<String>,
    pub third_party: BTreeSet<String>,
}

pub fn parse(list: &str) -> Domains {
    let mut domains = Domains::default();
    let mut allowed = BTreeSet::new();
    for line in list.lines() {
        let mut line = line.trim();
        let exception = line.starts_with("@@");
        if exception {
            line = &line[2..];
        }
        let Some(rule) = line.strip_prefix("||") else { continue };
        let Some((domain, rest)) = rule.split_once('^') else { continue };
        let domain = domain.to_ascii_lowercase();
        if domain.is_empty()
            || !domain.chars().all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '-')
        {
            continue;
        }
        if exception {
            // Any exception for a whole domain unblocks it entirely.
            if rest.is_empty() || rest.starts_with('$') {
                allowed.insert(domain);
            }
        } else if rest.is_empty() {
            domains.everywhere.insert(domain);
        } else if rest == "$third-party" {
            domains.third_party.insert(domain);
        }
        // Rules with paths or other options are skipped on purpose.
    }
    for domain in &allowed {
        domains.everywhere.remove(domain);
        domains.third_party.remove(domain);
    }
    domains
}

/// WebKit content-blocker JSON. WebKit's regular expressions have no `|`,
/// so it is one rule per domain.
pub fn to_webkit_json(domains: &Domains) -> String {
    let rule = |domain: &str, third_party: bool| {
        let escaped = domain.replace('.', "\\\\.");
        format!(
            r#"{{"trigger":{{"url-filter":"^[a-z-]+://([^/:]+\\.)?{escaped}[/:]","resource-type":{BLOCKED_TYPES}{}}},"action":{{"type":"block"}}}}"#,
            if third_party { r#","load-type":["third-party"]"# } else { "" }
        )
    };
    let rules: Vec<String> = domains
        .everywhere
        .iter()
        .map(|d| rule(d, false))
        .chain(domains.third_party.iter().map(|d| rule(d, true)))
        .collect();
    format!("[{}]", rules.join(","))
}

pub struct AdBlocker {
    store: webkit6::UserContentFilterStore,
    content: webkit6::UserContentManager,
    list_path: PathBuf,
    updating: RefCell<bool>,
}

impl AdBlocker {
    pub fn new(data_dir: &std::path::Path, content: &webkit6::UserContentManager) -> Rc<Self> {
        let store_dir = data_dir.join("filters");
        let _ = std::fs::create_dir_all(&store_dir);
        Rc::new(AdBlocker {
            store: webkit6::UserContentFilterStore::new(&store_dir.to_string_lossy()),
            content: content.clone(),
            list_path: data_dir.join("easylist.txt"),
            updating: RefCell::new(false),
        })
    }

    /// Applies the compiled list if there is one, and keeps it fresh.
    pub fn start(self: &Rc<Self>, session: &webkit6::NetworkSession) {
        let this = Rc::clone(self);
        self.store.load(FILTER_ID, gio::Cancellable::NONE, move |result| {
            if let Ok(filter) = result {
                this.content.add_filter(&filter);
            }
        });
        self.update_if_stale(session);
        let this = Rc::downgrade(self);
        let session = session.clone();
        glib::timeout_add_seconds_local(CHECK_EVERY_SECS, move || match this.upgrade() {
            Some(this) => {
                this.update_if_stale(&session);
                glib::ControlFlow::Continue
            }
            None => glib::ControlFlow::Break,
        });
    }

    /// The download of the list itself, so the browser does not announce it.
    pub fn owns(&self, download: &webkit6::Download) -> bool {
        download
            .request()
            .and_then(|r| r.uri())
            .is_some_and(|uri| uri == LIST_URL)
    }

    fn update_if_stale(self: &Rc<Self>, session: &webkit6::NetworkSession) {
        let fresh = std::fs::metadata(&self.list_path)
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| SystemTime::now().duration_since(t).ok())
            .is_some_and(|age| age < MAX_AGE);
        if fresh || self.updating.replace(true) {
            return;
        }
        let Some(download) = session.download_uri(LIST_URL) else {
            *self.updating.borrow_mut() = false;
            return;
        };
        download.set_allow_overwrite(true);
        let path = self.list_path.to_string_lossy().into_owned();
        download.connect_decide_destination(move |download, _| {
            download.set_destination(&path);
            true
        });
        let this = Rc::clone(self);
        download.connect_failed(move |_, error| {
            eprintln!("plain: filter list update failed: {error}");
        });
        download.connect_finished(move |_| {
            *this.updating.borrow_mut() = false;
            this.compile();
        });
    }

    fn compile(self: &Rc<Self>) {
        let Ok(list) = std::fs::read_to_string(&self.list_path) else { return };
        if !list.starts_with("[Adblock") {
            eprintln!("plain: filter list update returned unexpected content");
            return;
        }
        let domains = parse(&list);
        let count = domains.everywhere.len() + domains.third_party.len();
        let json = glib::Bytes::from_owned(to_webkit_json(&domains).into_bytes());
        let this = Rc::clone(self);
        self.store.save(FILTER_ID, &json, gio::Cancellable::NONE, move |result| match result {
            Ok(filter) => {
                this.content.remove_all_filters();
                this.content.add_filter(&filter);
                eprintln!("plain: ad blocker compiled {count} domain rules");
            }
            Err(error) => eprintln!("plain: ad blocker rules rejected: {error}"),
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const LIST: &str = "[Adblock Plus 2.0]\n\
        ! comment\n\
        ||ads.example.com^\n\
        ||tracker.net^$third-party\n\
        ||broken.org^$script,domain=foo.com\n\
        ||cdn.site.com/ads/\n\
        ||good.com^\n\
        @@||good.com^\n\
        ##.banner\n";

    #[test]
    fn keeps_only_whole_domain_rules() {
        let d = parse(LIST);
        assert_eq!(d.everywhere.iter().collect::<Vec<_>>(), ["ads.example.com"]);
        assert_eq!(d.third_party.iter().collect::<Vec<_>>(), ["tracker.net"]);
    }

    #[test]
    fn webkit_rules() {
        let json = to_webkit_json(&parse(LIST));
        assert!(json.starts_with("[{") && json.ends_with("}]"));
        assert!(json.contains(r#"([^/:]+\\.)?ads\\.example\\.com[/:]"#));
        assert_eq!(json.matches(r#""load-type":["third-party"]"#).count(), 1);
        assert!(!json.contains(r#""document""#));
    }
}
