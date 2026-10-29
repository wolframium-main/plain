#include "Browser.h"
#include "Panel.h"
#include "Quirks.h"
#include "Theme.h"
#include "WebView.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFindTextResult>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineNewWindowRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QWebEnginePermission>
#endif

namespace {

// What a permission request wants, in words. Empty means "deny without asking".
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
QString askableDevice(QWebEnginePermission::PermissionType type)
{
    using Type = QWebEnginePermission::PermissionType;
    switch (type) {
    case Type::MediaAudioCapture: return QObject::tr("микрофон");
    case Type::MediaVideoCapture: return QObject::tr("камеру");
    case Type::MediaAudioVideoCapture: return QObject::tr("камеру и микрофон");
    case Type::DesktopVideoCapture: return QObject::tr("экран");
    case Type::DesktopAudioVideoCapture: return QObject::tr("экран и звук");
    default: return QString();
    }
}
#else
QString askableDevice(QWebEnginePage::Feature feature)
{
    switch (feature) {
    case QWebEnginePage::MediaAudioCapture: return QObject::tr("микрофон");
    case QWebEnginePage::MediaVideoCapture: return QObject::tr("камеру");
    case QWebEnginePage::MediaAudioVideoCapture: return QObject::tr("камеру и микрофон");
    case QWebEnginePage::DesktopVideoCapture: return QObject::tr("экран");
    case QWebEnginePage::DesktopAudioVideoCapture: return QObject::tr("экран и звук");
    default: return QString();
    }
}
#endif

constexpr int MessageTimeoutMs = 4000;

bool isBlank(const QUrl &url)
{
    return url.isEmpty() || url == QUrl(QStringLiteral("about:blank"));
}

QUrl searchUrl(const QString &text)
{
    QUrl url(QStringLiteral("https://www.google.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), text);
    url.setQuery(query);
    return url;
}

// What the user typed into the line: an address or a Google search.
QUrl urlFromInput(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s"));
    static const QRegularExpression scheme(
        QStringLiteral("^(about|data|file|view-source):|^[a-zA-Z][a-zA-Z0-9+.-]*://"));

    // An explicit scheme always means an address, even with spaces in it.
    if (scheme.match(text).hasMatch())
        return QUrl(text);
    if (text.contains(whitespace))
        return searchUrl(text);
    if (text.startsWith(QLatin1Char('/')))
        return QUrl::fromLocalFile(text);
    if (text.startsWith(QLatin1String("~/")))
        return QUrl::fromLocalFile(QDir::homePath() + text.mid(1));
    if (text.contains(QLatin1Char('.')) || text.startsWith(QLatin1String("localhost"))) {
        const QUrl url = QUrl::fromUserInput(text);
        if (url.isValid())
            return url;
    }
    return searchUrl(text);
}

// A real example of each kind of input, as the design system asks.
QString defaultPlaceholder()
{
    return QObject::tr("doc.qt.io, маятник Фуко или :tabs");
}

QString shortPath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? QLatin1Char('~') + path.mid(home.size()) : path;
}

} // namespace

Browser::Browser(QWebEngineProfile *profile, AdBlocker *adBlocker, QWidget *parent)
    : QMainWindow(parent)
    , m_profile(profile)
{
    // Wolframium, theme "Ночь": the same everywhere, whatever the desktop.
    setPalette(Theme::palette());
    setAutoFillBackground(true);
    // Page area: tabs on the left, developer tools on the right when open.
    m_stack = new QStackedWidget;
    m_stack->installEventFilter(this);

    m_devTools = new QWebEngineView;
    m_devTools->setPage(new QWebEnginePage(m_profile, m_devTools));
    m_devTools->hide();

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_stack);
    m_splitter->addWidget(m_devTools);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(Theme::stroke);
    m_splitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: %1; }").arg(Theme::line.name()));

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    setCentralWidget(central);
    central->installEventFilter(this);

    // Loading: a 2px line, "ink-3", along the bottom edge of the page (or of
    // the panel, when it is open). Progress is a value, and values are data
    // lines: 2px; ink-3 reads on white pages (3.3:1) and on bg (5.9:1).
    m_progress = new QProgressBar(central);
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { border: none; background: transparent; }"
        "QProgressBar::chunk { background: %1; }").arg(Theme::ink3.name()));
    m_progress->hide();

    // Bottom panel, drawn over the page: tab list, message, command field.
    m_panel = new Panel(central);
    m_tabList = m_panel->tabList();
    m_message = m_panel->message();
    m_line = m_panel->line();
    m_line->setPlaceholderText(defaultPlaceholder());
    m_line->installEventFilter(this);
    connect(m_line, &QLineEdit::returnPressed, this, [this] { execute(m_line->text()); });
    connect(m_panel, &Panel::topChanged, this, &Browser::placeProgress);

    m_messageTimer.setSingleShot(true);
    connect(&m_messageTimer, &QTimer::timeout, m_message, &QWidget::hide);

    // The one and only hotkey: the key that carries the colon.
    auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, [this] { openCommandLine(); });

    connect(m_profile, &QWebEngineProfile::downloadRequested, this, &Browser::handleDownload);

    // Present plain as the Chrome it is built on. The "QtWebEngine/x.y" token
    // makes Google treat it as a bot and show captchas.
    static const QRegularExpression qtToken(QStringLiteral("QtWebEngine/\\S+\\s*"));
    m_profile->setHttpUserAgent(m_profile->httpUserAgent().remove(qtToken));

    // The one site-specific exception: Google sign-in (see Quirks.h).
    Quirks::install(m_profile);
    // Owned by the profile, so it lives exactly as long as the requests it filters.
    m_profile->setUrlRequestInterceptor(new Quirks::Interceptor(adBlocker, m_profile));

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Permission answers are never remembered: every request is asked again.
    m_profile->setPersistentPermissionsPolicy(
        QWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);
