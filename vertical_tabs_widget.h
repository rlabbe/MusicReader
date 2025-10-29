#pragma once

#include <QtWidgets>


class VerticalTabBar : public QTabBar {

public:
    explicit VerticalTabBar(QWidget* parent = nullptr)
        : QTabBar(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        setDocumentMode(true);
        setElideMode(Qt::ElideRight);
        setMovable(true);
        setTabsClosable(true);

        setStyleSheet(R"(
    QTabBar::tab {
        background: #454545;
        text-align: left;
        padding: 4px 8px;
        min-width: 150px;
        min-height: 24px;
        max-height: 28px;
        border-top-left-radius: 8px;
    }
    QTabBar::tab:selected {
        background: #404040;
        color: white;
        border-top: 2px solid #000000;
    }
    QTabBar::tab:!selected {
        color: white;
    }
)");
    }

protected:
    QSize tabSizeHint(int index) const override
    {
        QSize size = QTabBar::tabSizeHint(index);
        size.setHeight(28);
        return size;
    }

    void paintEvent(QPaintEvent*) override
    {
        QStylePainter painter(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            painter.drawControl(QStyle::CE_TabBarTab, opt);

            // Manually draw the text
            QRect text_rect = opt.rect.adjusted(10, 0, -30, 0);
            painter.setPen(Qt::white);
            painter.setFont(QApplication::font());
            painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, tabText(i));

            // Position the close button
            if (tabsClosable()) {
                QRect optRect = opt.rect;
                optRect.setX(optRect.right() - 20);
                optRect.setY(optRect.y() + 6);
                optRect.setSize(QSize(16, 16));

                if (QWidget* closeButton = tabButton(i, QTabBar::RightSide)) {
                    closeButton->setGeometry(optRect);
                }
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};


class VerticalTabsWidget : public QTabWidget {
public:
    explicit VerticalTabsWidget(QWidget* parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new VerticalTabBar(this));
        setTabPosition(QTabWidget::West);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        // Do nothing - ignore wheel events
        event->ignore();
    }
};
