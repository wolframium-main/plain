//! The only window of plain: up to five tabs that are never shown as tabs,
//! a header bar that hides itself, and a command line at the bottom.

use std::cell::{Cell, RefCell};
use std::collections::VecDeque;
use std::path::PathBuf;
use std::rc::{Rc, Weak};

use adw::prelude::*;
use gtk::{gdk, gio, glib};
use webkit6::prelude::*;

use crate::adblock::AdBlocker;
use crate::input::{self, Input};

pub const MAX_TABS: usize = 5;
const PLACEHOLDER: &str = "Адрес, поиск или :команда";
/// How close to the top edge the pointer brings the header bar back.
const HEADER_HOT_ZONE: f64 = 56.0;

/// A permission a site is waiting for: asked in the command line, one at a
/// time, never remembered.
struct Question {
    view: glib::WeakRef<webkit6::WebView>,
    text: String,
    request: webkit6::PermissionRequest,
}

/// A page held back by an invalid certificate until `:proceed`.
struct CertificateError {
    view: glib::WeakRef<webkit6::WebView>,
    uri: String,
    host: String,
    certificate: gio::TlsCertificate,
    text: String,
}

#[derive(Default)]
struct State {
    tabs: Vec<webkit6::WebView>,
    /// A tab waiting for a free slot: `Some(None)` is an empty tab.
    pending: Option<Option<String>>,
    line_open: bool,
    tabs_open: bool,
    pointer_near_top: bool,
    fullscreen: bool,
    questions: VecDeque<Question>,
    question_shown: bool,
    certificates: Vec<CertificateError>,
    find: Option<String>,
}

pub struct Browser {
    window: adw::ApplicationWindow,
    toolbar: adw::ToolbarView,
    title: adw::WindowTitle,
    banner: adw::Banner,
    toasts: adw::ToastOverlay,
    stack: gtk::Stack,
    progress: gtk::ProgressBar,
    tab_list: gtk::ListBox,
    entry: gtk::Entry,
    key_hint: gtk::Box,
    session: webkit6::NetworkSession,
    settings: webkit6::Settings,
    content: webkit6::UserContentManager,
    adblock: Rc<AdBlocker>,
    state: RefCell<State>,
    me: RefCell<Weak<Browser>>,
}

thread_local! {
    static INSTANCE: RefCell<Option<Rc<Browser>>> = const { RefCell::new(None) };
}

impl Browser {
    /// The one window, created on first use.
    pub fn for_app(app: &adw::Application) -> Rc<Browser> {
        if let Some(browser) = INSTANCE.with(|i| i.borrow().clone()) {
            return browser;
        }
        let browser = Browser::new(app);
        INSTANCE.with(|i| *i.borrow_mut() = Some(Rc::clone(&browser)));
        browser
    }

