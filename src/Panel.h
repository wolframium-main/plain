#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QParallelAnimationGroup;

// The bottom panel: tab list, message, command field. A strip along the
// bottom edge of the window, drawn over the page: surface, a "line" on the
// page side, space-3 / space-2 inside. It exists only while something in it
// is shown, and appears in 120ms (opacity and an 8px shift).
class Panel : public QWidget
{
    Q_OBJECT

public:
    struct Tab {
        QString meta;  // "2 · doc.qt.io"
        QString title; // "Qt 6 Docs"
        bool current = false;
    };

    explicit Panel(QWidget *parent);

    QWidget *tabList() const { return m_tabList; }
    QLabel *message() const { return m_message; }
    QLineEdit *line() const { return m_line; }

    void setTabs(const QList<Tab> &tabs);
    void setMessage(const QString &text, const QColor &color);
    void setKeyHint(const QString &text); // empty hides it

    // Where the panel's top edge is, in the parent's coordinates, or the
    // parent's height when the panel is hidden.
    int top() const;

Q_SIGNALS:
    void topChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int textWidth() const;
    void fitMessage();
    void place(bool animate);

    QWidget *m_tabList;
    QList<QWidget *> m_rows;
    QList<QLabel *> m_rowMeta;
    QList<QLabel *> m_rowTitle;
    QList<Tab> m_tabs;
    QLabel *m_message;
    QLineEdit *m_line;
    QWidget *m_fieldRow;
    QLabel *m_keyHint;
    QPointer<QParallelAnimationGroup> m_animation;
    bool m_relayout = false;
};
