#pragma once

#include <chrono>
#include <string>

#include <QScreen>
#include <QWidget>

#include "logger.h"

inline void ensure_window_is_visible(QWidget *win, bool move_only = false)
{
    try {
        if (!move_only) {
            win->setVisible(true);
        }

        QScreen *screen = QGuiApplication::screenAt(win->geometry().center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        QRect screen_geometry = screen->geometry();
        QRect window_geometry = win->geometry();

        if (!screen_geometry.contains(window_geometry.topLeft())) {
            win->move(screen_geometry.topLeft() + QPoint(50, 50));
        }
    } catch (...) {
        logger::log_error("ensure_window_is_visible: unexpected error");
    }
}


class Timer {
public:
    explicit Timer(const std::string &name) : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

    ~Timer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start_).count();
        std::cout << name_ << " execution time: " << duration << " ms" << std::endl;
    }

private:
    std::string name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};