#include "tour_dialog.h"
#include "logger.h"
#include <fstream>
#include <QUrl>
#include <QWebEngineSettings>

TourDialog::TourDialog(QWidget *parent)
    : QDialog(parent)
    , web_view_(nullptr)
    , prev_button_(nullptr)
    , next_button_(nullptr)
    , page_label_(nullptr)
    , current_slide_(0)
{
    setWindowTitle("MusicReader Tour");
    setModal(true);
    setFixedSize(800, 650);

    QVBoxLayout *main_layout = new QVBoxLayout(this);

    web_view_ = new QWebEngineView;
    main_layout->addWidget(web_view_);

    QHBoxLayout *button_layout = new QHBoxLayout;
    prev_button_ = new QPushButton("Previous");
    next_button_ = new QPushButton("Next");
    page_label_ = new QLabel;
    page_label_->setAlignment(Qt::AlignCenter);

    button_layout->addWidget(prev_button_);
    button_layout->addStretch();
    button_layout->addWidget(page_label_);
    button_layout->addStretch();
    button_layout->addWidget(next_button_);

    main_layout->addLayout(button_layout);

    // In constructor, after creating web_view_:
    web_view_->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, false);
    web_view_->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, false);
    connect(prev_button_, &QPushButton::clicked, this, &TourDialog::prev_slide);
    connect(next_button_, &QPushButton::clicked, this, &TourDialog::next_slide);

    load_tour_data();
    update_display();
    update_buttons();
}

void TourDialog::load_tour_data()
{
    std::filesystem::path tour_file = "./documentation/tour.dat";

    if (!std::filesystem::exists(tour_file)) {
        logger::error("Tour data file not found: {}", tour_file.string());
        return;
    }

    std::ifstream file(tour_file.string());
    if (!file.is_open()) {
        logger::error("Failed to open tour data file: {}", tour_file.string());
        return;
    }

    std::string filename;
    while (std::getline(file, filename)) {
        if (filename.empty() || filename[0] == '#')
            continue;

        std::filesystem::path image_path = std::filesystem::path("./documentation") / filename;

        if (!std::filesystem::exists(image_path)) {
            logger::warning("Tour image not found: {}", image_path.string());
            continue;
        }

        slide_names_.push_back(filename);
    }

    if (slide_names_.empty()) {
        logger::error("No valid tour images found");
        slide_names_.push_back("placeholder");
    }
}

void TourDialog::update_display()
{
    if (current_slide_ >= 0 && current_slide_ < static_cast<int>(slide_names_.size())) {
        std::filesystem::path image_path = std::filesystem::path("./documentation") / slide_names_[current_slide_];
        QUrl url = QUrl::fromLocalFile(QString::fromStdString(std::filesystem::absolute(image_path).string()));
        web_view_->load(url);

        page_label_->setText(QString("%1 of %2").arg(current_slide_ + 1).arg(slide_names_.size()));
    }
}

void TourDialog::update_buttons()
{
    prev_button_->setEnabled(current_slide_ > 0);

    if (current_slide_ >= static_cast<int>(slide_names_.size()) - 1) {
        next_button_->setText("Done");
    } else {
        next_button_->setText("Next");
    }
}

void TourDialog::next_slide()
{
    if (current_slide_ >= static_cast<int>(slide_names_.size()) - 1) {
        accept();
        return;
    }

    current_slide_++;
    update_display();
    update_buttons();
}

void TourDialog::prev_slide()
{
    if (current_slide_ > 0) {
        current_slide_--;
        update_display();
        update_buttons();
    }
}

void TourDialog::size_to_fit_images()
{
    // Not needed with fixed size web view
}