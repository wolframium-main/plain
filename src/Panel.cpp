#include "Panel.h"
#include "Theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int MaxRows = 5;
}

Panel::Panel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panel"));

    // Tab list. The list rule: rows separated by "line", space-2 above and
    // below each separator, the row's metadata ("meta", ink-3) above its
    // title ("body"). The current tab's title is ink, the others ink-2.
    m_tabList = new QWidget;
    m_tabList->setObjectName(QStringLiteral("tabList"));
    auto *listLayout = new QVBoxLayout(m_tabList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    for (int i = 0; i < MaxRows; ++i) {
        auto *row = new QWidget;
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setSpacing(0);
        auto *meta = new QLabel;
        meta->setFont(Theme::meta());
        meta->setFixedHeight(Theme::metaLine);
        meta->setStyleSheet(QStringLiteral("color: %1").arg(Theme::ink3.name()));
        auto *title = new QLabel;
        title->setFont(Theme::body());
        title->setFixedHeight(Theme::bodyLine);
        rowLayout->addWidget(meta);
        rowLayout->addWidget(title);
        listLayout->addWidget(row);
        m_rows << row;
        m_rowMeta << meta;
        m_rowTitle << title;
    }
    m_tabList->hide();

    // Message: body, never wider than a line of text.
    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("message"));
    m_message->setFont(Theme::body());
    m_message->setTextFormat(Qt::RichText);
    m_message->setWordWrap(true);
    m_message->setMaximumWidth(Theme::column);
    m_message->hide();

    // Command field: the field rule. Background one step away from the
    // panel's surface (bg), a line-strong border, the one radius, 40px,
    // space-1 / space-2 inside, body ink, a real example as placeholder.
    m_line = new QLineEdit;
    m_line->setFont(Theme::body());
    m_line->setFixedHeight(Theme::fieldHeight);
    m_line->setMaximumWidth(Theme::column);
    m_line->setStyleSheet(
        QStringLiteral("QLineEdit { background: %1; color: %2; border: %3px solid %4;"
                       " border-radius: %5px; padding: 0 %6px;"
                       " selection-background-color: %7; selection-color: %8; }")
            .arg(Theme::bg.name(), Theme::ink.name())
            .arg(Theme::stroke)
            .arg(Theme::lineStrong.name())
            .arg(Theme::radius)
            .arg(Theme::space2 - Theme::stroke - 1) // QLineEdit adds 1px of its own
            .arg(Theme::violet.name(), Theme::onViolet.name()));
    m_line->hide();

    // Key hint: "meta", ink-3, next to the field.
    m_keyHint = new QLabel;
    m_keyHint->setFont(Theme::meta());
    m_keyHint->setStyleSheet(QStringLiteral("color: %1").arg(Theme::ink3.name()));
    m_keyHint->hide();

    m_fieldRow = new QWidget;
    auto *fieldLayout = new QHBoxLayout(m_fieldRow);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(Theme::space2);
    fieldLayout->addWidget(m_line, 1);
    fieldLayout->addWidget(m_keyHint);
    fieldLayout->addStretch(0);

    auto *layout = new QVBoxLayout(this);
    // The ring around the field sits in the padding: 2px offset + 2px ring.
    layout->setContentsMargins(Theme::space3, Theme::space2, Theme::space3, Theme::space2);
    layout->setSpacing(Theme::space2);
    layout->addWidget(m_tabList);
    layout->addWidget(m_message);
    layout->addWidget(m_fieldRow);
    m_fieldRow->hide(); // takes no room (nor spacing) while the field is hidden

    for (QWidget *w : {m_tabList, static_cast<QWidget *>(m_message),
                       static_cast<QWidget *>(m_line)})
        w->installEventFilter(this);
    parent->installEventFilter(this);
    installEventFilter(this);

    hide();
}

int Panel::textWidth() const
{
    // The panel always spans its parent; ask the parent, which already has
    // its size while the panel is still hidden.
    return qMax(1, qMin(Theme::column, parentWidget()->width() - 2 * Theme::space3));
}

int Panel::top() const
{
    return isHidden() ? parentWidget()->height() : y();
}

void Panel::setTabs(const QList<Tab> &tabs)
{
    m_tabs = tabs;
    const QFontMetrics titleMetrics(Theme::body());
    const QFontMetrics metaMetrics(Theme::meta());
    const int width = qMax(120, textWidth());

    for (int i = 0; i < m_rows.size(); ++i) {
        QWidget *row = m_rows.at(i);
        if (i >= tabs.size()) {
            row->hide();
            continue;
        }
        const Tab &tab = tabs.at(i);
        m_rowMeta.at(i)->setText(metaMetrics.elidedText(tab.meta, Qt::ElideMiddle, width));
        m_rowTitle.at(i)->setText(titleMetrics.elidedText(tab.title, Qt::ElideRight, width));
        m_rowTitle.at(i)->setStyleSheet(
            QStringLiteral("color: %1").arg((tab.current ? Theme::ink : Theme::ink2).name()));
        // space-2 on both sides of every separator; the panel's own padding
        // takes care of the outer edges.
        const bool first = i == 0;
        const bool last = i == tabs.size() - 1;
        row->layout()->setContentsMargins(0, first ? 0 : Theme::space2, 0,
                                          last ? 0 : Theme::space2);
        row->show();
    }
    update();
}

