#pragma once

#include <QScreen>
#include <QWidget>
#include "logger.h"

inline void ensure_window_is_visible(QWidget* win, bool move_only = false)
{
    try {
        if (!move_only) {
            win->setVisible(true);
        }

        QScreen* screen = QGuiApplication::screenAt(win->geometry().center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        QRect screen_geometry = screen->geometry();
        QRect window_geometry = win->geometry();

        if (!screen_geometry.contains(window_geometry.topLeft())) {
            win->move(screen_geometry.topLeft() + QPoint(50, 50));
        }
    } catch (...) {
        logger::error("ensure_window_is_visible: unexpected error");
    }
}