    fn new(app: &adw::Application) -> Rc<Browser> {
        // Remembers cookies and sign-ins. Nothing else: no history.
        let data_dir = glib::user_data_dir().join("plain");
        let cache_dir = glib::user_cache_dir().join("plain");
        let _ = std::fs::create_dir_all(&data_dir);
        let session = webkit6::NetworkSession::new(
            Some(&data_dir.to_string_lossy()),
            Some(&cache_dir.to_string_lossy()),
        );
        if let Some(cookies) = session.cookie_manager() {
            cookies.set_persistent_storage(
                &data_dir.join("cookies.sqlite").to_string_lossy(),
                webkit6::CookiePersistentStorage::Sqlite,
            );
        }

        let settings = webkit6::Settings::new();
        settings.set_enable_developer_extras(true);
        settings.set_enable_media_stream(true);
        settings.set_enable_webrtc(true);
        // Debug builds only: fake camera and microphone for testing the
        // permission question on machines that have neither.
        if cfg!(debug_assertions) && std::env::var_os("PLAIN_MOCK_CAPTURE").is_some() {
            settings.set_enable_mock_capture_devices(true);
        }

        let content = webkit6::UserContentManager::new();
        let adblock = AdBlocker::new(&data_dir, &content);

        // Header bar: page title and domain, window buttons. Hidden unless
        // the command line is open or the pointer is at the top edge.
        let title = adw::WindowTitle::new("plain", "");
        let header = adw::HeaderBar::new();
        header.set_title_widget(Some(&title));

        // Page area: the tabs, a loading bar over their top edge, a banner
        // for whatever holds the page back, toasts for everything else.
        let stack = gtk::Stack::new();
        stack.set_vexpand(true);
        let progress = gtk::ProgressBar::new();
        progress.add_css_class("osd");
        progress.set_valign(gtk::Align::Start);
        progress.set_can_target(false);
        progress.set_visible(false);
        let overlay = gtk::Overlay::new();
        overlay.set_child(Some(&stack));
        overlay.add_overlay(&progress);
        let banner = adw::Banner::new("");
        let page = gtk::Box::new(gtk::Orientation::Vertical, 0);
        page.append(&banner);
        page.append(&overlay);
        let toasts = adw::ToastOverlay::new();
        toasts.set_child(Some(&page));

        // Bottom bar: tab list, command line, key hints.
        let tab_list = gtk::ListBox::new();
        tab_list.add_css_class("boxed-list");
        tab_list.set_selection_mode(gtk::SelectionMode::None);
        tab_list.set_visible(false);
        let entry = gtk::Entry::builder().placeholder_text(PLACEHOLDER).hexpand(true).build();
        let key_hint = gtk::Box::new(gtk::Orientation::Horizontal, 6);
        key_hint.set_halign(gtk::Align::Center);
        key_hint.append(&gtk::ShortcutLabel::new("y"));
        key_hint.append(&dim_label("разрешить"));
        key_hint.append(&gtk::ShortcutLabel::new("n"));
        key_hint.append(&gtk::ShortcutLabel::new("Escape"));
        key_hint.append(&dim_label("запретить"));
        key_hint.set_visible(false);
        let bottom_box = gtk::Box::new(gtk::Orientation::Vertical, 12);
        bottom_box.set_margin_top(12);
        bottom_box.set_margin_bottom(12);
        bottom_box.set_margin_start(12);
        bottom_box.set_margin_end(12);
        bottom_box.append(&tab_list);
        bottom_box.append(&entry);
        bottom_box.append(&key_hint);
        let bottom = adw::Clamp::builder().maximum_size(640).child(&bottom_box).build();

        let toolbar = adw::ToolbarView::new();
        toolbar.add_top_bar(&header);
        toolbar.add_bottom_bar(&bottom);
        toolbar.set_content(Some(&toasts));
        toolbar.set_top_bar_style(adw::ToolbarStyle::Raised);
        toolbar.set_bottom_bar_style(adw::ToolbarStyle::Raised);
        toolbar.set_extend_content_to_top_edge(true);
        toolbar.set_reveal_top_bars(false);
        toolbar.set_reveal_bottom_bars(false);

        let window = adw::ApplicationWindow::builder()
            .application(app)
            .title("plain")
            .default_width(1280)
            .default_height(800)
            .content(&toolbar)
            .build();

        let browser = Rc::new(Browser {
            window,
            toolbar,
            title,
            banner,
            toasts,
            stack,
            progress,
            tab_list,
            entry,
            key_hint,
            session,
            settings,
            content,
            adblock,
            state: RefCell::default(),
            me: RefCell::default(),
        });
        *browser.me.borrow_mut() = Rc::downgrade(&browser);
        browser.connect();
        browser.adblock.start(&browser.session);
        browser.create_tab(true, None);
        browser
    }

    fn weak(&self) -> Weak<Browser> {
        self.me.borrow().clone()
    }

    fn connect(&self) {
        // The one and only hotkey: the key that carries the colon. Captured
        // before the page, so no site can swallow it.
        let shortcuts = gtk::ShortcutController::new();
        shortcuts.set_propagation_phase(gtk::PropagationPhase::Capture);
        shortcuts.set_scope(gtk::ShortcutScope::Global);
        let weak = self.weak();
        shortcuts.add_shortcut(gtk::Shortcut::new(
            gtk::ShortcutTrigger::parse_string("<Control>semicolon"),
            Some(gtk::CallbackAction::new(move |_, _| {
                if let Some(b) = weak.upgrade() {
                    b.open_line("");
                }
                glib::Propagation::Stop
            })),
        ));
        self.window.add_controller(shortcuts);

        let weak = self.weak();
        self.entry.connect_activate(move |entry| {
            if let Some(b) = weak.upgrade() {
                b.execute(&entry.text());
            }
        });

        // Esc closes the line; that is also "no" to a question.
        let keys = gtk::EventControllerKey::new();
        let weak = self.weak();
        keys.connect_key_pressed(move |_, key, _, _| {
            if key == gdk::Key::Escape {
                if let Some(b) = weak.upgrade() {
                    b.close_line();
                }
                return glib::Propagation::Stop;
            }
            glib::Propagation::Proceed
        });
        self.entry.add_controller(keys);

        // Clicking back into the page closes the line.
        let focus = gtk::EventControllerFocus::new();
        let weak = self.weak();
        focus.connect_leave(move |_| {
            let Some(b) = weak.upgrade() else { return };
            let into_page = gtk::prelude::GtkWindowExt::focus(&b.window)
                .is_some_and(|w| w.is_ancestor(&b.stack));
            if into_page && b.state.borrow().line_open {
                b.close_line();
            }
        });
        self.entry.add_controller(focus);

        // The header bar comes back when the pointer reaches the top edge.
        let motion = gtk::EventControllerMotion::new();
        motion.set_propagation_phase(gtk::PropagationPhase::Capture);
        let weak = self.weak();
        motion.connect_motion(move |_, _, y| {
            if let Some(b) = weak.upgrade() {
                b.set_pointer_near_top(y < HEADER_HOT_ZONE);
            }
        });
        let weak = self.weak();
        motion.connect_leave(move |_| {
            if let Some(b) = weak.upgrade() {
                b.set_pointer_near_top(false);
            }
        });
        self.window.add_controller(motion);

        // The loading bar stays visible below the header bar.
        let weak = self.weak();
        self.toolbar.connect_reveal_top_bars_notify(move |_| {
            if let Some(b) = weak.upgrade() {
                b.place_progress();
            }
        });
        let weak = self.weak();
        self.toolbar.connect_top_bar_height_notify(move |_| {
            if let Some(b) = weak.upgrade() {
                b.place_progress();
            }
        });

        // Downloads go straight to the downloads folder, no questions.
        let weak = self.weak();
        self.session.connect_download_started(move |_, download| {
            if let Some(b) = weak.upgrade() {
                if !b.adblock.owns(download) {
                    b.handle_download(download);
                }
            }
        });
    }