#endif

    createTab(true);
}

// ---------------------------------------------------------------- tabs

WebView *Browser::createTab(bool makeCurrent)
{
    auto *view = new WebView(m_profile);
    m_tabs.append(view);
    m_stack->addWidget(view);

    connect(view, &QWebEngineView::titleChanged, this, [this, view] {
        if (view == currentTab())
            updateWindowTitle();
        refreshTabList();
    });
    connect(view, &QWebEngineView::urlChanged, this, &Browser::refreshTabList);

    // An empty tab is "bg". A real page gets white underneath, as every site
    // without its own background expects.
    view->page()->setBackgroundColor(Theme::bg);
    connect(view, &QWebEngineView::urlChanged, this, [view](const QUrl &url) {
        view->page()->setBackgroundColor(isBlank(url) ? Theme::bg : QColor(Qt::white));
    });
    connect(view, &QWebEngineView::loadStarted, this, [this, view] {
        if (view == currentTab())
            updateProgress();
    });
    connect(view, &QWebEngineView::loadProgress, this, [this, view] {
        if (view == currentTab())
            updateProgress();
    });
    connect(view, &QWebEngineView::loadFinished, this, [this, view] {
        if (view == currentTab())
            updateProgress();
    });
    connect(view, &QWebEngineView::renderProcessTerminated, this,
            [this, view](QWebEnginePage::RenderProcessTerminationStatus status) {
                if (status != QWebEnginePage::NormalTerminationStatus)
                    showMessage(tr("Вкладка %1 упала. :reload перезагрузит её.")
                                    .arg(m_tabs.indexOf(view) + 1),
                                MessageKind::Error);
            });

    QWebEnginePage *page = view->page();
    connect(page, &QWebEnginePage::newWindowRequested, this, &Browser::handleNewWindowRequest);
    connect(page, &QWebEnginePage::fullScreenRequested, this, &Browser::handleFullScreen);
    connectPermissions(page);
    connect(page, &QWebEnginePage::certificateError, this,
            [this, view](const QWebEngineCertificateError &error) {
                handleCertificateError(view, error);
            });
    connect(page, &QWebEnginePage::windowCloseRequested, this, [this, view] {
        const int index = m_tabs.indexOf(view);
        if (index >= 0 && closeTab(index))
            openCommandLine();
    });

    if (makeCurrent)
        setCurrentTab(view);
    refreshTabList();
    return view;
}

