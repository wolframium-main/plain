#include "AdBlocker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QWebEngineUrlRequestInfo>

namespace {

const QUrl ListUrl(QStringLiteral("https://easylist.to/easylist/easylist.txt"));
constexpr qint64 MaxListAgeSecs = 4 * 24 * 3600; // EasyList asks for ~4 days
constexpr int CheckIntervalMs = 6 * 3600 * 1000;

// Rough "site" of a host: its last two labels. Good enough to tell
// first-party from third-party requests without a public suffix list.
QString siteOf(const QString &host)
{
    if (!QHostAddress(host).isNull())
        return host;
    const int last = host.lastIndexOf(QLatin1Char('.'));
    if (last <= 0)
        return host;
    const int prev = host.lastIndexOf(QLatin1Char('.'), last - 1);
    return prev < 0 ? host : host.mid(prev + 1);
}

} // namespace

AdBlocker::AdBlocker(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_updateTimer.setInterval(CheckIntervalMs);
    connect(&m_updateTimer, &QTimer::timeout, this, &AdBlocker::updateIfStale);
}

void AdBlocker::start()
{
    QFile file(cachePath());
    if (file.open(QIODevice::ReadOnly))
        load(file.readAll());
    updateIfStale();
    m_updateTimer.start();
}

QString AdBlocker::cachePath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("easylist.txt"));
}

void AdBlocker::updateIfStale()
{
    const QFileInfo info(cachePath());
    if (m_updating
        || (info.exists()
            && info.lastModified().secsTo(QDateTime::currentDateTime()) < MaxListAgeSecs))
        return;

    m_updating = true;
    QNetworkReply *reply = m_network->get(QNetworkRequest(ListUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_updating = false;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("plain: filter list update failed: %s",
                     qPrintable(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (!data.startsWith("[Adblock")) {
            qWarning("plain: filter list update returned unexpected content");
            return;
        }
        QDir().mkpath(QFileInfo(cachePath()).absolutePath());
        QSaveFile file(cachePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.commit();
        }
        load(data);
    });
}

void AdBlocker::load(const QByteArray &data)
{
    static const QRegularExpression domainPattern(QStringLiteral("^[a-z0-9.-]+$"));

    QSet<QString> block, thirdParty, allow;
    const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();

        const bool exception = line.startsWith(QLatin1String("@@"));
        if (exception)
            line.remove(0, 2);
        if (!line.startsWith(QLatin1String("||")))
            continue;
        line.remove(0, 2);

        const int caret = line.indexOf(QLatin1Char('^'));
        if (caret <= 0)
            continue;
        const QString domain = line.left(caret).toLower();
        const QString rest = line.mid(caret + 1);
        if (!domainPattern.match(domain).hasMatch())
            continue;

        if (exception) {
            // Any exception for a whole domain unblocks it entirely:
            // under-blocking is better than breaking a site.
            if (rest.isEmpty() || rest.startsWith(QLatin1Char('$')))
                allow.insert(domain);
        } else if (rest.isEmpty()) {
            block.insert(domain);
        } else if (rest == QLatin1String("$third-party")) {
            thirdParty.insert(domain);
        }
        // Rules with other options or paths are skipped on purpose.
    }

    for (const QString &domain : std::as_const(allow)) {
        block.remove(domain);
        thirdParty.remove(domain);
    }

    m_block = std::move(block);
    m_blockThirdParty = std::move(thirdParty);
    qInfo("plain: ad blocker loaded %lld domain rules",
          static_cast<long long>(m_block.size() + m_blockThirdParty.size()));
}

void AdBlocker::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    // Never block what the user navigated to.
    if (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame)
        return;

    QString host = info.requestUrl().host().toLower();
    if (host.isEmpty())
        return;
    const bool thirdParty = siteOf(host) != siteOf(info.firstPartyUrl().host().toLower());

    // Check the host and every parent domain: ads.example.com, example.com.
    while (true) {
        if (m_block.contains(host) || (thirdParty && m_blockThirdParty.contains(host))) {
            info.block(true);
            return;
        }
        const int dot = host.indexOf(QLatin1Char('.'));
        if (dot < 0)
            return;
        host.remove(0, dot + 1);
    }
}
