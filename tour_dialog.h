#pragma once

#include <QtWidgets>
#include <vector>


// displays a series of images with next/previous buttons to guide the user
// through the main features of the application.
//
// uses ./documentation/tour.dat to get the list of images to display
class TourDialog : public QDialog {
    Q_OBJECT

public:
    explicit TourDialog(QWidget *parent = nullptr);

private slots:
    void next_slide();
    void prev_slide();

private:
    void load_tour_data();
    void update_display();
    void update_buttons();
    void size_to_fit_images();

    QLabel *image_label_;
    QPushButton *prev_button_;
    QPushButton *next_button_;
    QPushButton *done_button_;
    QLabel *page_label_;

    std::vector<QPixmap> slides_;
    std::vector<std::string> slide_names_;
    int current_slide_;
};