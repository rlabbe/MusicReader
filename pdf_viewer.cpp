#include "pdf_viewer.h"
#include <iostream>
#include "border.h"
#include "logger.h"


PDFViewer::PDFViewer(Document *document, ConfigFile *config, int page, StatusBar *sbar, QWidget *parent)
    : QWidget(parent)
    , document_(document)
    , status_bar_(sbar)
    , config_(config)
    , drawing_margin_(false)
{
    setFocusPolicy(Qt::StrongFocus);
    init_ui(page);

    //TODO Qt6 might use QEvent::ApplicationPaletteChange
    /*
         void changeEvent(QEvent *event) override {
        if (event->type() == QEvent::ApplicationPaletteChange) {
            // Handle palette change here
            QPalette newPalette = QApplication::palette();
            // ... use newPalette ...
        }
        QWidget::changeEvent(event);
    }

    * */

    //connect(qobject_cast<QGuiApplication *>(QCoreApplication::instance()), &QGuiApplication::paletteChanged, this, &PDFViewer::refresh);

    if (document_->page_count() > 0)
        get_page(page, true);
}

int PDFViewer::page_count() const
{
    return document_->page_count();
}

int PDFViewer::current_page() const
{
    if (page_.is_empty()) return 1;
    return page_.page_num;
}

bool PDFViewer::single_page_view() const
{
    return config_->page_view_count() == 1;
}

void PDFViewer::replace_document(Document *document, int page)
{
    document_ = document;
    get_page(page);
}

void PDFViewer::refresh()
{
    get_page(current_page());
    update_scrollbar_visibility();
}

void PDFViewer::page_up()
{
    change_page(single_page_view() ? -1 : -2);
}

void PDFViewer::page_down()
{
    change_page(single_page_view() ? 1 : 2);
}

void PDFViewer::change_page(int step)
{
    int new_page = qBound(1, current_page() + step, page_count());
    scrollbar_->setValue(new_page);
    get_page(new_page);
}

void PDFViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_PageUp:
    case Qt::Key_Up:
        page_up();
        event->accept();
        break;
    case Qt::Key_PageDown:
    case Qt::Key_Down:
    case Qt::Key_Space:
        page_down();
        event->accept();
        break;
    case Qt::Key_Left:
        change_page(-1);
        event->accept();

        break;
    case Qt::Key_Right:
        change_page(1);
        logger::log_info("some stuff");
        event->accept();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void PDFViewer::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0) {
        page_up();
    } else {
        page_down();
    }
}

bool PDFViewer::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        auto *gesture = dynamic_cast<QSwipeGesture *>(static_cast<QGestureEvent *>(event)->gesture(Qt::SwipeGesture));

        if (gesture->horizontalDirection() == QSwipeGesture::Left) {
            page_down();
        } else {
            page_up();
        }
        return true;
    }
    return QWidget::event(event);
}

void PDFViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    _update_image();
}

void PDFViewer::init_ui(int page)
{
    layout_ = new QHBoxLayout(this);  // Change to horizontal layout

    scrollbar_ = new QScrollBar(Qt::Vertical, this);
    scrollbar_->setMinimum(1);
    scrollbar_->setMaximum(page_count());
    scrollbar_->setValue(page);
    connect(scrollbar_, &QScrollBar::valueChanged, this, &PDFViewer::on_scrollbar_value_changed);

    label_ = new QLabel(this);
    label_->setStyleSheet("border: 0px;");
    label_->setAlignment(Qt::AlignCenter);

    layout_->addWidget(label_, 1);  // Stretch document display
    layout_->addWidget(scrollbar_);

    setLayout(layout_);
    update_scrollbar_visibility();
}

void PDFViewer::on_scrollbar_value_changed(int new_page)
{
    if (new_page != current_page()) {
        get_page(new_page);
    }
}

void PDFViewer::update_scrollbar_visibility()
{
    bool all_pages_shown = (page_count() == 1) || (page_count() == 2 && single_page_view());
    scrollbar_->setVisible(!all_pages_shown);
}

