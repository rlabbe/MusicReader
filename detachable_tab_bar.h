#pragma once

#include <QtWidgets>
#include <functional>

// Tab bar that lets the user drag a tab out of the window. Tabs reorder while
// the pointer is inside the bar. After the pointer leaves, a top-level label
// with the tab text follows it. On release, drag_finished receives the pressed
// tab index and the release position; the caller moves the page and decides
// which window receives it.
class DetachableTabBar : public QTabBar
{
public:
    explicit DetachableTabBar(QWidget* parent = nullptr);
    ~DetachableTabBar() override;

    std::function<void(int, const QPoint&)> drag_finished;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void start_drag(const QString& text);
    void move_drag(const QPoint& global_pos);
    void stop_drag();

    int pressed_index_ = -1;
    QPoint press_pos_;
    QLabel* drag_label_ = nullptr;
};

inline DetachableTabBar::DetachableTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setMovable(false);
    setExpanding(false);
}

inline DetachableTabBar::~DetachableTabBar()
{
    delete drag_label_;
}

inline void DetachableTabBar::mousePressEvent(QMouseEvent* event)
{
    QTabBar::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const int index = tabAt(event->position().toPoint());
    if (index < 0)
        return;

    pressed_index_ = index;
    press_pos_ = event->position().toPoint();
}

inline void DetachableTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (pressed_index_ < 0)
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    const QPoint pos = event->position().toPoint();
    if ((pos - press_pos_).manhattanLength() < QApplication::startDragDistance())
        return;

    if (rect().contains(pos))
    {
        if (drag_label_)
            stop_drag();

        const int target = tabAt(pos);
        if (target >= 0 && target != pressed_index_)
        {
            moveTab(pressed_index_, target);
            pressed_index_ = target;
        }
        return;
    }

    if (!drag_label_)
        start_drag(tabText(pressed_index_));

    move_drag(event->globalPosition().toPoint());
}

inline void DetachableTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    const int index = pressed_index_;
    pressed_index_ = -1;

    if (!drag_label_)
    {
        QTabBar::mouseReleaseEvent(event);
        return;
    }

    stop_drag();
    if (drag_finished)
        drag_finished(index, event->globalPosition().toPoint());
}

inline void DetachableTabBar::start_drag(const QString& text)
{
    drag_label_ = new QLabel(text, nullptr, Qt::ToolTip);
    drag_label_->setAttribute(Qt::WA_ShowWithoutActivating);
    drag_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drag_label_->setStyleSheet(
        QStringLiteral("QLabel { background: #454545; color: white; padding: 4px 10px; border: 1px solid #909090; }"));
    drag_label_->adjustSize();
    drag_label_->show();
}

inline void DetachableTabBar::move_drag(const QPoint& global_pos)
{
    if (!drag_label_)
        return;

    drag_label_->move(global_pos - QPoint(drag_label_->width() / 2, drag_label_->height() / 2));
}

inline void DetachableTabBar::stop_drag()
{
    if (!drag_label_)
        return;

    drag_label_->hide();
    drag_label_->deleteLater();
    drag_label_ = nullptr;
}
