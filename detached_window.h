#pragma once

#include <QtWidgets>

class StatusBar;

// Top-level window that holds tabs dragged out of the main window. It has its
// own status bar, so a viewer moved here writes its page information to this
// window.
class DetachedWindow : public QWidget {
public:
    explicit DetachedWindow(bool horizontal_tabs);

    QTabWidget* tab_widget() const { return tab_widget_; }
    StatusBar* status_bar() const { return status_bar_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QTabWidget* tab_widget_ = nullptr;
    StatusBar* status_bar_ = nullptr;
};
