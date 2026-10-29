// Drives plain through its only interface, the command line, and checks
// the behaviour agreed in DESIGN.md.

#include "../src/Browser.h"
#include "../src/Quirks.h"
#include "../src/WebView.h"

#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTest>
#include <QTextDocumentFragment>
#include <QWebEngineProfile>

class TestPlain : public QObject
{
    Q_OBJECT

private:
    QWebEngineProfile *profile = nullptr;
    Browser *browser = nullptr;
    QTemporaryDir dir;

    QLineEdit *line() const { return browser->findChild<QLineEdit *>(); }
    QStackedWidget *stack() const { return browser->findChild<QStackedWidget *>(); }
    WebView *current() const { return qobject_cast<WebView *>(stack()->currentWidget()); }
    int tabCount() const { return stack()->count(); }
    QString message() const
    {
        const auto *label = browser->findChild<QLabel *>(QStringLiteral("message"));
        return label->isVisible() ? QTextDocumentFragment::fromHtml(label->text()).toPlainText()
                                  : QString();
    }
    QWidget *tabList() const
    {
        return browser->findChild<QWidget *>(QStringLiteral("tabList"));
    }

    void type(const QString &text)
    {
        if (!line()->isVisible())
            browser->openCommandLine();
        line()->setText(text);
        QTest::keyClick(line(), Qt::Key_Return);
        QCoreApplication::processEvents();
    }

    QString writePage(const QString &name, const QString &html)
    {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            qFatal("cannot write %s", qPrintable(file.fileName()));
        file.write(html.toUtf8());
        return QUrl::fromLocalFile(file.fileName()).toString();
    }

