#pragma once


#include <QtWidgets>
#include "music_reader.h"


class BookmarkTitleBar : public QWidget {
    Q_OBJECT

public:
    explicit BookmarkTitleBar(MusicReader* main_window, QWidget* parent = nullptr)
        : QWidget(parent)
        , main_window_(main_window)
    {

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(5, 2, 5, 2);
        layout->setSpacing(5);

        // Add a label for "Bookmarks"
        title_label_ = new QLabel("Bookmarks", this);
        title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(title_label_, 1);

        // Add a close button
        close_button_ = new QPushButton("✕", this);
        close_button_->setFixedSize(20, 20);
        close_button_->setToolTip("Close bookmarks");
        connect(close_button_, &QPushButton::clicked, this, &BookmarkTitleBar::onCloseClicked);
        layout->addWidget(close_button_, 0);

        setLayout(layout);
    }

private slots:
    void onCloseClicked()
    {
        if (main_window_)
            parentWidget()->setVisible(false);
    }

private:
    QWidget* main_window_;
    QLabel* title_label_;
    QPushButton* close_button_;
};
