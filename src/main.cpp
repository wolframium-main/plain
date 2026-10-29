#include "AdBlocker.h"
#include "Browser.h"

#include <QApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include <unistd.h>

namespace {

// First argument that is not a Chromium/Qt flag.
QString urlArgument(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith(QLatin1Char('-')))
            return arg;
    }
    return QString();
}

QUrl toUrl(const QString &text)
{
    return QUrl::fromUserInput(text, QDir::currentPath());
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName(QStringLiteral("plain"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLAIN_VERSION));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("plain"));

    const QString urlArg = urlArgument(argc, argv);

    // One window only: a second launch hands its URL to the running
    // instance and exits.
    const QString serverName = QStringLiteral("plain-%1").arg(getuid());
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(500)) {
            socket.write(urlArg.toUtf8());
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return 0;
        }
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    // Declared before the profile so it outlives it.
    AdBlocker adBlocker;

    // Remembers cookies and sign-ins. Nothing else: no history.
    QWebEngineProfile profile(QStringLiteral("default"));
    profile.setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile.setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile.clearAllVisitedLinks();

    QWebEngineSettings *settings = profile.settings();
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);

    adBlocker.start();

    Browser browser(&profile, &adBlocker);
    browser.resize(1280, 800);
    browser.show();

    QObject::connect(&server, &QLocalServer::newConnection, &browser, [&server, &browser] {
        QLocalSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::disconnected, &browser, [client, &browser] {
            const QString text = QString::fromUtf8(client->readAll()).trimmed();
            client->deleteLater();
            if (text.isEmpty()) {
                browser.raise();
                browser.activateWindow();
            } else {
                browser.openExternalUrl(toUrl(text));
            }
        });
    });

    if (urlArg.isEmpty())
        browser.openCommandLine();
    else
        browser.openExternalUrl(toUrl(urlArg));

    const int result = app.exec();
    profile.clearAllVisitedLinks();
    return result;
}
