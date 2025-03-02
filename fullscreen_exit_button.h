#pragma once

#include <QWidget>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>

class FullscreenExitButton : public QWidget {
    Q_OBJECT

public:
    explicit FullscreenExitButton(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(60, 60);  // Circular button size

        // Create the exit button
        QPushButton *exit_button = new QPushButton(this);
        exit_button->setFixedSize(60, 60);
        exit_button->setStyleSheet(R"(
            QPushButton {
                background-color: rgba(50, 50, 50, 200);
                border-radius: 30px;
                border: 2px solid white;
                color: white;
                font-size: 20px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: rgba(255, 50, 50, 220);
            }
        )");
        exit_button->setText("x");
        connect(exit_button, &QPushButton::clicked, this, &FullscreenExitButton::exitFullscreen);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(exit_button);
        layout->setContentsMargins(0, 0, 0, 0);
        setLayout(layout);

        // Initialize animations
        fade_in_ = new QPropertyAnimation(this, "windowOpacity");
        fade_in_->setDuration(300);
        fade_in_->setStartValue(0.0);
        fade_in_->setEndValue(1.0);

        fade_out_ = new QPropertyAnimation(this, "windowOpacity");
        fade_out_->setDuration(500);
        fade_out_->setStartValue(1.0);
        fade_out_->setEndValue(0.0);
        connect(fade_out_, &QPropertyAnimation::finished, this, &QWidget::hide);

        slide_ = new QPropertyAnimation(this, "pos");
        slide_->setDuration(300);

        // Auto-hide timer
        auto_hide_timer_ = new QTimer(this);
        auto_hide_timer_->setInterval(2500);
        connect(auto_hide_timer_, &QTimer::timeout, this, &FullscreenExitButton::hideWithAnimation);
    }

    void showAtTop()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        QRect screen_geometry = screen->geometry();
        int x = (screen_geometry.width() - width()) / 2;
        int start_y = -height();
        int end_y = 10;

        move(x, start_y);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);  // Enable mouse clicks again
        show();

        slide_->setStartValue(QPoint(x, start_y));
        slide_->setEndValue(QPoint(x, end_y));
        slide_->start();

        fade_in_->start();
        auto_hide_timer_->start();
    }

public slots:
    void exitFullscreen()
    {
        if (parentWidget()) {
            parentWidget()->showNormal();  // Exit fullscreen
        }
        hide();
    }

    void hideWithAnimation()
    {
        fade_out_->start();
        connect(fade_out_, &QPropertyAnimation::finished, this, [this]() {
            setAttribute(Qt::WA_TransparentForMouseEvents, true);  // Ignore mouse clicks
            hide();
        });
    }

private:
    QPropertyAnimation *fade_in_;
    QPropertyAnimation *fade_out_;
    QPropertyAnimation *slide_;
    QTimer *auto_hide_timer_;
};