    /// Shows the window and opens what was asked for.
    pub fn present(&self, urls: &[String]) {
        for url in urls {
            let current = self.current();
            let blank = current.as_ref().is_some_and(is_blank);
            match current {
                Some(view) if blank && !view.is_loading() => {
                    view.load_uri(url);
                    self.close_line();
                }
                _ => self.request_tab(Some(url.clone()), false),
            }
        }
        self.window.present();
        if urls.is_empty() && self.current().as_ref().is_some_and(is_blank) {
            self.open_line("");
        }
    }

    // ------------------------------------------------------------- tabs

    fn current(&self) -> Option<webkit6::WebView> {
        self.stack.visible_child().and_downcast::<webkit6::WebView>()
    }

    fn index_of(&self, view: &webkit6::WebView) -> Option<usize> {
        self.state.borrow().tabs.iter().position(|v| v == view)
    }

    fn tab_count(&self) -> usize {
        self.state.borrow().tabs.len()
    }

    fn create_tab(&self, make_current: bool, related: Option<&webkit6::WebView>) -> webkit6::WebView {
        let view = match related {
            // Keeps window.opener: sign-in popups need it.
            Some(parent) => webkit6::WebView::builder()
                .related_view(parent)
                .settings(&self.settings)
                .user_content_manager(&self.content)
                .build(),
            None => webkit6::WebView::builder()
                .network_session(&self.session)
                .settings(&self.settings)
                .user_content_manager(&self.content)
                .build(),
        };
        view.set_vexpand(true);
        set_page_background(&view);
        self.state.borrow_mut().tabs.push(view.clone());
        self.stack.add_child(&view);
        self.connect_tab(&view);
        if make_current {
            self.set_current(&view);
        }
        self.refresh();
        view
    }