void Panel::setMessage(const QString &text, const QColor &color)
{
    m_message->setText(Theme::paragraph(text, color));
    fitMessage();
}

void Panel::fitMessage()
{
    // Exactly as many 24px lines as the text needs at the column's width.
    m_message->setFixedHeight(qMax(Theme::bodyLine, m_message->heightForWidth(textWidth())));
}

void Panel::setKeyHint(const QString &text)
{
    m_keyHint->setText(text);
    m_keyHint->setVisible(!text.isEmpty());
}

void Panel::place(bool animate)
{
    QWidget *parent = parentWidget();
    const int width = parent->width();
    if (this->width() != width) {
        resize(width, this->height());
        fitMessage();
    }
    layout()->activate();
    const int height = layout()->totalSizeHint().height();
    const QRect target(0, parent->height() - height, width, height);

    m_relayout = false;
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
        setGraphicsEffect(nullptr);
    }

    if (!animate || Theme::reduceMotion()) {
        setGeometry(target);
        raise();
        Q_EMIT topChanged();
        return;
    }

    // Appearing: opacity 0 → 1 and up by 8px, 120ms ease-out.
    setGeometry(target.translated(0, Theme::motionShift));
    raise();
    auto *opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(0);
    setGraphicsEffect(opacity);

    auto *group = new QParallelAnimationGroup(this);
    auto *fade = new QPropertyAnimation(opacity, "opacity");
    fade->setEndValue(1.0);
    auto *rise = new QPropertyAnimation(this, "pos");
    rise->setEndValue(target.topLeft());
    for (QPropertyAnimation *a : {fade, rise}) {
        a->setDuration(Theme::motionMs);
        a->setEasingCurve(QEasingCurve::OutCubic); // = cubic-bezier(0.215, 0.61, 0.355, 1)
        group->addAnimation(a);
    }
    connect(group, &QAbstractAnimation::finished, this, [this] {
        setGraphicsEffect(nullptr);
        // Content that changed while the panel was appearing.
        if (m_relayout)
            QTimer::singleShot(0, this, [this] { place(false); });
    });
    m_animation = group;
    group->start(QAbstractAnimation::DeleteWhenStopped);
    Q_EMIT topChanged();
}

bool Panel::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Something in the panel appeared or disappeared: the panel follows.
    if ((watched == m_tabList || watched == m_message || watched == m_line)
        && (type == QEvent::ShowToParent || type == QEvent::HideToParent)) {
        if (watched == m_line)
            m_fieldRow->setVisible(type == QEvent::ShowToParent);
        const bool wanted =
            !m_tabList->isHidden() || !m_message->isHidden() || !m_line->isHidden();
        if (wanted && isHidden()) {
            show();
            place(true);
        } else if (!wanted && !isHidden()) {
            hide();
            Q_EMIT topChanged();
        }
    }

    // The ring follows the field's focus.
    if (watched == m_line && (type == QEvent::FocusIn || type == QEvent::FocusOut))
        update();

    // Resized window or changed content: stay glued to the bottom edge.
    const bool resized = watched == parentWidget() && type == QEvent::Resize;
    if (!isHidden() && (resized || (watched == this && type == QEvent::LayoutRequest))) {
        if (resized) {
            resize(parentWidget()->width(), height());
            fitMessage();
            if (!m_tabs.isEmpty())
                setTabs(m_tabs); // re-elide for the new width
        }
        if (m_animation)
            m_relayout = true; // after the appearance
        else
            place(false);
    }
    return QWidget::eventFilter(watched, event);
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::surface);
    p.fillRect(0, 0, width(), Theme::stroke, Theme::line); // the page side

    // Separators of the tab list.
    if (!m_tabList->isHidden()) {
        bool first = true;
        for (QWidget *row : std::as_const(m_rows)) {
            if (row->isHidden())
                continue;
            if (!first) {
                const QPoint at = row->mapTo(this, QPoint(0, 0));
                p.fillRect(at.x(), at.y(), textWidth(), Theme::stroke, Theme::line);
            }
            first = false;
        }
    }

    // Keyboard focus ring: focus, 2px, 2px away from the field.
    if (m_line->isVisible() && m_line->hasFocus()) {
        const QRect field(m_line->mapTo(this, QPoint(0, 0)), m_line->size());
        const int out = Theme::focusOffset + Theme::focusWidth / 2;
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::focus, Theme::focusWidth));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(field.adjusted(-out, -out, out - 1, out - 1)), Theme::radius,
                          Theme::radius);
    }
}
