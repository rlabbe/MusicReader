#include "pdf_viewer.h"
#include <thread>
#include <iostream>
#include "border.h"
#include "logger.h"
#include "status_bar.h"
#include "config_file.h"
#include "log_timer.h"

PDFViewer::PDFViewer(std::shared_ptr<Document> document,
                     ConfigFile *config,
                     int page,
                     StatusBar *sbar,
                     QWidget *parent)
    : QWidget(parent)
    , document_(document)
    , status_bar_(sbar)
    , config_(config)
    , drawing_margin_(false)
{
    setFocusPolicy(Qt::StrongFocus);
    init_ui(page);

    connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);
    get_page(page, true);

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
}


PDFViewer::~PDFViewer()
{
    if (document_) {
        document_->save();
    }
}

void PDFViewer::update_status_bar()
{
    if (!status_bar_) return;
    if (!isVisible()) return;

    if (page_.is_empty()) {
        status_bar_->clear_page_count();
        return;
    }

    status_bar_->set_page_count(current_page(), document_->page_count());
}


int PDFViewer::page_count() const
{
    return document_->page_count();
}


int PDFViewer::current_page() const
{
    //if (page_.is_empty()) return 1;
    return page_.page_num;
}

bool PDFViewer::single_page_view() const
{
    return config_->page_view_count() == 1 || page_count() == 1;
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
    int count = page_count();
    int new_page = qBound(1, current_page() + step, count);

    // don't go to last page if even number of pages
    if (new_page == count && double_page_view() && new_page % 2 == 0)
        new_page = count - 1;

    manual_scrollbar_change_ = true;
    scrollbar_->setValue(new_page);
    manual_scrollbar_change_ = false;

    get_page(new_page);
}

void PDFViewer::replace_document(std::shared_ptr<Document> document, int page)
{
    document_ = document;
    connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);
    get_page(page, false);
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
    update_image();
}


void PDFViewer::init_ui(int page)
{
    layout_ = new QHBoxLayout(this);

    // Remove extra spacing/margins
    layout_->setContentsMargins(0, 4, 0, 4);
    layout_->setSpacing(0);

    scrollbar_ = new QScrollBar(Qt::Vertical, this);
    scrollbar_->setMinimum(1);
    scrollbar_->setMaximum(page_count());
    manual_scrollbar_change_ = true;
    scrollbar_->setValue(page);
    manual_scrollbar_change_ = false;

    connect(scrollbar_, &QScrollBar::valueChanged, this, &PDFViewer::on_scrollbar_value_changed);

    label_ = new QLabel(this);
    label_->setStyleSheet("border: 0px;");


    label_->setAlignment(Qt::AlignTop | page_alignment());

    label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label_->setMinimumSize(1, 1);  // Prevent weird shrinking issues

    layout_->addWidget(label_, 1);  // Stretch document display
    layout_->addWidget(scrollbar_);

    setLayout(layout_);
    update_scrollbar_visibility();
}


void PDFViewer::on_scrollbar_value_changed(int new_page)
{
    if (manual_scrollbar_change_) return;

    if (new_page != current_page()) {
        get_page(new_page);
    }
}

void PDFViewer::update_scrollbar_visibility()
{
    bool all_pages_shown = false;
    int page_count = document_->page_count();

    if (page_count == 1)
        all_pages_shown = true;
    else if (page_count == 2 && !single_page_view())
        all_pages_shown = true;

    scrollbar_->setVisible(!all_pages_shown);
}

void PDFViewer::prefetch_async(int page_num)
{
    const int count = document_->page_count();
    if (page_num < 1 || page_num > count)
        return;

    const bool is_double = double_page_view();

    // Choose correct slot
    PrefetchEntry &slot = (page_num > page_.page_num) ? prefetch_.next : prefetch_.prev;

    if (slot.page_num == page_num && slot.double_page == is_double)
        return; // Already prefetched, matching mode

    std::jthread([this, page_num, is_double]() {
        PrefetchEntry entry = is_double
            ? make_double_page_entry(page_num)
            : make_single_page_entry(page_num);

        if (entry.rendered.isNull())
            return;

        {
            std::lock_guard lock(prefetch_mutex_);
            PrefetchEntry &dest = (entry.page_num > page_.page_num) ? prefetch_.next : prefetch_.prev;
            dest = std::move(entry);
        }
    }).detach();
}


