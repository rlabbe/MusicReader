#pragma once

#include <QtWidgets>


class VerticalTabBar : public QTabBar {
public:
    explicit VerticalTabBar(QWidget *parent = nullptr) : QTabBar(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        setDocumentMode(true);
        setElideMode(Qt::ElideRight);
        setMovable(true);

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

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            painter.drawControl(QStyle::CE_TabBarTab, opt);

            // Manually draw the text
            QRect text_rect = opt.rect.adjusted(10, 0, -10, 0);
            painter.setPen(Qt::white); // Force white text color
            painter.setFont(QApplication::font()); // Set a readable font
            painter.drawText(text_rect, Qt::AlignCenter, tabText(i));
        }
    }

    void tabLayoutChange() override
    {
        for (int i = 0; i < count(); ++i) {
            QWidget *close_button = tabButton(i, QTabBar::RightSide);
            if (close_button) {
                close_button->setFixedSize(16, 16);
                close_button->move(tabRect(i).right() - 20, tabRect(i).center().y() - 8);
            }
        }
    }
};


class VerticalTabsWidget : public QTabWidget {
public:
    explicit VerticalTabsWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        setTabBar(new VerticalTabBar(this));
        setTabPosition(QTabWidget::West);
    }
};