    fn connect_tab(&self, view: &webkit6::WebView) {
        let weak = self.weak();
        view.connect_title_notify(move |view| {
            if let Some(b) = weak.upgrade() {
                if b.current().as_ref() == Some(view) {
                    b.refresh();
                } else {
                    b.refresh_tab_list();
                }
            }
        });
        let weak = self.weak();
        view.connect_uri_notify(move |view| {
            set_page_background(view);
            if let Some(b) = weak.upgrade() {
                b.refresh();
            }
        });
        let weak = self.weak();
        view.connect_estimated_load_progress_notify(move |view| {
            if let Some(b) = weak.upgrade() {
                if b.current().as_ref() == Some(view) {
                    b.update_progress();
                }
            }
        });
        let weak = self.weak();
        view.connect_is_loading_notify(move |view| {
            if let Some(b) = weak.upgrade() {
                if b.current().as_ref() == Some(view) {
                    b.update_progress();
                }
            }
        });

        // A site asks for a new tab: a new tab is a new tab.
        let weak = self.weak();
        view.connect_closure(
            "create",
            false,
            glib::closure_local!(move |_view: webkit6::WebView,
                                       mut action: webkit6::NavigationAction|
                  -> Option<gtk::Widget> {
                let b = weak.upgrade()?;
                if b.tab_count() >= MAX_TABS {
                    // The request cannot wait; remember the address instead.
                    let uri = action.request().and_then(|r| r.uri()).map(|u| u.to_string());
                    b.state.borrow_mut().pending = Some(uri);
                    b.show_limit_reached();
                    return None;
                }
                let new = b.create_tab(true, Some(&_view));
                Some(new.upcast())
            }),
        );

        let weak = self.weak();
        view.connect_decide_policy(move |_, decision, kind| {
            let Some(b) = weak.upgrade() else { return false };
            b.decide_policy(decision, kind)
        });

        let weak = self.weak();
        view.connect_permission_request(move |view, request| {
            let Some(b) = weak.upgrade() else { return false };
            b.handle_permission(view, request);
            true
        });

        let weak = self.weak();
        view.connect_load_failed_with_tls_errors(move |view, uri, certificate, errors| {
            if let Some(b) = weak.upgrade() {
                b.handle_certificate_error(view, uri, certificate, errors);
            }
            true
        });

        // The context menu keeps three items, whichever apply.
        view.connect_context_menu(|_, menu, _| {
            use webkit6::ContextMenuAction as A;
            for item in menu.items() {
                if !matches!(item.stock_action(), A::Copy | A::CopyLinkToClipboard | A::DownloadImageToDisk) {
                    menu.remove(&item);
                }
            }
            menu.n_items() == 0 // nothing applies: no menu at all
        });

        let weak = self.weak();
        view.connect_close(move |view| {
            if let Some(b) = weak.upgrade() {
                if let Some(index) = b.index_of(view) {
                    if b.close_tab(index) {
                        b.open_line("");
                    }
                }
            }
        });

        let weak = self.weak();
        view.connect_web_process_terminated(move |view, reason| {
            let Some(b) = weak.upgrade() else { return };
            if reason != webkit6::WebProcessTerminationReason::TerminatedByApi {
                let n = b.index_of(view).map_or(0, |i| i + 1);
                b.toast(&format!("Вкладка {n} завершилась аварийно. :reload откроет её снова"));
            }
        });

        let weak = self.weak();
        view.connect_enter_fullscreen(move |_| {
            if let Some(b) = weak.upgrade() {
                b.close_line();
                b.state.borrow_mut().fullscreen = true;
                b.window.fullscreen();
                b.update_bars();
            }
            false
        });
        let weak = self.weak();
        view.connect_leave_fullscreen(move |_| {
            if let Some(b) = weak.upgrade() {
                b.state.borrow_mut().fullscreen = false;
                b.window.unfullscreen();
                b.update_bars();
            }
            false
        });
    }

    fn decide_policy(&self, decision: &webkit6::PolicyDecision, kind: webkit6::PolicyDecisionType) -> bool {
        match kind {
            webkit6::PolicyDecisionType::NavigationAction => {
                let Some(nav) = decision.downcast_ref::<webkit6::NavigationPolicyDecision>() else {
                    return false;
                };
                let Some(mut action) = nav.navigation_action() else { return false };
                let ctrl = action.modifiers() & gdk::ModifierType::CONTROL_MASK.bits() != 0;
                // Middle click or Ctrl+click: a new tab in the background.
                if action.mouse_button() == 2 || ctrl {
                    let uri = action.request().and_then(|r| r.uri()).map(|u| u.to_string());
                    decision.ignore();
                    self.request_tab(uri, true);
                    return true;
                }
                false
            }
            webkit6::PolicyDecisionType::Response => {
                let Some(response) = decision.downcast_ref::<webkit6::ResponsePolicyDecision>() else {
                    return false;
                };
                let mime = response.response().and_then(|r| r.mime_type()).unwrap_or_default();
                // No PDF viewer inside, and nothing the page cannot show.
                if mime == "application/pdf" || !response.is_mime_type_supported() {
                    decision.download();
                    return true;
                }
                false
            }
            _ => false,
        }
    }

    fn set_current(&self, view: &webkit6::WebView) {
        self.stack.set_visible_child(view);
        if !self.state.borrow().line_open {
            view.grab_focus();
        }
        self.refresh();
        self.update_progress();
    }

    /// Returns true when the last tab was closed and a fresh empty one took
    /// its place: the caller should then open the command line.
    fn close_tab(&self, index: usize) -> bool {
        let view = self.state.borrow_mut().tabs.remove(index);
        self.drop_certificate(&view, false);
        let was_current = self.current().as_ref() == Some(&view);
        if let Some(inspector) = view.inspector() {
            inspector.close();
        }
        self.stack.remove(&view);
        if self.tab_count() == 0 {
            self.create_tab(true, None);
            return true;
        }
        if was_current {
            let next = {
                let s = self.state.borrow();
                s.tabs[index.min(s.tabs.len() - 1)].clone()
            };
            self.set_current(&next);
        }
        self.refresh();
        false
    }

    fn request_tab(&self, url: Option<String>, background: bool) {
        if self.tab_count() >= MAX_TABS {
            self.state.borrow_mut().pending = Some(url);
            self.show_limit_reached();
            return;
        }
        let view = self.create_tab(!background, None);
        if let Some(url) = url {
            view.load_uri(&url);
        }
        if background {
            self.toast(&format!("Открыта вкладка {}", self.tab_count()));
        }
    }