void PDFViewer::get_page(int page_num, bool first_call)
{
    if (document_->page_count() == 0) return;

    if (single_page_view() || page_num == document_->page_count()) {
        page_ = get_single_page(page_num);
    } else {
        page_ = get_double_page(page_num);
    }

    _update_image();
    if (first_call) {
        adjust_initial_subwindow_size();
    }
}

Page PDFViewer::get_single_page(int page_num)
{
    auto page = document_->get_page(page_num);
    if (!page.is_empty())
        aspect_ratio_ = double(page.width()) / page.height();

    return page;
}

Page PDFViewer::get_double_page(int page_num)
{
    bool zoom_to_content = config_->zoom_to_content();
    int margin = config_->border_margin();

    Page p1 = document_->get_page(page_num);
    Page p2 = document_->get_page(page_num + 1);

    // Determine cropped dimensions if zooming to content
    QRect p1_crop = zoom_to_content
        ? QRect(p1.border.left, p1.border.top,
                p1.border.right - p1.border.left,
                p1.border.bottom - p1.border.top)
        : QRect(0, 0, p1.width(), p1.height());

    QRect p2_crop = zoom_to_content
        ? QRect(p2.border.left, p2.border.top,
                p2.border.right - p2.border.left,
                p2.border.bottom - p2.border.top)
        : QRect(0, 0, p2.width(), p2.height());

    // New dimensions including space for the separator line
    int line_width = 8;
    int combined_width = p1_crop.width() + p2_crop.width() + line_width;
    int max_height = std::max(p1_crop.height(), p2_crop.height());

    aspect_ratio_ = static_cast<double>(combined_width) / max_height;

    // Create the combined QPixmap
    QPixmap combined_image(combined_width, max_height);
    QColor back_color = static_cast<QApplication *>(QApplication::instance())->palette().color(QPalette::Window);

    combined_image.fill(back_color);

    QPainter painter(&combined_image);

    // Center each cropped page within the available height
    int p1_offset = (max_height - p1_crop.height()) / 2;
    int p2_offset = (max_height - p2_crop.height()) / 2;

    // Draw cropped pages directly
    painter.drawPixmap(0, p1_offset, p1.img, p1_crop.x(), p1_crop.y(), p1_crop.width(), p1_crop.height());
    painter.drawPixmap(p1_crop.width() + line_width, p2_offset, p2.img, p2_crop.x(), p2_crop.y(), p2_crop.width(), p2_crop.height());

    // Draw separator line
    painter.setPen(QPen(back_color, line_width));
    painter.drawLine(p1_crop.width(), 0, p1_crop.width(), max_height);

    painter.end();

    return Page(combined_image, page_num, true);//true for double page
}


void PDFViewer::_update_image(const QString &message)
{
    if (page_.is_empty()) {
        label_->setText(message.isEmpty() ? "Loading..." : message);
        label_->setAlignment(Qt::AlignCenter);
        label_->setStyleSheet("background-color: white; color: black; font-size: 16pt;");
        return;
    }

    label_->setStyleSheet("");

    QSize max_size;
    
    if (config_->allow_oversize()) {
        max_size = label_->size();
        std::cout << "max_size label: " << max_size.width() << " " << max_size.height() << std::endl;
    } else {
        max_size = page_.img.size().boundedTo(label_->size());
        std::cout << "max_size image: " << max_size.width() << " " << max_size.height() << std::endl;
    }

    label_->setAlignment(Qt::AlignTop | Qt::AlignCenter);
    label_->setScaledContents(false);
    label_->setContentsMargins(0, 0, 0, 0);
    label_->setPixmap(page_.img.scaled(max_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PDFViewer::adjust_initial_subwindow_size()
{
    if (page_.is_empty()) return;

    QSize max_size = parentWidget()->size();
    QSize scaled_size = page_.size().scaled(max_size, Qt::KeepAspectRatio);
    resize(scaled_size);
    label_->resize(scaled_size);
    setMinimumSize(1, 1);
}

void PDFViewer::adjust_subwindow_size()
{
    /*if (page_.is_empty()) return;

    QSize max_size = parentWidget()->size();
    QSize new_size = QSize(std::min(page_.width(), max_size.width()), std::min(page_.height(), max_size.height()));
    resize(new_size);*/
}