WebView *Browser::currentTab() const
{
    return qobject_cast<WebView *>(m_stack->currentWidget());
}

void Browser::setCurrentTab(WebView *view)
{
    m_stack->setCurrentWidget(view);
    if (m_devTools->isVisible())
        view->page()->setDevToolsPage(m_devTools->page());
    updateWindowTitle();
    updateProgress();
    refreshTabList();
    if (m_certificateErrors.count(view))
        showCertificateMessage(view);
    else
        hideStickyMessage();
    if (!m_line->isVisible())
        view->setFocus();
}

// Returns true when the last tab was closed and a fresh empty one took its
// place: the caller should then open the command line.
bool Browser::closeTab(int index)
{
    WebView *view = m_tabs.takeAt(index);
    rejectPendingCertificate(view);
    const bool wasCurrent = view == currentTab();
    if (m_devTools->isVisible() && wasCurrent)
        view->page()->setDevToolsPage(nullptr);
    m_stack->removeWidget(view);
    view->deleteLater();

    if (m_tabs.isEmpty()) {
        createTab(true);
        return true;
    }
    if (wasCurrent)
        setCurrentTab(m_tabs.at(qMin(index, m_tabs.size() - 1)));
    refreshTabList();
    return false;
}

void Browser::requestNewTab(const QUrl &url, bool background)
{
    if (m_tabs.size() >= MaxTabs) {
        m_hasPending = true;
        m_pendingUrl = url;
        showLimitReached();
        return;
    }
    WebView *view = createTab(!background);
    if (!isBlank(url))
        view->load(url);
    if (background)
        showMessage(tr("Открыта вкладка %1").arg(m_tabs.size()));
}

void Browser::handleNewWindowRequest(QWebEngineNewWindowRequest &request)
{
    // A new tab is a new tab: the site asked for one, it gets one.
    const bool background =
        request.destination() == QWebEngineNewWindowRequest::InNewBackgroundTab;

    if (m_tabs.size() >= MaxTabs) {
        // The request can't wait; remember the address instead.
        m_hasPending = true;
        m_pendingUrl = request.requestedUrl();
        showLimitReached();
        return;
    }
    WebView *view = createTab(!background);
    request.openIn(view->page()); // keeps window.opener: sign-in popups work
    if (background)
        showMessage(tr("Открыта вкладка %1").arg(m_tabs.size()));
}

void Browser::showLimitReached()
{
    showMessage(tr("Открыто %1 вкладок. Закрой одну — новая откроется сама.").arg(MaxTabs));
    m_tabList->show();
    refreshTabList();
    m_line->show();
    m_line->setText(QStringLiteral(":close "));
    m_line->setFocus();
}

// Opens the tab that waited for a free slot. Returns true when that tab is
// empty and wants the command line open.
bool Browser::openPendingTab()
{
    if (!m_hasPending || m_tabs.size() >= MaxTabs)
        return false;
    const QUrl url = m_pendingUrl;
    m_hasPending = false;
    m_pendingUrl.clear();
    WebView *view = createTab(true);
    if (isBlank(url))
        return true;
    view->load(url);
    return false;
}

void Browser::openExternalUrl(const QUrl &url)
{
    WebView *tab = currentTab();
    if (tab && isBlank(tab->url()) && !tab->isLoading()) {
        tab->load(url);
        hideCommandLine();
    } else {
        requestNewTab(url, false);
    }
    raise();
    activateWindow();
}

// -------------------------------------------------------- command line

void Browser::openCommandLine(const QString &prefill)
{
    m_line->setText(prefill);
    m_line->show();
    m_line->setFocus();
    m_line->end(false);
}

