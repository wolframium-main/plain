//! plain — a browser that does exactly what you tell it.
//! Why it is the way it is: DESIGN.md.

mod adblock;
mod browser;
mod input;

use adw::prelude::*;
use gtk::{gio, glib};

pub const APP_ID: &str = "io.github.wolframium_main.Plain";

fn main() -> glib::ExitCode {
    let app = adw::Application::builder()
        .application_id(APP_ID)
        // One window only: a second `plain URL` hands the URL to this one.
        .flags(gio::ApplicationFlags::HANDLES_COMMAND_LINE)
        .build();

    app.connect_command_line(|app, command_line| {
        let browser = browser::Browser::for_app(app);
        let cwd = command_line.cwd();
        let urls: Vec<String> = command_line
            .arguments()
            .iter()
            .skip(1)
            .filter_map(|arg| arg.to_str())
            .filter(|arg| !arg.starts_with('-'))
            .map(|arg| match &cwd {
                // A file next to where plain was started.
                Some(dir) if !arg.contains("://") && dir.join(arg).exists() => {
                    gio::File::for_path(dir.join(arg)).uri().to_string()
                }
                _ => input::url_from_input(arg),
            })
            .collect();
        browser.present(&urls);
        glib::ExitCode::SUCCESS
    });

    app.run()
}