    fn show_limit_reached(&self) {
        self.toast(&format!("Открыто {MAX_TABS} вкладок — закройте одну, и новая откроется сама"));
        self.state.borrow_mut().tabs_open = true;
        self.open_line(":close ");
    }

    /// Opens the tab that waited for a free slot. Returns true when it is
    /// empty and wants the command line.
    fn open_pending_tab(&self) -> bool {
        if self.tab_count() >= MAX_TABS {
            return false;
        }
        let Some(url) = self.state.borrow_mut().pending.take() else { return false };
        let view = self.create_tab(true, None);
        match url {
            Some(url) => {
                view.load_uri(&url);
                false
            }
            None => true,
        }
    }

    // ------------------------------------------------------ command line

    pub fn open_line(&self, prefill: &str) {
        self.state.borrow_mut().line_open = true;
        self.entry.set_text(prefill);
        self.update_bars();
        self.entry.grab_focus_without_selecting();
        self.entry.set_position(-1);
    }

    fn close_line(&self) {
        // Leaving the line without an answer means "no".
        if self.state.borrow().question_shown {
            self.answer_question(false);
        }
        {
            let mut s = self.state.borrow_mut();
            // Leaving the line abandons a tab that waited for a free slot.
            s.pending = None;
            s.line_open = false;
            s.tabs_open = false;
        }
        self.entry.set_text("");
        self.update_bars();
        if let Some(view) = self.current() {
            view.grab_focus();
        }
        self.show_next_question();
    }

    fn execute(&self, text: &str) {
        if self.state.borrow().question_shown {
            let yes = matches!(text.trim().to_lowercase().as_str(), "y" | "yes" | "д" | "да" | "н");
            self.answer_question(yes);
            self.close_line();
            return;
        }
        let close = match input::parse(text) {
            Input::Empty => true,
            Input::Open(url) => {
                if let Some(view) = self.current() {
                    self.drop_certificate(&view, true); // going elsewhere means "no"
                    view.load_uri(&url);
                }
                true
            }
            Input::Command { name, arg } => self.run(&name, &arg),
        };
        if close {
            self.close_line();
        }
    }

    /// Returns true when the line should close afterwards.
    fn run(&self, name: &str, arg: &str) -> bool {
        let view = self.current();
        match name {
            "new" => {
                if self.tab_count() >= MAX_TABS {
                    self.state.borrow_mut().pending =
                        Some((!arg.is_empty()).then(|| input::url_from_input(arg)));
                    self.show_limit_reached();
                    return false;
                }
                let new = self.create_tab(true, None);
                if arg.is_empty() {
                    self.open_line("");
                    return false;
                }
                new.load_uri(&input::url_from_input(arg));
                true
            }
            "tabs" => {
                self.state.borrow_mut().tabs_open = true;
                self.entry.set_text("");
                self.update_bars();
                false
            }
            "tab" => {
                match input::tab_index(arg, self.tab_count()) {
                    Some(i) => {
                        let target = self.state.borrow().tabs[i].clone();
                        self.set_current(&target);
                    }
                    None => self.toast(&format!("Нет вкладки «{arg}»")),
                }
                true
            }
            "close" => {
                let index = if arg.is_empty() {
                    view.as_ref().and_then(|v| self.index_of(v))
                } else {
                    input::tab_index(arg, self.tab_count())
                };
                let Some(index) = index else {
                    self.toast(&format!("Нет вкладки «{arg}»"));
                    if self.state.borrow().pending.is_some() {
                        self.entry.set_text(":close ");
                        self.entry.set_position(-1);
                        return false;
                    }
                    return true;
                };
                let last_replaced = self.close_tab(index);
                let pending_empty = self.open_pending_tab();
                if last_replaced || pending_empty {
                    self.state.borrow_mut().tabs_open = false;
                    self.open_line("");
                    return false;
                }
                true
            }
            "back" | "forward" | "reload" => {
                if let Some(view) = view {
                    self.drop_certificate(&view, true);
                    match name {
                        "back" => view.go_back(),
                        "forward" => view.go_forward(),
                        _ => view.reload(),
                    }
                }
                true
            }
            "proceed" => {
                let error = view.as_ref().and_then(|v| self.take_certificate(v));
                match (view, error) {
                    (Some(view), Some(error)) => {
                        self.session.allow_tls_certificate_for_host(&error.certificate, &error.host);
                        view.load_uri(&error.uri);
                        self.refresh_banner();
                    }
                    _ => self.toast("В этой вкладке нет страницы с недействительным сертификатом"),
                }
                true
            }
            "find" => {
                let Some(finder) = view.and_then(|v| v.find_controller()) else { return true };
                if arg.is_empty() {
                    finder.search_finish();
                    self.state.borrow_mut().find = None;
                    return true;
                }
                let again = self.state.borrow().find.as_deref() == Some(arg);
                if again {
                    finder.search_next();
                } else {
                    let options = (webkit6::FindOptions::CASE_INSENSITIVE
                        | webkit6::FindOptions::WRAP_AROUND)
                        .bits();
                    let weak = self.weak();
                    let text = arg.to_owned();
                    finder.connect_counted_matches(move |finder, count| {
                        if let Some(b) = weak.upgrade() {
                            if finder.search_text().as_deref() == Some(text.as_str()) {
                                b.toast(&format!("Совпадений: {count}"));
                            }
                        }
                    });
                    let weak = self.weak();
                    let text = arg.to_owned();
                    finder.connect_failed_to_find_text(move |_| {
                        if let Some(b) = weak.upgrade() {
                            b.toast(&format!("Не найдено: {text}"));
                        }
                    });
                    finder.count_matches(arg, options, u32::MAX);
                    finder.search(arg, options, u32::MAX);
                    self.state.borrow_mut().find = Some(arg.to_owned());
                }
                true
            }
            "zoom" => {
                let Some(view) = view else { return true };
                if arg.is_empty() {
                    self.toast(&format!("Масштаб: {} %", (view.zoom_level() * 100.0).round()));
                } else if let Some(level) = input::zoom_level(arg) {
                    view.set_zoom_level(level);
                    self.toast(&format!("Масштаб: {} %", (level * 100.0).round()));
                } else {
                    self.toast("Масштаб — число от 25 до 500");
                }
                true
            }
            "url" => {
                match view.as_ref().filter(|v| !is_blank(v)).and_then(|v| v.uri()) {
                    Some(uri) => {
                        self.window.clipboard().set_text(&uri);
                        self.toast("Адрес скопирован");
                    }
                    None => self.toast("Во вкладке ничего не открыто"),
                }
                true
            }
            "devtools" => {
                if let Some(inspector) = view.and_then(|v| v.inspector()) {
                    if inspector.web_view().is_some_and(|w| w.is_visible()) {
                        inspector.close();
                    } else {
                        inspector.show();
                    }
                }
                true
            }
            "quit" => {
                if let Some(app) = self.window.application() {
                    app.quit();
                }
                true
            }
            _ => {
                // Keep the typo on screen so it can be fixed.
                self.toast(&format!("Неизвестная команда «:{name}»"));
                false
            }
        }
    }