void Browser::hideCommandLine()
{
    // Leaving the line without an answer means "no".
    if (m_questionShown)
        answerQuestion(false);
    // Leaving the line abandons a tab that waited for a free slot.
    m_hasPending = false;
    m_pendingUrl.clear();
    m_line->clear();
    m_line->hide();
    m_tabList->hide();
    if (WebView *tab = currentTab())
        tab->setFocus();
    if (!m_questions.isEmpty())
        QTimer::singleShot(0, this, &Browser::showNextQuestion);
}

void Browser::execute(const QString &rawInput)
{
    const QString input = rawInput.trimmed();
    bool close = true;

    if (m_questionShown) {
        // "н" and "д": the y key in the Russian layout, and the word itself.
        static const QStringList yes = {QStringLiteral("y"), QStringLiteral("yes"),
                                        QStringLiteral("н"), QStringLiteral("д"),
                                        QStringLiteral("да")};
        answerQuestion(yes.contains(input.toLower()));
        hideCommandLine();
        return;
    }

    if (input.startsWith(QLatin1Char(':'))) {
        const QString body = input.mid(1).trimmed();
        const int space = body.indexOf(QLatin1Char(' '));
        const QString name = space < 0 ? body : body.left(space);
        const QString arg = space < 0 ? QString() : body.mid(space + 1).trimmed();
        if (!name.isEmpty())
            close = runCommand(name, arg);
    } else if (!input.isEmpty()) {
        if (WebView *tab = currentTab()) {
            rejectPendingCertificate(tab); // going elsewhere means "no"
            tab->load(urlFromInput(input));
        }
    }

    if (close)
        hideCommandLine();
}

// Returns true when the command line should close afterwards. When it returns
// false the command has already put the line into the state it needs.
bool Browser::runCommand(const QString &name, const QString &arg)
{
    WebView *tab = currentTab();

    auto parseTabNumber = [this](const QString &text) {
        bool ok = false;
        const int n = text.toInt(&ok);
        return ok && n >= 1 && n <= m_tabs.size() ? n - 1 : -1;
    };

    if (name == QLatin1String("new")) {
        if (m_tabs.size() >= MaxTabs) {
            m_hasPending = true;
            m_pendingUrl = arg.isEmpty() ? QUrl() : urlFromInput(arg);
            showLimitReached();
            return false;
        }
        WebView *view = createTab(true);
        if (arg.isEmpty()) {
            openCommandLine();
            return false;
        }
        view->load(urlFromInput(arg));
        return true;
    }

    if (name == QLatin1String("tabs")) {
        refreshTabList();
        m_tabList->show();
        m_line->clear();
        return false;
    }

    if (name == QLatin1String("tab")) {
        const int index = parseTabNumber(arg);
        if (index < 0) {
            showMessage(tr("Нет вкладки «%1»").arg(arg), MessageKind::Error);
            return true;
        }
        setCurrentTab(m_tabs.at(index));
        return true;
    }

    if (name == QLatin1String("close")) {
        int index = tab ? m_tabs.indexOf(tab) : -1;
        if (!arg.isEmpty())
            index = parseTabNumber(arg);
        if (index < 0) {
            showMessage(tr("Нет вкладки «%1»").arg(arg), MessageKind::Error);
            if (m_hasPending) {
                m_line->setText(QStringLiteral(":close "));
                return false;
            }
            return true;
        }
        const bool lastTabReplaced = closeTab(index);
        const bool pendingTabIsEmpty = openPendingTab();
        if (lastTabReplaced || pendingTabIsEmpty) {
            m_tabList->hide();
            openCommandLine();
            return false;
        }
        return true;
    }

    if (name == QLatin1String("back") || name == QLatin1String("forward")
        || name == QLatin1String("reload")) {
        if (!tab)
            return true;
        rejectPendingCertificate(tab);
        if (name == QLatin1String("back"))
            tab->back();
        else if (name == QLatin1String("forward"))
            tab->forward();
        else
            tab->reload();
        return true;
    }

    if (name == QLatin1String("proceed")) {
        if (!tab || !m_certificateErrors.count(tab)) {
            showMessage(tr("В этой вкладке нет страницы с недействительным сертификатом"),
                        MessageKind::Error);
            return true;
        }
        const auto it = m_certificateErrors.find(tab);
        QWebEngineCertificateError error = it->second;
        m_certificateErrors.erase(it);
        error.acceptCertificate();
        hideStickyMessage();
        return true;
    }

    if (name == QLatin1String("find")) {
        if (!tab)
            return true;
        tab->findText(arg, {}, [this, arg](const QWebEngineFindTextResult &result) {
            if (arg.isEmpty())
                return;
            if (result.numberOfMatches() == 0)
                showMessage(tr("Не найдено: %1").arg(arg));
            else
                showMessage(tr("Найдено: %1 из %2")
                                .arg(result.activeMatch())
                                .arg(result.numberOfMatches()));
        });
        return true;
    }

    if (name == QLatin1String("zoom")) {
        if (!tab)
            return true;
        if (arg.isEmpty()) {
            showMessage(tr("Масштаб: %1%").arg(qRound(tab->zoomFactor() * 100)));
            return true;
        }
        bool ok = false;
        const int percent = QString(arg).remove(QLatin1Char('%')).toInt(&ok);
        if (!ok || percent < 25 || percent > 500) {
            showMessage(tr("Такого масштаба нет. Введи число от 25 до 500"), MessageKind::Error);
            return true;
        }
        tab->setZoomFactor(percent / 100.0);
        showMessage(tr("Масштаб: %1%").arg(percent));
        return true;
    }

    if (name == QLatin1String("url")) {
        if (!tab || isBlank(tab->url())) {
            showMessage(tr("Во вкладке ничего не открыто"));
            return true;
        }
        const QString url = tab->url().toString();
        QGuiApplication::clipboard()->setText(url);
        showMessage(tr("Скопировано: %1").arg(url));
        return true;
    }

    if (name == QLatin1String("devtools")) {
        toggleDevTools();
        return true;
    }

    if (name == QLatin1String("quit")) {
        QApplication::quit();
        return true;
    }

    // Keep the typo on screen so it can be fixed.
    showMessage(tr("Нет такой команды: :%1").arg(name), MessageKind::Error);
    return false;
}

