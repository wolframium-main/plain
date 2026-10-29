#pragma once

#include <QSet>
#include <QString>
#include <QTimer>
#include <QWebEngineUrlRequestInterceptor>

class QNetworkAccessManager;

// Built-in ad blocker without settings: one filter list (EasyList),
// updated automatically. Blocks requests by domain only.
class AdBlocker : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT

public:
    explicit AdBlocker(QObject *parent = nullptr);

    // Loads the cached list and schedules updates.
    void start();

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

private:
    void load(const QByteArray &data);
    void updateIfStale();
    QString cachePath() const;

    QSet<QString> m_block;           // ||domain^
    QSet<QString> m_blockThirdParty; // ||domain^$third-party
    QNetworkAccessManager *m_network;
    QTimer m_updateTimer;
    bool m_updating = false;
};
