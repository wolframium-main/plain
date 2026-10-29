#include "Theme.h"

#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace Theme {

namespace {

QFont font(const QString &family, QFont::StyleHint hint, int pixelSize, QFont::Weight weight)
{
    QFont f(family);
    f.setStyleHint(hint); // falls back to the system font of that kind
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    return f;
}

} // namespace

QFont body()
{
    return font(QStringLiteral("IBM Plex Sans"), QFont::SansSerif, 15, QFont::Normal);
}

QFont bodyStrong()
{
    return font(QStringLiteral("IBM Plex Sans"), QFont::SansSerif, 15, QFont::DemiBold);
}

QFont meta()
{
    return font(QStringLiteral("IBM Plex Mono"), QFont::Monospace, 12, QFont::Normal);
}

QFont code()
{
    return font(QStringLiteral("IBM Plex Mono"), QFont::Monospace, 14, QFont::Normal);
}

QString paragraph(const QString &text, const QColor &color, int lineHeight)
{
    return QStringLiteral("<p style=\"margin:0; line-height:%1px; color:%2\">%3</p>")
        .arg(lineHeight)
        .arg(color.name(), text.toHtmlEscaped());
}

QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, ink);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, surface);
    p.setColor(QPalette::Text, ink);
    p.setColor(QPalette::PlaceholderText, ink3);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, ink);
    p.setColor(QPalette::Mid, line);
    p.setColor(QPalette::Highlight, violet); // text selection
    p.setColor(QPalette::HighlightedText, onViolet);
    p.setColor(QPalette::ToolTipBase, surfaceRaised);
    p.setColor(QPalette::ToolTipText, ink);
    return p;
}

QString menuStyleSheet()
{
    // A container one step above the page: surface-raised, a "line" border,
    // the one radius, no shadow. Items are quiet pressables: body-strong,
    // space-1 / space-2, 40px. The item under the pointer or the keyboard is
    // a highlighted block: violet under on-violet.
    return QStringLiteral(
               "QMenu { background: %1; border: %2px solid %3; border-radius: %4px;"
               " padding: 0; }"
               "QMenu::item { color: %5; padding: %6px %7px; min-height: %8px;"
               " border-radius: %4px; }"
               "QMenu::item:selected { background: %9; color: %10; }")
        .arg(surfaceRaised.name())
        .arg(stroke)
        .arg(line.name())
        .arg(radius)
        .arg(ink.name())
        .arg(space1)
        .arg(space2)
        .arg(bodyLine)
        .arg(violet.name(), onViolet.name());
}

bool reduceMotion()
{
    static const bool reduced = [] {
        // KDE: System Settings → Animation speed → Instant.
        const QString kdeglobals = QStandardPaths::locate(
            QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
        if (!kdeglobals.isEmpty()) {
            QSettings kde(kdeglobals, QSettings::IniFormat);
            bool ok = false;
            const double factor =
                kde.value(QStringLiteral("KDE/AnimationDurationFactor")).toDouble(&ok);
            if (ok && factor == 0.0)
                return true;
        }
        // GNOME and everything that follows its settings.
        const QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
        if (!gsettings.isEmpty()) {
            QProcess process;
            process.start(gsettings, {QStringLiteral("get"),
                                      QStringLiteral("org.gnome.desktop.interface"),
                                      QStringLiteral("enable-animations")});
            if (process.waitForFinished(500)
                && process.readAllStandardOutput().trimmed() == "false")
                return true;
        }
        return false;
    }();
    return reduced;
}

} // namespace Theme