// ------------------------------------------------------------ feedback

void Browser::refreshTabList()
{
    const WebView *current = currentTab();
    QList<Panel::Tab> tabs;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const WebView *view = m_tabs.at(i);
        const QUrl url = view->url();
        Panel::Tab tab;
        tab.current = view == current;
        tab.meta = QString::number(i + 1);
        if (isBlank(url)) {
            tab.title = tr("Пустая вкладка");
        } else {
            const QString domain = url.host();
            if (!domain.isEmpty())
                tab.meta += QStringLiteral(" · ") + domain;
            tab.title = view->title();
            if (tab.title.isEmpty() || tab.title == url.toString())
                tab.title = domain.isEmpty() ? url.toString() : domain;
        }
        tabs << tab;
    }
    m_panel->setTabs(tabs);
}

void Browser::updateWindowTitle()
{
    const WebView *tab = currentTab();
    const QString title = tab && !isBlank(tab->url()) ? tab->title() : QString();
    setWindowTitle(title.isEmpty() ? QStringLiteral("plain") : title + QStringLiteral(" — plain"));
}

void Browser::updateProgress()
{
    const WebView *tab = currentTab();
    if (tab && tab->isLoading()) {
        m_progress->setValue(qMax(5, tab->progress()));
        m_progress->show();
        placeProgress();
    } else {
        m_progress->hide();
    }
}

void Browser::placeProgress()
{
    QWidget *central = centralWidget();
    const QPoint at = m_stack->mapTo(central, QPoint(0, 0));
    const int bottom = qMin(m_panel->top(), at.y() + m_stack->height());
    m_progress->setGeometry(at.x(), bottom - Theme::dataLine, m_stack->width(),
                            Theme::dataLine);
    m_progress->raise();
}

