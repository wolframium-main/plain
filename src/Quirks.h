#pragma once

#include <QByteArray>
#include <QString>
#include <QWebEngineUrlRequestInterceptor>

class AdBlocker;
class QWebEngineProfile;

// Site-specific exceptions. There is exactly one, and every new one has to
// be justified in DESIGN.md first.
//
// Google refuses to sign in from browsers built on QtWebEngine ("This
// browser or app may not be secure"). On its sign-in host plain presents
// itself as Firefox: in the HTTP header and in navigator.userAgent.
namespace Quirks {

// The sign-in host. A variable only so tests can point it at a local server.
extern QString googleSignInHost;

QByteArray firefoxUserAgent();

// Adds the navigator.userAgent override to the profile. Call once, after
// googleSignInHost is final.
void install(QWebEngineProfile *profile);

// The profile's request interceptor: rewrites the User-Agent header for the
// sign-in host, then hands the request to the ad blocker, if there is one.
class Interceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT

public:
    explicit Interceptor(AdBlocker *adBlocker, QObject *parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

private:
    AdBlocker *m_adBlocker;
};

} // namespace Quirks