void PDFViewer::clear_prefetch()
{
    prefetch_.next.clear();
    prefetch_.prev.clear();
}


PDFViewer::PrefetchEntry PDFViewer::make_double_page_entry(int page_num) const
{
    PrefetchEntry entry(page_num, true, config_->zoom_to_content(), config_->border_margin());

    entry.p1 = document_->get_page(page_num);
    if (page_num < document_->page_count())
        entry.p2 = document_->get_page(page_num + 1);
    else
        copy_blank_image(entry.p1, entry.p2);

    if (!entry.p1.is_empty() && !entry.p2.is_empty())
        entry.rendered = compose_double_page(entry.p1, entry.p2);

    return entry;
}

PDFViewer::PrefetchEntry PDFViewer::make_single_page_entry(int page_num) const
{
    PrefetchEntry entry(page_num, false, config_->zoom_to_content(), config_->border_margin());

    entry.p1 = document_->get_page(page_num);
    if (!entry.p1.is_empty()) {
        if (config_->zoom_to_content())
            entry.rendered = entry.p1.resize_by_border(config_->border_margin());
        else
            entry.rendered = entry.p1.img;
    }

    return entry;
}


void PDFViewer::get_page(int page_num, bool first_call)
{
    const int count = document_->page_count();
    if (count == 0) return;

    const bool is_double = double_page_view();

    // Prefetch next and previous pages asap to maximize chances of being done
    // by the next request
    const int delta = is_double ? 2 : 1;

    if (page_num + delta <= count)
        prefetch_async(page_num + delta);

    if (page_num - delta >= 1)
        prefetch_async(page_num - delta);

    {
        std::lock_guard lock(prefetch_mutex_);
        if (prefetch_.next.valid(page_num, *config_)) {
            page_ = Page(prefetch_.next.rendered, page_num, is_double);
            update_image();
            return;
        } else if (prefetch_.prev.valid(page_num, *config_)) {
            page_ = Page(prefetch_.prev.rendered, page_num, is_double);
            update_image();
            return;
        }
    }

    PrefetchEntry entry = is_double
        ? make_double_page_entry(page_num)
        : make_single_page_entry(page_num);

    page_ = Page(entry.rendered, page_num, is_double);
    update_image();
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
    const bool zoom = config_->zoom_to_content();
    const int margin = config_->border_margin();
    const bool last_page = (page_num == page_count());

    Page p1 = document_->get_page(page_num);
    Page p2;
    if (!last_page)
        p2 = document_->get_page(page_num + 1);
    else
        // ensure the last page has a valid blank image
        // so it is rendered correctly in double page mode
        copy_blank_image(p1, p2);

    if (p1.is_empty() || p2.is_empty()) {
        return Page(page_num); // Empty page to show "Loading..."
    }

    // Determine cropped dimensions if zooming to content
    QRect p1_crop = zoom ? border_to_qrect(p1.border, margin) : QRect(0, 0, p1.width(), p1.height());
    QRect p2_crop = zoom ? border_to_qrect(p1.border, margin) : QRect(0, 0, p2.width(), p2.height());

    // New dimensions including space for the separator line
    int line_width = 8;
    int combined_width = p1_crop.width() + p2_crop.width() + line_width;
    int max_height = std::max(p1_crop.height(), p2_crop.height());

    aspect_ratio_ = static_cast<double>(combined_width) / max_height;

    // Create the combined QPixmap
    QPixmap combined_image(combined_width, max_height);
    QColor back_color = 0xffffff;

    combined_image.fill(back_color);

    // Center each cropped page within the available height
    int p1_offset = (max_height - p1_crop.height()) / 2;
    int p2_offset = (max_height - p2_crop.height()) / 2;

    auto w = p2.img.width();
    auto h = p2.img.height();

    // Draw cropped pages directly
    QPainter painter(&combined_image);
    painter.drawPixmap(0, p1_offset, p1.img, p1_crop.x(), p1_crop.y(), p1_crop.width(), p1_crop.height());
    painter.drawPixmap(p1_crop.width() + line_width, p2_offset, p2.img, p2_crop.x(), p2_crop.y(), p2_crop.width(), p2_crop.height());
    painter.end();

    return Page(combined_image, page_num, true); //true for double page
}