void Browser::showMessage(const QString &text, MessageKind kind)
{
    m_messageSticky = false;
    // Results are "ink-2"; an error is "danger", always with words.
    m_panel->setMessage(text, kind == MessageKind::Error ? Theme::danger : Theme::ink2);
    m_message->show();
    m_messageTimer.start(MessageTimeoutMs);
}

// A message that stays until what it describes is resolved.
void Browser::showStickyMessage(const QString &text)
{
    m_messageTimer.stop();
    m_messageSticky = true;
    m_panel->setMessage(text, Theme::ink);
    m_message->show();
}

void Browser::hideStickyMessage()
{
    if (!m_messageSticky)
        return;
    m_messageSticky = false;
    m_message->hide();
}

bool Browser::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_stack || watched == centralWidget()) && event->type() == QEvent::Resize) {
        placeProgress();
    } else if (watched == m_line) {
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            hideCommandLine();
            return true;
        }
        if (event->type() == QEvent::FocusOut
            && static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason) {
            hideCommandLine(); // clicked back into the page
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ------------------------------------------------------- engine requests

void Browser::handleDownload(QWebEngineDownloadRequest *download)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QDir().mkpath(dir);

    // Never overwrite: file.txt, file (1).txt, file (2).txt...
    QString name = download->downloadFileName();
    if (name.isEmpty())
        name = QStringLiteral("download");
    const QFileInfo original(name);
    const QString base = original.completeBaseName();
    const QString suffix = original.suffix();
    for (int n = 1; QFileInfo::exists(QDir(dir).filePath(name)); ++n) {
        name = suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n)
                                : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
    }

    download->setDownloadDirectory(dir);
    download->setDownloadFileName(name);
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [this, download] {
        if (!download->isFinished())
            return;
        const QString path =
            QDir(download->downloadDirectory()).filePath(download->downloadFileName());
        if (download->state() != QWebEngineDownloadRequest::DownloadCompleted) {
            showMessage(tr("Загрузка не удалась: %1").arg(download->downloadFileName()),
                        MessageKind::Error);
        } else if (download->mimeType() == QLatin1String("application/pdf")) {
            // No PDF viewer inside: hand it to the system.
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            showMessage(tr("PDF сохранён и открыт: %1").arg(shortPath(path)));
        } else {
            showMessage(tr("Загружено: %1").arg(shortPath(path)));
        }
    });
    download->accept();
    showMessage(tr("Загрузка: %1").arg(name));
}

void Browser::handleFullScreen(QWebEngineFullScreenRequest request)
{
    if (request.toggleOn()) {
        hideCommandLine();
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
    request.accept();
}

void Browser::connectPermissions(QWebEnginePage *page)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    connect(page, &QWebEnginePage::permissionRequested, this,
            [this, page](QWebEnginePermission permission) {
                const QString what = askableDevice(permission.permissionType());
                if (what.isEmpty()) {
                    permission.deny();
                    return;
                }
                askPermission(page, permission.origin(), what, [permission](bool granted) {
                    if (!permission.isValid())
                        return;
                    if (granted)
                        permission.grant();
                    else
                        permission.deny();
                    // Otherwise Qt 6.8+ remembers the answer for the tab's lifetime.
                    permission.reset();
                });
            });
#else
    connect(page, &QWebEnginePage::featurePermissionRequested, this,
            [this, page](const QUrl &origin, QWebEnginePage::Feature feature) {
                const QString what = askableDevice(feature);
                if (what.isEmpty()) {
                    page->setFeaturePermission(origin, feature,
                                               QWebEnginePage::PermissionDeniedByUser);
                    return;
                }
                QPointer<QWebEnginePage> guard(page);
                askPermission(page, origin, what, [guard, origin, feature](bool granted) {
                    if (guard)
                        guard->setFeaturePermission(
                            origin, feature,
                            granted ? QWebEnginePage::PermissionGrantedByUser
                                    : QWebEnginePage::PermissionDeniedByUser);
                });
            });
#endif
}

