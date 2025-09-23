#include "tour_dialog.h"
#include "logger.h"
#include <fstream>
#include <filesystem>


TourDialog::TourDialog(QWidget *parent)
    : QDialog(parent)
    , image_label_(nullptr)
    , prev_button_(nullptr)
    , next_button_(nullptr)
    , done_button_(nullptr)
    , page_label_(nullptr)
    , current_slide_(0)
{
    setWindowTitle("MusicReader Tour");
    setModal(true);

    // Center on parent window
    if (parent) {
        QRect parent_geometry = parent->geometry();
        int x = parent_geometry.x() + (parent_geometry.width() - width()) / 2;
        int y = parent_geometry.y() + (parent_geometry.height() - height()) / 2;
        move(x, y);
    }

    QVBoxLayout *main_layout = new QVBoxLayout(this);

    // Image display area
    image_label_ = new QLabel;
    image_label_->setAlignment(Qt::AlignCenter);
    image_label_->setStyleSheet("border: 1px solid gray;");
    image_label_->setScaledContents(true);
    main_layout->addWidget(image_label_);

    // Button layout
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

    connect(prev_button_, &QPushButton::clicked, this, &TourDialog::prev_slide);
    connect(next_button_, &QPushButton::clicked, this, &TourDialog::next_slide);

    load_tour_data();
    size_to_fit_images();
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

        QPixmap pixmap(QString::fromStdString(image_path.string()));
        if (pixmap.isNull()) {
            logger::warning("Failed to load tour image: {}", image_path.string());
            continue;
        }

        slides_.push_back(pixmap);
        slide_names_.push_back(filename);
    }

    if (slides_.empty()) {
        logger::error("No valid tour images found");
        // Add a placeholder message
        QPixmap placeholder(760, 570);
        placeholder.fill(Qt::white);
        QPainter painter(&placeholder);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 16));
        painter.drawText(placeholder.rect(), Qt::AlignCenter, "No tour images available");
        slides_.push_back(placeholder);
        slide_names_.push_back("placeholder");
    }
}

void TourDialog::update_display()
{
    if (current_slide_ >= 0 && current_slide_ < static_cast<int>(slides_.size())) {
        image_label_->setPixmap(slides_[current_slide_]);
        page_label_->setText(QString("%1 of %2").arg(current_slide_ + 1).arg(slides_.size()));
    }
}

void TourDialog::update_buttons()
{
    prev_button_->setEnabled(current_slide_ > 0);

    // Change "Next" to "Done" on last slide
    if (current_slide_ >= static_cast<int>(slides_.size()) - 1) {
        next_button_->setText("Done");
    } else {
        next_button_->setText("Next");
    }
}

void TourDialog::next_slide()
{
    if (current_slide_ >= static_cast<int>(slides_.size()) - 1) {
        // On last slide, "Done" button closes dialog
        accept();
        return;
    }

    current_slide_++;
    update_display();
    update_buttons();
}

void TourDialog::size_to_fit_images()
{
    if (slides_.empty()) return;

    int max_width = 0;
    int max_height = 0;

    for (const auto &pixmap : slides_) {
        max_width = std::max(max_width, pixmap.width());
        max_height = std::max(max_height, pixmap.height());
    }

    // Limit to 1024x768
    max_width = std::min(max_width, 1024);
    max_height = std::min(max_height, 768);

    // Add space for buttons and layout margins (approximately 100 pixels)
    int dialog_width = max_width + 40;  // margins
    int dialog_height = max_height + 100;  // buttons + margins

    setFixedSize(dialog_width, dialog_height);
    image_label_->setFixedSize(max_width, max_height);

    // Re-center on parent after resize
    if (parentWidget()) {
        QRect parent_geometry = parentWidget()->geometry();
        int x = parent_geometry.x() + (parent_geometry.width() - width()) / 2;
        int y = parent_geometry.y() + (parent_geometry.height() - height()) / 2;
        move(x, y);
    }
}

void TourDialog::prev_slide()
{
    if (current_slide_ > 0) {
        current_slide_--;
        update_display();
        update_buttons();
    }
}