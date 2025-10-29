#pragma once
#include <QGuiApplication>
#include <QCursor>

class WaitCursor {
public:
    WaitCursor() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QGuiApplication::restoreOverrideCursor(); }
};