void Browser::askPermission(QWebEnginePage *page, const QUrl &origin, const QString &what,
                            std::function<void(bool)> answer)
{
    const QString site = origin.host().isEmpty() ? origin.toString() : origin.host();
    m_questions.append({page, tr("%1 просит %2. Разрешить?").arg(site, what),
                        std::move(answer)});
    if (!m_questionShown)
        showNextQuestion();
}

void Browser::showNextQuestion()
{
    // Questions from tabs that were closed in the meantime are dropped.
    while (!m_questions.isEmpty() && !m_questions.first().page)
        m_questions.removeFirst();
    if (m_questions.isEmpty() || m_questionShown)
        return;

    m_questionShown = true;
    m_messageSticky = false;
    m_messageTimer.stop();
    m_panel->setMessage(m_questions.first().text, Theme::ink);
    m_message->show();
    m_tabList->hide();
    m_line->setPlaceholderText(QString());
    m_panel->setKeyHint(tr("y — разрешить · n или Esc — запретить"));
    openCommandLine();
}

void Browser::answerQuestion(bool granted)
{
    m_questionShown = false;
    const PermissionQuestion question = m_questions.takeFirst();
    if (question.page)
        question.answer(granted);
    m_message->hide();
    m_line->setPlaceholderText(defaultPlaceholder());
    m_panel->setKeyHint(QString());
    // The question may have covered a certificate message: bring it back.
    WebView *tab = currentTab();
    if (tab && m_certificateErrors.count(tab))
        showCertificateMessage(tab);
}

void Browser::handleCertificateError(WebView *view, const QWebEngineCertificateError &error)
{
    QWebEngineCertificateError handle = error; // shared handle, needed to defer
    const QString host = error.url().host();

    if (!handle.isOverridable()) {
        // HSTS and similar: the site itself forbids opening it insecurely.
        handle.rejectCertificate();
        showMessage(tr("Сертификат %1 недействителен, а сайт запрещает открывать себя "
                       "без защиты").arg(host),
                    MessageKind::Error);
        return;
    }

    rejectPendingCertificate(view);
    handle.defer();
    m_certificateErrors.insert_or_assign(view, handle);
    if (view == currentTab())
        showCertificateMessage(view);
}

void Browser::rejectPendingCertificate(WebView *view)
{
    if (!m_certificateErrors.count(view))
        return;
    const auto it = m_certificateErrors.find(view);
    QWebEngineCertificateError error = it->second;
    m_certificateErrors.erase(it);
    error.rejectCertificate();
    if (view == currentTab())
        hideStickyMessage();
}

void Browser::showCertificateMessage(WebView *view)
{
    const QWebEngineCertificateError &error = m_certificateErrors.at(view);
    QString reason;
    switch (error.type()) {
    case QWebEngineCertificateError::CertificateCommonNameInvalid:
        reason = tr("выдан для другого сайта");
        break;
    case QWebEngineCertificateError::CertificateDateInvalid:
        reason = tr("просрочен или ещё не действует");
        break;
    case QWebEngineCertificateError::CertificateAuthorityInvalid:
        reason = tr("выдан неизвестным центром, возможно, самим сайтом");
        break;
    case QWebEngineCertificateError::CertificateRevoked:
        reason = tr("отозван");
        break;
    default:
        reason = error.description();
        break;
    }
    showStickyMessage(tr("Сертификат %1 недействителен: %2. :proceed — открыть всё равно")
                          .arg(error.url().host(), reason));
}

void Browser::toggleDevTools()
{
    WebView *tab = currentTab();
    if (m_devTools->isVisible()) {
        if (tab)
            tab->page()->setDevToolsPage(nullptr);
        m_devTools->hide();
        return;
    }
    if (!tab)
        return;
    tab->page()->setDevToolsPage(m_devTools->page());
    m_devTools->show();
    const int total = m_splitter->width();
    m_splitter->setSizes({total * 3 / 5, total * 2 / 5});
}