    // ---------------------------------------------------------- feedback

    fn toast(&self, text: &str) {
        let toast = adw::Toast::new(&glib::markup_escape_text(text));
        toast.set_timeout(4);
        self.toasts.add_toast(toast);
    }

    fn refresh(&self) {
        let view = self.current();
        let blank = view.as_ref().is_none_or(is_blank);
        let title = view.as_ref().and_then(|v| v.title()).filter(|t| !t.is_empty());
        let host = view.as_ref().and_then(|v| v.uri()).and_then(|u| host_of(&u));
        let page_title = match (&title, &host) {
            _ if blank => "plain".to_owned(),
            (Some(title), _) => title.to_string(),
            (None, Some(host)) => host.clone(),
            (None, None) => "plain".to_owned(),
        };
        self.title.set_title(&page_title);
        self.title.set_subtitle(if blank { "" } else { host.as_deref().unwrap_or("") });
        self.window.set_title(Some(&page_title));
        self.refresh_tab_list();
        self.refresh_banner();
        self.update_bars();
    }

    fn refresh_tab_list(&self) {
        self.tab_list.remove_all();
        let current = self.current();
        let tabs = self.state.borrow().tabs.clone();
        for (i, view) in tabs.iter().enumerate() {
            let blank = is_blank(view);
            let host = view.uri().and_then(|u| host_of(&u)).unwrap_or_default();
            let title = if blank {
                "Пустая вкладка".to_owned()
            } else {
                view.title()
                    .map(|t| t.to_string())
                    .filter(|t| !t.is_empty())
                    .unwrap_or_else(|| host.clone())
            };
            let row = adw::ActionRow::builder()
                .title(glib::markup_escape_text(&title).as_str())
                .subtitle(glib::markup_escape_text(&host).as_str())
                .title_lines(1)
                .subtitle_lines(1)
                .activatable(false)
                .build();
            let number = gtk::Label::new(Some(&(i + 1).to_string()));
            number.add_css_class("dim-label");
            number.add_css_class("numeric");
            row.add_prefix(&number);
            if current.as_ref() == Some(view) {
                row.add_suffix(&gtk::Image::from_icon_name("object-select-symbolic"));
            }
            self.tab_list.append(&row);
        }
    }

