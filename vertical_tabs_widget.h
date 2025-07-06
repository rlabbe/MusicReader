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
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);

            // Skip painting the tab being dragged
            if (opt.state & QStyle::State_Selected &&
                QApplication::mouseButtons() & Qt::LeftButton) {
                continue;
            }

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
                if (QWidget *closeButton = tabButton(i, QTabBar::RightSide)) {
                    closeButton->setGeometry(optRect);
                }
            }
        }
    }
    void wheelEvent(QWheelEvent *event) override
    {
        event->ignore();
    }
};















class VerticalTabsWidget : public QTabWidget {
public:
	explicit VerticalTabsWidget(QWidget *parent = nullptr) : QTabWidget(parent)
	{
		setTabBar(new VerticalTabBar(this));
		setTabPosition(QTabWidget::West);
	}

	void wheelEvent(QWheelEvent *event) override
	{
		// Do nothing - ignore wheel events
		event->ignore();
	}
};



class HorizontalTabBar : public QTabBar {
	Q_OBJECT
public:
	explicit HorizontalTabBar(QWidget *parent = nullptr) : QTabBar(parent)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		setDocumentMode(true);
		setElideMode(Qt::ElideRight);
		setMovable(true);

		setStyleSheet(R"(
    QTabBar::tab {
        background: #454545;
        text-align: center;
        padding: 4px 8px;
        min-width: 80px;
        min-height: 24px;
        max-height: 28px;
        border-top-left-radius: 8px;
        border-top-right-radius: 8px;
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


protected:
	void wheelEvent(QWheelEvent *event) override
	{
		// Do nothing - ignore wheel events to prevent tab switching
		event->ignore();
	}

	QSize tabSizeHint(int index) const override
	{
		QSize size = QTabBar::tabSizeHint(index);
		size.setHeight(28);
		return size;
	}


};


class HorizontalTabWidget : public QTabWidget {
public:
	explicit HorizontalTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
	{
		setTabBar(new HorizontalTabBar(this));
		setTabPosition(QTabWidget::North);
	}

	void wheelEvent(QWheelEvent *event) override
	{
		// Do nothing - ignore wheel events
		event->ignore();
	}
};



