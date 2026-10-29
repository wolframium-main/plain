#pragma once

#include <QList>
#include <QMainWindow>
#include <QWebEngineCertificateError>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <unordered_map>

class QLabel;
class Panel;
class QLineEdit;
class QProgressBar;
class QWebEnginePage;
class QSplitter;
class QStackedWidget;
class QWebEngineDownloadRequest;
class QWebEngineFullScreenRequest;
class QWebEngineNewWindowRequest;
class QWebEngineProfile;
class QWebEngineView;
class WebView;
class AdBlocker;

// The only window of plain: up to five hidden tabs, one command line
// at the bottom, a thin progress bar. Nothing else.
class Browser : public QMainWindow
{
    Q_OBJECT

public:
    static constexpr int MaxTabs = 5;

    // adBlocker may be null (tests).
    Browser(QWebEngineProfile *profile, AdBlocker *adBlocker, QWidget *parent = nullptr);

    // A URL from outside (command line argument, another app).
    void openExternalUrl(const QUrl &url);
    void openCommandLine(const QString &prefill = QString());

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Tabs
    WebView *createTab(bool makeCurrent);
    WebView *currentTab() const;
    void setCurrentTab(WebView *view);
    bool closeTab(int index);
    void requestNewTab(const QUrl &url, bool background);
    void handleNewWindowRequest(QWebEngineNewWindowRequest &request);
    void showLimitReached();
    bool openPendingTab();

    // Command line
    void hideCommandLine();
    void execute(const QString &input);
    bool runCommand(const QString &name, const QString &arg);

    // Feedback
    void refreshTabList();
    void updateWindowTitle();
    void updateProgress();
    void placeProgress();
    enum class MessageKind { Info, Error };
    void showMessage(const QString &text, MessageKind kind = MessageKind::Info);
    void showStickyMessage(const QString &text);
    void hideStickyMessage();

    // Engine requests
    void handleDownload(QWebEngineDownloadRequest *download);
    void handleFullScreen(QWebEngineFullScreenRequest request);
    void toggleDevTools();

    // An invalid certificate holds the page until the user types :proceed
    // or goes elsewhere.
    void handleCertificateError(WebView *view, const QWebEngineCertificateError &error);
    void rejectPendingCertificate(WebView *view);
    void showCertificateMessage(WebView *view);

    // Site permissions: camera, microphone and screen are asked about in the
    // bottom line, one question at a time; everything else is denied.
    struct PermissionQuestion {
        QPointer<QWebEnginePage> page;
        QString text;
        std::function<void(bool)> answer;
    };
    void connectPermissions(QWebEnginePage *page);
    void askPermission(QWebEnginePage *page, const QUrl &origin, const QString &what,
                       std::function<void(bool)> answer);
    void showNextQuestion();
    void answerQuestion(bool granted);

    QWebEngineProfile *m_profile;
    QList<WebView *> m_tabs;

    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWebEngineView *m_devTools = nullptr;
    QProgressBar *m_progress = nullptr;
    Panel *m_panel = nullptr;
    QWidget *m_tabList = nullptr;
    QLabel *m_message = nullptr;
    QLineEdit *m_line = nullptr;
    QTimer m_messageTimer;

    // A tab that waits for a free slot when the limit is reached.
    bool m_hasPending = false;
    QUrl m_pendingUrl;

    std::unordered_map<WebView *, QWebEngineCertificateError> m_certificateErrors;
    bool m_messageSticky = false;

    QList<PermissionQuestion> m_questions;
    bool m_questionShown = false;

    bool m_wasMaximized = false;
};