    /// Which bars are out: the header bar while the line is open, the
    /// pointer is at the top or the tab is empty; the bottom bar while the
    /// line is open. Nothing in full screen.
    fn update_bars(&self) {
        let (line, tabs, near_top, fullscreen) = {
            let s = self.state.borrow();
            (s.line_open, s.tabs_open, s.pointer_near_top, s.fullscreen)
        };
        let blank = self.current().as_ref().is_none_or(is_blank);
        self.toolbar.set_reveal_top_bars(!fullscreen && (line || near_top || blank));
        self.toolbar.set_reveal_bottom_bars(!fullscreen && line);
        self.tab_list.set_visible(line && tabs);
        self.place_progress();
    }

    fn set_pointer_near_top(&self, near: bool) {
        if self.state.borrow().pointer_near_top == near {
            return;
        }
        self.state.borrow_mut().pointer_near_top = near;
        self.update_bars();
    }

    fn update_progress(&self) {
        match self.current() {
            Some(view) if view.is_loading() => {
                self.progress.set_fraction(view.estimated_load_progress().max(0.05));
                self.progress.set_visible(true);
            }
            _ => self.progress.set_visible(false),
        }
    }

    /// The page runs under the header bar; the loading bar sits just below it.
    fn place_progress(&self) {
        let covered = self.toolbar.reveals_top_bars() && self.toolbar.is_extend_content_to_top_edge();
        self.progress.set_margin_top(if covered { self.toolbar.top_bar_height() } else { 0 });
    }

    // --------------------------------------------------- engine requests

    fn handle_download(&self, download: &webkit6::Download) {
        // The folder the desktop calls Downloads; ~/Downloads if it has none.
        let dir = glib::user_special_dir(glib::UserDirectory::Downloads)
            .unwrap_or_else(|| glib::home_dir().join("Downloads"));
        let _ = std::fs::create_dir_all(&dir);
        let failed = Rc::new(Cell::new(false));
        let target: Rc<RefCell<Option<PathBuf>>> = Rc::default();

        let t = Rc::clone(&target);
        download.connect_decide_destination(move |download, suggested| {
            let path = input::unique_path(&dir, suggested);
            download.set_destination(&path.to_string_lossy());
            *t.borrow_mut() = Some(path);
            true
        });
        let weak = self.weak();
        let f = Rc::clone(&failed);
        download.connect_failed(move |download, error| {
            f.set(true);
            if error.matches(webkit6::DownloadError::CancelledByUser) {
                return;
            }
            if let Some(b) = weak.upgrade() {
                let name = download
                    .destination()
                    .map(|d| PathBuf::from(d.as_str()))
                    .and_then(|p| p.file_name().map(|n| n.to_string_lossy().into_owned()))
                    .unwrap_or_default();
                b.toast(&format!("Не удалось загрузить {name}"));
            }
        });
        let weak = self.weak();
        download.connect_finished(move |download| {
            if failed.get() {
                return;
            }
            let Some(b) = weak.upgrade() else { return };
            let Some(path) = target.borrow().clone() else { return };
            let pdf = download
                .response()
                .and_then(|r| r.mime_type())
                .is_some_and(|m| m == "application/pdf");
            if pdf {
                // No PDF viewer inside: hand it to the system.
                let uri = gio::File::for_path(&path).uri();
                let _ = gio::AppInfo::launch_default_for_uri(&uri, None::<&gio::AppLaunchContext>);
                b.toast(&format!("PDF сохранён и открыт: {}", input::short_path(&path)));
            } else {
                b.toast(&format!("Загружено: {}", input::short_path(&path)));
            }
        });
    }

    fn handle_permission(&self, view: &webkit6::WebView, request: &webkit6::PermissionRequest) {
        // Camera, microphone and screen are asked about; everything else
        // (notifications, location, clipboard, pointer lock…) is denied.
        let Some(media) = request.downcast_ref::<webkit6::UserMediaPermissionRequest>() else {
            request.deny();
            return;
        };
        let what = match (
            webkit6::functions::user_media_permission_is_for_video_device(media),
            webkit6::functions::user_media_permission_is_for_audio_device(media),
            webkit6::functions::user_media_permission_is_for_display_device(media),
        ) {
            (_, _, true) => "доступ к экрану",
            (true, true, _) => "доступ к камере и микрофону",
            (true, false, _) => "доступ к камере",
            (false, true, _) => "доступ к микрофону",
            _ => {
                request.deny();
                return;
            }
        };
        let site = view.uri().and_then(|u| host_of(&u)).unwrap_or_else(|| "Сайт".to_owned());
        self.state.borrow_mut().questions.push_back(Question {
            view: view.downgrade(),
            text: format!("{site} запрашивает {what}"),
            request: request.clone(),
        });
        self.show_next_question();
    }