    void closeAllButOne()
    {
        while (tabCount() > 1)
            type(QStringLiteral(":close"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The Google sign-in quirk is tested against a local server on 127.0.0.2.
        Quirks::googleSignInHost = QStringLiteral("127.0.0.2");
        profile = new QWebEngineProfile(this); // off-the-record for tests
        browser = new Browser(profile, nullptr);
        browser->resize(1000, 700);
        browser->show();
        browser->openCommandLine();
        QVERIFY(QTest::qWaitForWindowExposed(browser));
    }

    void cleanupTestCase()
    {
        delete browser;
    }

    void startsWithOneEmptyTabAndOpenLine()
    {
        QCOMPARE(tabCount(), 1);
        QVERIFY(line()->isVisible());
    }

    void presentsItselfAsPlainChrome()
    {
        type(QStringLiteral("data:text/html,<script>document.title=navigator.userAgent</script>"));
        QTRY_VERIFY(current()->title().contains(QStringLiteral("Chrome/")));
        QVERIFY(!current()->title().contains(QStringLiteral("QtWebEngine")));
        QVERIFY(!profile->httpUserAgent().contains(QStringLiteral("QtWebEngine")));
        browser->openCommandLine();
    }

    void ctrlSemicolonOpensLine()
    {
        QTest::keyClick(line(), Qt::Key_Escape);
        QVERIFY(!line()->isVisible());
        QTest::keyClick(browser, Qt::Key_Semicolon, Qt::ControlModifier);
        QVERIFY(line()->isVisible());
    }

    void panelIsReallyOnScreen()
    {
        // Shown is not enough: the panel must have a real size, sit on the
        // bottom edge, and the field must be 40px tall inside it.
        browser->openCommandLine();
        QTest::qWait(300); // the 120ms appearance
        QWidget *panel = browser->findChild<QWidget *>(QStringLiteral("panel"));
        QVERIFY(panel && panel->isVisible());
        QCOMPARE(panel->width(), browser->centralWidget()->width());
        QCOMPARE(panel->y() + panel->height(), browser->centralWidget()->height());
        QCOMPARE(line()->height(), 40);
        QCOMPARE(panel->height(), 16 + 40 + 16); // space-2, field, space-2
        QVERIFY(line()->hasFocus());
    }

    void typedAddressOpensInCurrentTabAndHidesLine()
    {
        const QString url = writePage(QStringLiteral("a.html"),
                                      QStringLiteral("<title>Alpha</title><p>word word</p>"));
        type(url);
        QVERIFY(!line()->isVisible());
        QTRY_COMPARE(current()->title(), QStringLiteral("Alpha"));
        QCOMPARE(tabCount(), 1);
    }

    void plainTextBecomesGoogleSearch()
    {
        // Don't actually load it: check the URL the line would produce.
        type(QStringLiteral(":new"));
        QVERIFY(line()->isVisible()); // a new empty tab opens the line
        QCOMPARE(tabCount(), 2);
        type(QStringLiteral("qt webengine"));
        QTRY_VERIFY(current()->url().toString().startsWith(
            QStringLiteral("https://www.google.com/search?q=qt")));
        current()->stop();
        type(QStringLiteral(":close"));
        QCOMPARE(tabCount(), 1);
    }

    void addressWithSchemeAndSpacesIsNotSearched()
    {
        type(QStringLiteral("data:text/html,<title>Two words</title>"));
        QTRY_COMPARE(current()->title(), QStringLiteral("Two words"));
        type(writePage(QStringLiteral("a.html"),
                       QStringLiteral("<title>Alpha</title><p>word word</p>")));
        QTRY_COMPARE(current()->title(), QStringLiteral("Alpha"));
        QTRY_VERIFY(!current()->isLoading()); // the body, not just the title
    }

    void findAndZoomAndUrl()
    {
        type(QStringLiteral(":find word"));
        QTRY_COMPARE(message(), QStringLiteral("Найдено: 1 из 2"));
        type(QStringLiteral(":zoom 150"));
        QCOMPARE(current()->zoomFactor(), 1.5);
        type(QStringLiteral(":zoom 9000"));
        QCOMPARE(current()->zoomFactor(), 1.5);
        type(QStringLiteral(":zoom 100"));
        type(QStringLiteral(":url"));
        QVERIFY(message().startsWith(QStringLiteral("Скопировано: file://")));
    }

    void unknownCommandKeepsLineForCorrection()
    {
        type(QStringLiteral(":clsoe"));
        QVERIFY(line()->isVisible());
        QCOMPARE(line()->text(), QStringLiteral(":clsoe"));
        QTest::keyClick(line(), Qt::Key_Escape);
        QVERIFY(!line()->isVisible());
    }

    void limitOfFiveTabsAsksToCloseOne()
    {
        for (int i = 0; i < 4; ++i)
            type(QStringLiteral(":new"));
        QCOMPARE(tabCount(), 5);
        QTest::keyClick(line(), Qt::Key_Escape);

        type(QStringLiteral(":new ") + writePage(QStringLiteral("b.html"),
                                                 QStringLiteral("<title>Beta</title>")));
        QCOMPARE(tabCount(), 5);                          // nothing opened
        QCOMPARE(line()->text(), QStringLiteral(":close "));
        QVERIFY(line()->isVisible());
        QVERIFY(tabList() && tabList()->isVisible());

        type(QStringLiteral(":close 2"));
        QCOMPARE(tabCount(), 5);                          // one closed, Beta opened
        QTRY_COMPARE(current()->title(), QStringLiteral("Beta"));
        QVERIFY(!line()->isVisible());
    }

    void escapeAbandonsPendingTab()
    {
        type(QStringLiteral(":new"));
        QCOMPARE(line()->text(), QStringLiteral(":close "));
        QTest::keyClick(line(), Qt::Key_Escape);
        type(QStringLiteral(":close 1"));
        QCOMPARE(tabCount(), 4);                          // pending was dropped
    }

    void switchingTabsByNumber()
    {
        type(QStringLiteral(":tab 1"));
        QCOMPARE(stack()->currentIndex(), 0);
        type(QStringLiteral(":tab 9"));
        QCOMPARE(stack()->currentIndex(), 0);
        QCOMPARE(message(), QStringLiteral("Нет вкладки «9»"));
    }

    void panelGeometryFollowsTheGrid()
    {
        // A one-line error: space-2, one 24px line, space-2.
        type(QStringLiteral(":zoom 9000"));
        QTest::qWait(300);
        QWidget *panel = browser->findChild<QWidget *>(QStringLiteral("panel"));
        QLabel *messageLabel = browser->findChild<QLabel *>(QStringLiteral("message"));
        QCOMPARE(messageLabel->height(), 24);
        QCOMPARE(panel->height(), 16 + 24 + 16);
        QCOMPARE(panel->y() + panel->height(), browser->centralWidget()->height());
        QTest::keyClick(line(), Qt::Key_Escape);

        // The tab list: each row is meta (16) + title (24) with space-2 on
        // both sides of every separator; then space-2, the field, space-2.
        type(QStringLiteral(":tabs"));
        QTest::qWait(300);
        const int n = tabCount();
        const int list = n * (16 + 24) + (n - 1) * 2 * 16;
        QCOMPARE(tabList()->height(), list);
        const int message = messageLabel->isVisible() ? 24 + 16 : 0; // may still be up
        QCOMPARE(panel->height(), 16 + list + 16 + message + 40 + 16);
        QCOMPARE(panel->y() + panel->height(), browser->centralWidget()->height());
        QTest::keyClick(line(), Qt::Key_Escape);
    }

    void closingLastTabLeavesAnEmptyOne()
    {
        closeAllButOne();
        type(QStringLiteral(":close"));
        QCOMPARE(tabCount(), 1);
        QVERIFY(line()->isVisible());
        QVERIFY(current()->url().isEmpty() || current()->url().toString() == "about:blank");
        QTest::keyClick(line(), Qt::Key_Escape);
    }

    void siteRequestedNewTabOpensNewTab()
    {
        const QString target = writePage(QStringLiteral("c.html"),
                                         QStringLiteral("<title>Gamma</title>"));
        const QString page = writePage(
            QStringLiteral("link.html"),
            QStringLiteral("<title>Links</title><style>a{display:block;width:100vw;"
                           "height:100vh}</style><a target=_blank href='%1'>x</a>")
                .arg(target));
        type(page);
        QTRY_COMPARE(current()->title(), QStringLiteral("Links"));
        QTest::qWait(300);

        QWidget *target_widget = current()->focusProxy();
        QVERIFY(target_widget);
        QTest::mouseClick(target_widget, Qt::LeftButton, {}, QPoint(200, 200));
        QTRY_COMPARE(tabCount(), 2);
        QTRY_COMPARE(current()->title(), QStringLiteral("Gamma"));
    }

    void cameraIsAskedAbout_data()
    {
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("result");
        QTest::newRow("y") << "y" << "granted:live";
        QTest::newRow("russian layout") << "н" << "granted:live";
        QTest::newRow("n") << "n" << "denied";
        QTest::newRow("anything else") << "maybe" << "denied";
    }

    void cameraIsAskedAbout()
    {
        QFETCH(QString, answer);
        QFETCH(QString, result);
        const QString page = writePage(
            QStringLiteral("cam.html"),
            QStringLiteral("<title>wait</title><script>"
                           "navigator.mediaDevices.getUserMedia({video:true})"
                           ".then(s=>{window.stream=s;setTimeout(()=>document.title="
                           "'granted:'+s.getTracks()[0].readyState,500)},"
                           "()=>document.title='denied')</script>"));
        type(page);
        QTRY_VERIFY(message().contains(QStringLiteral("просит камеру")));
        QVERIFY(line()->isVisible());
        type(answer);
        QTRY_COMPARE(current()->title(), result);
        QVERIFY(!line()->isVisible());
    }

    void escapeDeniesPermission()
    {
        const QString page = writePage(
            QStringLiteral("mic.html"),
            QStringLiteral("<title>wait</title><script>"
                           "navigator.mediaDevices.getUserMedia({audio:true})"
                           ".then(()=>document.title='granted',"
                           "()=>document.title='denied')</script>"));
        type(page);
        QTRY_VERIFY(message().contains(QStringLiteral("просит микрофон")));
        QTest::keyClick(line(), Qt::Key_Escape);
        QTRY_COMPARE(current()->title(), QStringLiteral("denied"));
    }

    void notificationsAndLocationDeniedWithoutAsking()
    {
        const QString page = writePage(
            QStringLiteral("notify.html"),
            QStringLiteral("<title>wait</title><script>"
                           "Notification.requestPermission().then(p=>{"
                           "navigator.geolocation.getCurrentPosition("
                           "()=>document.title=p+',located',"
                           "()=>document.title=p+',nolocation')})</script>"));
        type(page);
        QTRY_COMPARE(current()->title(), QStringLiteral("denied,nolocation"));
        QVERIFY(!line()->isVisible());
    }

    // --- certificates: a local HTTPS server with a self-signed certificate

    void startHttpsServer()
    {
        QProcess openssl;
        openssl.setWorkingDirectory(dir.path());
        openssl.start(QStringLiteral("openssl"),
                      {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                       "-subj", "/CN=plain-test", "-keyout", "key.pem", "-out", "cert.pem"});
        QVERIFY(openssl.waitForFinished(30000));
        QCOMPARE(openssl.exitCode(), 0);

        writePage(QStringLiteral("secure.html"), QStringLiteral("<title>Secure</title>"));
        httpsServer.setWorkingDirectory(dir.path());
        httpsServer.start(QStringLiteral("python3"),
                          {"-c",
                           "import http.server,ssl\n"
                           "s=http.server.HTTPServer(('127.0.0.1',48443),"
                           "http.server.SimpleHTTPRequestHandler)\n"
                           "c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)\n"
                           "c.load_cert_chain('cert.pem','key.pem')\n"
                           "s.socket=c.wrap_socket(s.socket,server_side=True)\n"
                           "print('ready',flush=True)\n"
                           "s.serve_forever()"});
        QVERIFY(httpsServer.waitForReadyRead(10000));
    }

    void invalidCertificateWaitsForProceed()
    {
        type(QStringLiteral("https://127.0.0.1:48443/secure.html"));
        QTRY_VERIFY(message().contains(QStringLiteral(":proceed")));
        QVERIFY(message().contains(QStringLiteral("неизвестным центром")));
        QTest::qWait(1500);
        QVERIFY(message().contains(QStringLiteral(":proceed"))); // stays put
        QVERIFY(current()->title() != QStringLiteral("Secure"));

        type(QStringLiteral(":proceed"));
        QTRY_COMPARE(current()->title(), QStringLiteral("Secure"));
        QVERIFY(message().isEmpty());

        // Until restart, the same site opens without asking again.
        type(QStringLiteral(":new https://127.0.0.1:48443/secure.html?again"));
        QTRY_COMPARE(current()->url().query(), QStringLiteral("again"));
        QTRY_COMPARE(current()->title(), QStringLiteral("Secure"));
        QVERIFY(message().isEmpty());
        type(QStringLiteral(":close"));
    }

    void goingElsewhereRejectsCertificate()
    {
        // Another host name, so the earlier :proceed does not apply.
        type(QStringLiteral("https://localhost:48443/secure.html"));
        QTRY_VERIFY(message().contains(QStringLiteral(":proceed")));

        type(writePage(QStringLiteral("d.html"), QStringLiteral("<title>Delta</title>")));
        QTRY_COMPARE(current()->title(), QStringLiteral("Delta"));
        QVERIFY(message().isEmpty());

        type(QStringLiteral(":proceed"));
        QVERIFY(message().startsWith(QStringLiteral("В этой вкладке нет")));
    }

    void certificateMessageFollowsItsTab()
    {
        type(QStringLiteral(":new https://localhost:48443/secure.html"));
        QTRY_VERIFY(message().contains(QStringLiteral(":proceed")));
        const QString errorTab = QString::number(tabCount());
        type(QStringLiteral(":tab 1"));
        QVERIFY(message().isEmpty());
        type(QStringLiteral(":tab ") + errorTab);
        QVERIFY(message().contains(QStringLiteral(":proceed")));
        type(QStringLiteral(":close"));
        QVERIFY(message().isEmpty());
    }

private:
    QProcess httpsServer;

private Q_SLOTS:
    void stopHttpsServer()
    {
        httpsServer.kill();
        httpsServer.waitForFinished();
    }

    // --- the Google sign-in quirk, against a local server

    void startEchoServer()
    {
        // Answers every request with a page that reports the User-Agent the
        // server received and the one the page's scripts see.
        echoServer.start(QStringLiteral("python3"),
                         {"-c",
                          "import http.server,json\n"
                          "class H(http.server.BaseHTTPRequestHandler):\n"
                          "  def do_GET(s):\n"
                          "    h=json.dumps(s.headers.get('User-Agent',''))\n"
                          "    b=('<script>document.title=JSON.stringify({path:'+json.dumps(s.path)+',header:'+h+',"
                          "page:navigator.userAgent,vendor:navigator.vendor})</script>').encode()\n"
                          "    s.send_response(200);s.send_header('Content-Type','text/html')\n"
                          "    s.end_headers();s.wfile.write(b)\n"
                          "  def log_message(s,*a): pass\n"
                          "s=http.server.HTTPServer(('',48080),H)\n"
                          "print('ready',flush=True)\n"
                          "s.serve_forever()"});
        QVERIFY(echoServer.waitForReadyRead(10000));
    }

    void googleSignInSeesFirefox()
    {
        type(QStringLiteral("http://127.0.0.2:48080/signin"));
        QTRY_VERIFY(current()->title().contains(QStringLiteral("\"/signin\"")));
        const QJsonObject seen = QJsonDocument::fromJson(current()->title().toUtf8()).object();
        QCOMPARE(seen.value("header").toString().toLatin1(), Quirks::firefoxUserAgent());
        QCOMPARE(seen.value("page").toString().toLatin1(), Quirks::firefoxUserAgent());
        QCOMPARE(seen.value("vendor").toString(), QString());
    }

    void otherSitesSeeChrome()
    {
        type(QStringLiteral("http://127.0.0.1:48080/other"));
        QTRY_VERIFY(current()->title().contains(QStringLiteral("\"/other\"")));
        const QJsonObject seen = QJsonDocument::fromJson(current()->title().toUtf8()).object();
        for (const char *key : {"header", "page"}) {
            const QString ua = seen.value(key).toString();
            QVERIFY2(ua.contains(QStringLiteral("Chrome/")), qPrintable(ua));
            QVERIFY2(!ua.contains(QStringLiteral("Firefox")), qPrintable(ua));
            QVERIFY2(!ua.contains(QStringLiteral("QtWebEngine")), qPrintable(ua));
        }
        QVERIFY(!seen.value("vendor").toString().isEmpty()); // "Google Inc."
    }

    void stopEchoServer()
    {
        echoServer.kill();
        echoServer.waitForFinished();
    }

private:
    QProcess echoServer;
};

QTEST_MAIN(TestPlain)
#include "test_plain.moc"
