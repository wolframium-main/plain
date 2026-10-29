#include "Quirks.h"
#include "AdBlocker.h"

#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlRequestInfo>

namespace Quirks {

QString googleSignInHost = QStringLiteral("accounts.google.com");

QByteArray firefoxUserAgent()
{
    // A current Firefox for Linux. If Google starts calling it outdated,
    // raise the version.
    return QByteArrayLiteral(
        "Mozilla/5.0 (X11; Linux x86_64; rv:155.0) Gecko/20100101 Firefox/155.0");
}

void install(QWebEngineProfile *profile)
{
    const QString name = QStringLiteral("plain-quirk-google-signin");
    const auto existing = profile->scripts()->find(name);
    for (const QWebEngineScript &script : existing)
        profile->scripts()->remove(script);

    // What the page's own scripts see must match the header: Firefox has no
    // vendor string and no userAgentData.
    QWebEngineScript script;
    script.setName(name);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(
        QStringLiteral(
            "(() => {"
            "  if (location.hostname !== '%1') return;"
            "  const ua = '%2';"
            "  const fake = (key, value) => Object.defineProperty("
            "      Navigator.prototype, key, {get: () => value, configurable: true});"
            "  fake('userAgent', ua);"
            "  fake('appVersion', ua.slice('Mozilla/'.length));"
            "  fake('vendor', '');"
            "  fake('platform', 'Linux x86_64');"
            "  fake('userAgentData', undefined);"
            "})();")
            .arg(googleSignInHost, QString::fromLatin1(firefoxUserAgent())));
    profile->scripts()->insert(script);
}

Interceptor::Interceptor(AdBlocker *adBlocker, QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
    , m_adBlocker(adBlocker)
{
}

void Interceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    // A navigation goes by its own address: leaving the sign-in page must
    // not carry the disguise to the next site. Everything a page loads goes
    // by the page.
    const bool navigation =
        info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame;
    const QString host =
        navigation ? info.requestUrl().host() : info.firstPartyUrl().host();
    if (host == googleSignInHost)
        info.setHttpHeader(QByteArrayLiteral("User-Agent"), firefoxUserAgent());

    if (m_adBlocker)
        m_adBlocker->interceptRequest(info);
}

} // namespace Quirks
