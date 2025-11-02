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

static inline std::string to_string(Qt::ApplicationState state)
{

    switch (state) {
        case Qt::ApplicationSuspended: return "Suspended";
        case Qt::ApplicationHidden: return "Hidden";
        case Qt::ApplicationInactive: return "Inactive";
        case Qt::ApplicationActive: return "Active";
        default: return "Unknown";
    }
}


// RAII class to save and restore the state of a variable
template<class T> class SaveState {
private:
    T& var_;
    T old_value;

public:
    SaveState(T& var, T value)
        : var_(var)
    {
        old_value = var;
        var_ = value;
    }
    ~SaveState() { var_ = old_value; }
};


