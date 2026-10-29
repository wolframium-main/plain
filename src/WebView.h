#pragma once

#include <QWebEngineView>

class QWebEngineProfile;

// A single tab. Keeps track of its own loading state and shows
// the minimal context menu: copy, copy link, save image.
class WebView : public QWebEngineView
{
    Q_OBJECT

public:
    WebView(QWebEngineProfile *profile, QWidget *parent = nullptr);

    bool isLoading() const { return m_loading; }
    int progress() const { return m_progress; }

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    bool m_loading = false;
    int m_progress = 0;
};
