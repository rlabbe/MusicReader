#pragma once

#include <QtWidgets>
#include <QWebEngineView>
#include <vector>
#include <filesystem>

class TourDialog : public QDialog {
    Q_OBJECT

public:
    explicit TourDialog(QWidget* parent = nullptr);

private slots:
    void next_slide();
    void prev_slide();

private:
    void load_tour_data();
    void update_display();
    void update_buttons();

    QWebEngineView* web_view_;
    QPushButton* prev_button_;
    QPushButton* next_button_;
    QLabel* page_label_;

    std::vector<std::string> slide_names_;
    int current_slide_;
};