QPixmap PDFViewer::compose_double_page(const Page &p1, const Page &p2) const
{
    const bool zoom = config_->zoom_to_content();
    const int margin = config_->border_margin();

    QRect p1_crop = zoom ? border_to_qrect(p1.border, margin) : QRect(0, 0, p1.width(), p1.height());
    QRect p2_crop = zoom ? border_to_qrect(p2.border, margin) : QRect(0, 0, p2.width(), p2.height());

    int line_width = 8;
    int combined_width = p1_crop.width() + p2_crop.width() + line_width;
    int max_height = std::max(p1_crop.height(), p2_crop.height());

    QPixmap combined_image(combined_width, max_height);
    combined_image.fill(Qt::white);

    int p1_offset = (max_height - p1_crop.height()) / 2;
    int p2_offset = (max_height - p2_crop.height()) / 2;

    QPainter painter(&combined_image);
    painter.drawPixmap(0, p1_offset, p1.img, p1_crop.x(), p1_crop.y(), p1_crop.width(), p1_crop.height());
    painter.drawPixmap(p1_crop.width() + line_width, p2_offset, p2.img, p2_crop.x(), p2_crop.y(), p2_crop.width(), p2_crop.height());
    painter.end();

    return combined_image;
}


void PDFViewer::on_page_loaded(int page_index)
{
    int page_num = current_page();
    int count = page_count();

    if (single_page_view() || count == 1) {
        if (page_num == page_index) {
            PrefetchEntry entry = make_single_page_entry(page_num);
            page_ = Page(entry.rendered, page_num, false);
            update_image();
        }
    } else {
        if (page_num == page_index || page_num + 1 == page_index) {
            PrefetchEntry entry = make_double_page_entry(page_num);
            page_ = Page(entry.rendered, page_num, true);
            update_image();
        }
    }
}



void PDFViewer::update_image(const QString &message)
{
    update_status_bar();

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
    } else {
        max_size = page_.img.size().boundedTo(label_->size());
    }

    label_->setAlignment(Qt::AlignTop | page_alignment());
    label_->setScaledContents(false);
    label_->setContentsMargins(0, 0, 0, 0);
    QPixmap scaled_pixmap = page_.img.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    label_->setPixmap(scaled_pixmap);

    adjust_initial_subwindow_size(); // safe to call multiple times
}


void PDFViewer::adjust_initial_subwindow_size()
{
    if (page_.is_empty()) return;

    static bool first_time = true;
    if (!first_time) return;
    first_time = false;

    QSize max_size = parentWidget()->size();
    QSize scaled_size = page_.size().scaled(max_size, Qt::KeepAspectRatio);
    resize(scaled_size);
    label_->resize(scaled_size);
    setMinimumSize(1, 1);
}

Qt::AlignmentFlag PDFViewer::page_alignment() const
{
    switch (config_->page_location()) {
    case PageLocation::Left: return Qt::AlignmentFlag::AlignLeft;
    default:  return Qt::AlignmentFlag::AlignHCenter;
    };
}



bool PDFViewer::PrefetchEntry::valid(int target_page_num, ConfigFile &config) const
{
    return page_num == target_page_num &&
        double_page == (config.page_view_count() == 2) &&
        zoom_to_content == config.zoom_to_content() &&
        border_margin == config.border_margin() &&
        !rendered.isNull();
}
