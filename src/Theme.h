#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

// The Wolframium design system, theme "Ночь" (dark). plain draws only a
// bottom panel, a loading line and a context menu; everything else is the
// page. Where Wolframium has no rule, the rule chosen for plain is written
// down in DESIGN.md, "Внешний вид".
namespace Theme {

// Colors (tokens.json, theme "dark")
inline const QColor bg{0x10, 0x0d, 0x18};            // bg
inline const QColor surface{0x17, 0x13, 0x25};       // surface
inline const QColor surfaceRaised{0x1f, 0x1a, 0x31}; // surface-raised
inline const QColor line{0x2c, 0x26, 0x45};          // line
inline const QColor lineStrong{0x6e, 0x6b, 0x82};    // line-strong
inline const QColor ink{0xe9, 0xe7, 0xef};           // ink
inline const QColor ink2{0xb3, 0xb0, 0xc0};          // ink-2
inline const QColor ink3{0x8f, 0x8c, 0x9f};          // ink-3
inline const QColor violet{0x3b, 0x24, 0x75};        // violet
inline const QColor onViolet{0xee, 0xec, 0xf3};      // on-violet
inline const QColor amber{0xf2, 0xa9, 0x3b};         // amber
inline const QColor focus = amber;                   // focus = {amber} in dark
inline const QColor danger{0xf4, 0x7c, 0x86};        // danger

// Spacing (8px grid)
constexpr int space1 = 8;
constexpr int space2 = 16;
constexpr int space3 = 24;

// Strokes
constexpr int stroke = 1;
constexpr int focusWidth = 2;
constexpr int focusOffset = 2;
constexpr int radius = 2;
constexpr int dataLine = 2; // "Линии данных толщиной 2px"

// Derived sizes
constexpr int fieldHeight = 40; // fields and pressables
constexpr int column = 640;     // longest line of text: 68 characters of body
constexpr int bodyLine = 24;    // line height of body and code
constexpr int metaLine = 16;    // line height of meta

// Motion
constexpr int motionMs = 120;   // ease-out cubic-bezier(0.215, 0.61, 0.355, 1)
constexpr int motionShift = 8;  // the largest allowed shift

// Text styles
QFont body();       // IBM Plex Sans 15px 400
QFont bodyStrong(); // IBM Plex Sans 15px 600
QFont meta();       // IBM Plex Mono 12px 400
QFont code();       // IBM Plex Mono 14px 400

// Plain text set in a style's line height, for labels that may wrap.
QString paragraph(const QString &text, const QColor &color, int lineHeight = bodyLine);

QPalette palette();
QString menuStyleSheet();

// The desktop asks for less motion (KDE: animation speed at zero; GNOME:
// animations off). Checked once.
bool reduceMotion();

} // namespace Theme