    fn show_next_question(&self) {
        {
            let mut s = self.state.borrow_mut();
            // Questions from tabs closed in the meantime are dropped.
            while s.questions.front().is_some_and(|q| q.view.upgrade().is_none()) {
                s.questions.pop_front();
            }
            if s.question_shown || s.questions.is_empty() {
                return;
            }
            s.question_shown = true;
            s.tabs_open = false;
        }
        self.entry.set_placeholder_text(None);
        self.key_hint.set_visible(true);
        self.refresh_banner();
        self.open_line("");
    }

    fn answer_question(&self, granted: bool) {
        let question = {
            let mut s = self.state.borrow_mut();
            s.question_shown = false;
            s.questions.pop_front()
        };
        if let Some(q) = question {
            if q.view.upgrade().is_some() {
                if granted {
                    q.request.allow();
                } else {
                    q.request.deny();
                }
            }
        }
        self.entry.set_placeholder_text(Some(PLACEHOLDER));
        self.key_hint.set_visible(false);
        self.refresh_banner();
    }

    fn handle_certificate_error(
        &self,
        view: &webkit6::WebView,
        uri: &str,
        certificate: &gio::TlsCertificate,
        errors: gio::TlsCertificateFlags,
    ) {
        let host = host_of(uri).unwrap_or_default();
        let reason = if errors.contains(gio::TlsCertificateFlags::UNKNOWN_CA) {
            "выдан неизвестным центром сертификации"
        } else if errors.contains(gio::TlsCertificateFlags::BAD_IDENTITY) {
            "выдан для другого сайта"
        } else if errors.contains(gio::TlsCertificateFlags::EXPIRED) {
            "срок действия истёк"
        } else if errors.contains(gio::TlsCertificateFlags::NOT_ACTIVATED) {
            "ещё не вступил в силу"
        } else if errors.contains(gio::TlsCertificateFlags::REVOKED) {
            "отозван"
        } else if errors.contains(gio::TlsCertificateFlags::INSECURE) {
            "использует ненадёжный алгоритм"
        } else {
            "содержит ошибки"
        };
        self.drop_certificate(view, false);
        self.state.borrow_mut().certificates.push(CertificateError {
            view: view.downgrade(),
            uri: uri.to_owned(),
            text: format!("Сертификат {host} недействителен: {reason}. Открыть всё равно — :proceed"),
            host,
            certificate: certificate.clone(),
        });
        self.refresh_banner();
    }

    fn take_certificate(&self, view: &webkit6::WebView) -> Option<CertificateError> {
        let mut s = self.state.borrow_mut();
        let i = s.certificates.iter().position(|c| c.view.upgrade().as_ref() == Some(view))?;
        Some(s.certificates.remove(i))
    }

    fn drop_certificate(&self, view: &webkit6::WebView, refresh: bool) {
        if self.take_certificate(view).is_some() && refresh {
            self.refresh_banner();
        }
    }

    /// The banner shows the question being asked, or else what holds the
    /// current tab back. While it is out, the page no longer runs under the
    /// header bar, so the banner is never covered.
    fn refresh_banner(&self) {
        let current = self.current();
        let text = {
            let s = self.state.borrow();
            if s.question_shown {
                s.questions.front().map(|q| q.text.clone())
            } else {
                s.certificates
                    .iter()
                    .find(|c| c.view.upgrade().is_some() && c.view.upgrade() == current)
                    .map(|c| c.text.clone())
            }
        };
        match text {
            Some(text) => {
                self.banner.set_title(&glib::markup_escape_text(&text));
                self.banner.set_revealed(true);
                self.toolbar.set_extend_content_to_top_edge(false);
            }
            None => {
                self.banner.set_revealed(false);
                self.toolbar.set_extend_content_to_top_edge(true);
            }
        }
        self.place_progress();
    }
}

fn is_blank(view: &webkit6::WebView) -> bool {
    view.uri().is_none_or(|u| u.is_empty() || u == "about:blank")
}

/// An empty tab shows the window behind it; a real page gets white, as every
/// site without its own background expects.
fn set_page_background(view: &webkit6::WebView) {
    let color = if is_blank(view) {
        gdk::RGBA::new(0.0, 0.0, 0.0, 0.0)
    } else {
        gdk::RGBA::WHITE
    };
    view.set_background_color(&color);
}

fn host_of(uri: &str) -> Option<String> {
    glib::Uri::parse(uri, glib::UriFlags::NONE)
        .ok()
        .and_then(|u| u.host())
        .map(|h| h.to_string())
        .filter(|h| !h.is_empty())
}

fn dim_label(text: &str) -> gtk::Label {
    let label = gtk::Label::new(Some(text));
    label.add_css_class("dim-label");
    label.add_css_class("caption");
    label
}
