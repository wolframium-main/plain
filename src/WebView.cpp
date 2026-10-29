#include "WebView.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QMenu>
#include <QWebEngineContextMenuRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>

WebView::WebView(QWebEngineProfile *profile, QWidget *parent)
    : QWebEngineView(parent)
{
    setPage(new QWebEnginePage(profile, this));

    connect(this, &QWebEngineView::loadStarted, this, [this] {
        m_loading = true;
        m_progress = 0;
    });
    connect(this, &QWebEngineView::loadProgress, this, [this](int value) {
        m_progress = value;
    });
    connect(this, &QWebEngineView::loadFinished, this, [this] {
        m_loading = false;
        m_progress = 100;
    });
}

void WebView::contextMenuEvent(QContextMenuEvent *event)
{
    const QWebEngineContextMenuRequest *request = lastContextMenuRequest();
    if (!request)
        return;

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setFont(Theme::bodyStrong()); // pressable labels are body-strong
    menu->setStyleSheet(Theme::menuStyleSheet());
    // The corners outside the 2px radius stay transparent.
    menu->setAttribute(Qt::WA_TranslucentBackground);

    auto addAction = [this, menu](const QString &text, QWebEnginePage::WebAction action) {
        connect(menu->addAction(text), &QAction::triggered, this,
                [this, action] { page()->triggerAction(action); });
    };

    if (!request->selectedText().isEmpty())
        addAction(tr("Копировать"), QWebEnginePage::Copy);
    if (request->linkUrl().isValid())
        addAction(tr("Копировать ссылку"), QWebEnginePage::CopyLinkToClipboard);
    if (request->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage)
        addAction(tr("Сохранить картинку"), QWebEnginePage::DownloadImageToDisk);

    // Nothing applies to what was clicked: no menu at all.
    if (menu->isEmpty()) {
        delete menu;
        return;
    }
    menu->popup(event->globalPos());

    // Appearing: opacity 0 → 1 in 120ms ease-out. Wayland does not let a
    // window set its own opacity, so there the menu simply appears.
    if (!Theme::reduceMotion() && QGuiApplication::platformName() != QLatin1String("wayland")) {
        auto *fade = new QPropertyAnimation(menu, "windowOpacity", menu);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setDuration(Theme::motionMs);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
