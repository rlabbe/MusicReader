#include "pdf_viewer.h"
#include <thread>
#include <iostream>
#include "border.h"
#include "logger.h"
#include "status_bar.h"
#include "config_file.h"
#include "log_timer.h"
#include "exception_logger.h"
#include "requires.h"
#include "fitz_utils.h"
#include "in_place_annotation_editor.h"
#include "musicreader.h"
#include "bookmark_panel.h"


PDFViewer::PDFViewer(std::shared_ptr<Document> document,
                     ConfigFile *config,
                     int page,
                     StatusBar *sbar,
                     QWidget *parent,
                     MusicReader *reader,
                     BookmarkPanel *panel)
    : QWidget(parent)
    , document_(document)
    , status_bar_(sbar)
    , config_(config)
    , drawing_margin_(false)
    , bookmark_panel_(panel)
{
    setFocusPolicy(Qt::StrongFocus);
    init_ui(page);

    if (document) {
        connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);
        get_page(page);
    }

    connect(this, &PDFViewer::annotation_mode_changed, reader, &MusicReader::on_annotation_mode_changed);

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
    REQUIRES(document_);
    if (document_)
        document_->save();
}


void PDFViewer::update_status_bar()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);
    REQUIRES(status_bar_);

    if (!status_bar_) return;
    if (!isVisible()) return;

    /*if (page_.is_empty()) {
        status_bar_->clear_page_count();
        return;
    }*/

    status_bar_->set_page_count(current_page(), document_->page_count());
}


bool PDFViewer::in_single_page_view() const
{
    SAFE_METHOD;
    REQUIRES_RET(config_, true);

    return config_->page_view_count() == 1 || page_count() == 1;
}


void PDFViewer::refresh()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    get_page(current_page());
    update_scrollbar_visibility();
}

void PDFViewer::page_up()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    bool single_page = in_single_page_view() || config_->page_step_size() == 1;

    change_page(single_page ? -1 : -2);
}

void PDFViewer::page_down()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    bool single_page = in_single_page_view() || config_->page_step_size() == 1;
    change_page(single_page ? 1 : 2);
}

void PDFViewer::change_page(int step)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(scrollbar_);

    int count = page_count();
    int new_page = qBound(1, current_page() + step, count);

    // don't go to last page if even number of pages
    if (new_page == count && in_double_page_view() && new_page % 2 == 0)
        new_page = count - 1;

    manual_scrollbar_change_ = true;
    scrollbar_->setValue(new_page);
    manual_scrollbar_change_ = false;

    if (document_)
        document_->prioritize();
    get_page(new_page);
}

void PDFViewer::replace_document(std::shared_ptr<Document> document, int page)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document || document == document_) return;
    document_ = document;
    connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);
    get_page(page);
}


void PDFViewer::keyPressEvent(QKeyEvent *event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Handle selection-related keys first
    if (event->key() == Qt::Key_Delete && has_selection_) {
        // Delete selected annotation
        if (document_->remove_annotation(selected_annotation_)) {
            clear_selection();
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape && has_selection_) {
        // Clear selection on Escape
        clear_selection();
        event->accept();
        return;
    }

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
        logger::info("some stuff");
        event->accept();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void PDFViewer::wheelEvent(QWheelEvent *event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->angleDelta().y() > 0)
        page_up();
    else
        page_down();
}

bool PDFViewer::event(QEvent *event)
{
    SAFE_METHOD;

    if (event->type() == QEvent::Gesture) {
        TRACE_FUNCTION;

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
    SAFE_METHOD;
    TRACE_FUNCTION;

    QWidget::resizeEvent(event);
    update_image();
}


void PDFViewer::init_ui(int page)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

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
    label_->setScaledContents(false);
    label_->setContentsMargins(0, 0, 0, 0);

    layout_->addWidget(label_, 1);  // Stretch document display
    layout_->addWidget(scrollbar_);

    annotation_editor_ = new InPlaceAnnotationEditor(annotation_font_, this);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_finished,
            this, &PDFViewer::on_annotation_text_finished);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_cancelled,
            this, &PDFViewer::on_annotation_text_cancelled);

    setLayout(layout_);
    update_scrollbar_visibility();

    QShortcut *delete_shortcut = new QShortcut(QKeySequence::Delete, this);
    delete_shortcut->setContext(Qt::WidgetShortcut); // Only when this widget has focus
    connect(delete_shortcut, &QShortcut::activated, this, [this]() {
        if (has_selection_ && document_) {

            if (document_->remove_annotation(selected_annotation_)) {
                clear_selection();
            }
        }
    });
}


void PDFViewer::on_scrollbar_value_changed(int new_page)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (manual_scrollbar_change_) return;

    if (new_page != current_page()) {
        get_page(new_page);
    }
}

void PDFViewer::update_scrollbar_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(scrollbar_);
    REQUIRES(document_);

    bool all_pages_shown = false;
    int page_count = document_->page_count();

    if (page_count == 1)
        all_pages_shown = true;
    else if (page_count == 2 && !in_single_page_view())
        all_pages_shown = true;

    scrollbar_->setVisible(!all_pages_shown);
}

void PDFViewer::prefetch_async(int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);

    std::jthread([this, page_num]() {
        const int count = document_->page_count();
        if (page_num < 1 || page_num > count)
            return;

        const bool is_double = in_double_page_view();

        // Choose correct slot
        PrefetchEntry &slot = (page_num > page_.page_num) ? prefetch_.next : prefetch_.prev;
        if (slot.page_num == page_num && slot.double_page == is_double)
            return; // Already prefetched, matching mode

        PrefetchEntry entry = is_double
            ? make_double_page_entry(page_num, false)
            : make_single_page_entry(page_num, false);

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
    SAFE_METHOD;
    TRACE_FUNCTION;

    prefetch_.next.clear();
    prefetch_.prev.clear();
}


PDFViewer::PrefetchEntry PDFViewer::make_double_page_entry(int page_num, bool is_current_page) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!config_ || !document_)
        return PrefetchEntry(page_num, false, false, 0);

    PrefetchEntry entry(page_num, true, config_->zoom_to_content(), config_->border_margin());
    entry.p1 = PixmapPage(document_->get_page(page_num, is_current_page));

    if (page_num < document_->page_count())
        entry.p2 = document_->get_page(page_num + 1, false);
    else
        copy_blank_image(entry.p1, entry.p2);

    if (!entry.p1.is_empty() && !entry.p2.is_empty())
        entry.rendered = compose_double_page(entry.p1, entry.p2);

    return entry;
}


PDFViewer::PrefetchEntry PDFViewer::make_single_page_entry(int page_num, bool is_current_page) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!config_ || !document_)
        return PrefetchEntry(page_num, false, false, 0);


    PrefetchEntry entry(page_num, false, config_->zoom_to_content(), config_->border_margin());

    entry.p1 = document_->get_page(page_num, is_current_page);
    if (!entry.p1.is_empty()) {
        if (config_->zoom_to_content())
            entry.rendered = Page::as_pixmap(entry.p1.resize_by_border(config_->border_margin()));
        else
            entry.rendered = entry.p1.as_pixmap();
    }
    return entry;
}


void PDFViewer::get_page(int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);

    const int count = document_->page_count();
    if (count == 0) return;

    const bool is_double = in_double_page_view();
    PrefetchEntry entry = is_double
        ? make_double_page_entry(page_num, true)
        : make_single_page_entry(page_num, true);

    page_ = PixmapPage(entry.rendered, page_num, is_double);

    update_image();

    // Prefetch next and previous pages asap to maximize chances of being done
    // by the next request
    const int delta = is_double ? 2 : 1;

    if (page_num + delta <= count)
        prefetch_async(page_num + delta);

    if (page_num - delta >= 1)
        prefetch_async(page_num - delta);

    if (bookmark_panel_)
        bookmark_panel_->select_page(page_num);
}



QPixmap PDFViewer::compose_double_page(const PixmapPage &p1, const PixmapPage &p2) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES_RET(config_, QPixmap());

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
    painter.drawPixmap(0, p1_offset, p1.pixmap, p1_crop.x(), p1_crop.y(), p1_crop.width(), p1_crop.height());
    painter.drawPixmap(p1_crop.width() + line_width, p2_offset, p2.pixmap, p2_crop.x(), p2_crop.y(), p2_crop.width(), p2_crop.height());
    painter.setPen(QPen(Qt::black, line_width));
    painter.drawLine(p1_crop.width() + line_width / 2, 0, p1_crop.width() + line_width / 2, max_height);
    painter.end();

    return combined_image;
}


void PDFViewer::on_page_loaded(std::string name, int page_index)
{
    SAFE_METHOD;

    if (name != document_->filename())
        return;

    int page_num = current_page();

    if (in_single_page_view()) {
        if (page_index != page_num)
            return;
    } else {
        if (page_index != page_num && page_index != page_num + 1)
            return;
    }

    TRACE_FUNCTION;
    int count = page_count();
    if (in_single_page_view() || count == 1) {
        if (page_num == page_index) {
            PrefetchEntry entry = make_single_page_entry(page_num, page_num == page_index);
            page_ = PixmapPage(entry.rendered, page_num, false);
            update_image();
        }
    } else {
        if (page_num == page_index || page_num + 1 == page_index) {
            PrefetchEntry entry = make_double_page_entry(page_num, page_num == page_index);
            page_ = PixmapPage(entry.rendered, page_num, true);
            update_image();
        }
    }
}


void PDFViewer::update_image(const QString &message)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("{} {} page:{}", document_->filename(), message.toStdString(), page_.page_num);
    REQUIRES(label_);
    REQUIRES(config_);

    update_status_bar();

    if (page_.is_empty()) {
        label_->setText(message.isEmpty() ? "Loading..." : message);
        label_->setAlignment(Qt::AlignCenter);
        label_->setStyleSheet("background-color: white; color: black; font-size: 16pt;");
        return;
    } else {
        label_->setStyleSheet("");
    }

    REQUIRES(document_);
    QSize max_size;
    if (config_->allow_oversize())
        max_size = label_->size();
    else
        max_size = page_.pixmap.size().boundedTo(label_->size());

    label_->setAlignment(Qt::AlignTop | page_alignment());
    QPixmap scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Draw selection box around selected annotation only
    if (has_selection_) {
        QPainter painter(&scaled_pixmap);

        // Find the selected annotation and draw dotted red box
        for (const auto &annotation : document_->annotations()) {
            if (annotation.handle_ == selected_annotation_) {
                QRect bounding_box = calculate_annotation_bounding_box(annotation);
                if (!bounding_box.isEmpty()) {
                    painter.setPen(QPen(Qt::red, 1, Qt::DotLine));
                    painter.drawRect(bounding_box);
                }
                break;
            }
        }
        painter.end();
    }

    label_->setPixmap(scaled_pixmap);
    adjust_initial_subwindow_size();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void PDFViewer::adjust_initial_subwindow_size()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(label_);

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
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES_RET(config_, Qt::AlignmentFlag::AlignLeft);

    switch (config_->page_location()) {
    case PageLocation::Left: return Qt::AlignmentFlag::AlignLeft;
    default:  return Qt::AlignmentFlag::AlignHCenter;
    };
}


bool PDFViewer::PrefetchEntry::valid(int target_page_num, ConfigFile &config) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    return page_num == target_page_num &&
        double_page == (config.page_view_count() == 2) &&
        zoom_to_content == config.zoom_to_content() &&
        border_margin == config.border_margin() &&
        !rendered.isNull();
}




void PDFViewer::set_text_annotation_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    text_annotation_mode_ = enabled;
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
}


void PDFViewer::mousePressEvent(QMouseEvent *event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    if (!document_) return;
    if (page_.is_empty()) return;

    setFocus();

    if (event->button() == Qt::LeftButton && text_annotation_mode_) {
        QPixmap displayed = label_->pixmap();
        last_click_target_ = get_click_target(event);

        auto [pdf_width, pdf_height] = document_->get_page_dimensions_points(last_click_target_.page_num);

        // Position editor directly at click point (screen coordinates)
        annotation_editor_->start_editing(event->pos());

        event->accept();
        return;
    } else if (event->button() == Qt::LeftButton) {
        // Handle annotation selection

        AnnotationHandle clicked_annotation = find_annotation_at_point(event);
        if (clicked_annotation) {
            // Clicked on an annotation - select it
            select_annotation(clicked_annotation);
            event->accept();
            return;
        } else {
            // Clicked elsewhere - clear selection
            if (has_selection_) {
                clear_selection();
            }
        }
    }

    QWidget::mousePressEvent(event);
}


void PDFViewer::on_annotation_text_finished(const QString &text)
{
    if (!text.trimmed().isEmpty() && last_click_target_.page_num > 0) {

        QSize size = calculate_text_size(text, annotation_font_);

        Annotation annotation(text.toStdString(), last_click_target_.page_num,
                            last_click_target_.points_x, last_click_target_.points_y,
                            static_cast<float>(size.width()), static_cast<float>(size.height()),  // Back to pixels
                            annotation_font_);

        // In on_annotation_text_finished(), replace the existing debug with:
       // QFont font(annotation_font_.family, static_cast<int>(annotation_font_.size));
        //QFontMetrics fm(font);
        //int fm_width = fm.horizontalAdvance(text);
        //int fm_height = fm.height();
        //int calculated_width = fm_width + 4;
        //int calculated_height = fm_height + 4;

        document_->add_annotation(annotation);
    }



    text_annotation_mode_ = false;
    setCursor(Qt::ArrowCursor);
    emit annotation_mode_changed(false);
}

void PDFViewer::on_annotation_text_cancelled()
{
    text_annotation_mode_ = false;
    setCursor(Qt::ArrowCursor);
    emit annotation_mode_changed(false);
}


PDFViewer::ClickTarget PDFViewer::get_click_target(QMouseEvent *event) const
{
    if (!document_ || page_.is_empty())
        return { 0, 0.0f, 0.0f };

    QPixmap displayed = label_->pixmap();
    if (displayed.isNull())
        return { 0, 0.0f, 0.0f };

    int target_page = current_page();
    int mouse_x = event->pos().x();
    int mouse_y = event->pos().y();
    int display_width = displayed.width();
    int display_height = displayed.height();

    bool double_page = in_double_page_view();
    if (double_page)
        display_width /= 2;
    if (double_page && mouse_x > display_width) {
        target_page++;
        mouse_x -= display_width;
    }

    int pixel_width = double_page ? page_.width() / 2 : page_.width();
    int pixel_height = page_.height();

    float scale_x = float(pixel_width) / display_width;
    float scale_y = float(pixel_height) / display_height;
    int scaled_mouse_x = int(mouse_x * scale_x);
    int scaled_mouse_y = int(mouse_y * scale_y);

    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(target_page);
    auto [points_x, points_y] = pixels_to_pdf_points(scaled_mouse_x, scaled_mouse_y,
                                                     pixel_width, pixel_height,
                                                     pdf_width_points, pdf_height_points);

    return { target_page, points_x, points_y };
}



QRect PDFViewer::calculate_annotation_bounding_box(const Annotation &annotation) const
{
    int current_page_num = current_page();
    if (annotation.page_num_ != current_page_num) {
        return QRect(); // Empty rect for annotations not on current page
    }

    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(current_page_num);

    // Calculate position from PDF coordinates
    float pdf_y_from_top = pdf_height_points - annotation.y_;
    float pixel_x = (annotation.x_ / pdf_width_points) * page_.pixmap.width();
    float pixel_y = (pdf_y_from_top / pdf_height_points) * page_.pixmap.height();
    pixel_x -= 5;
    pixel_y -= 6;

    // Calculate bounding box size from font and text
    QSize box_size = calculate_text_size(QString::fromStdString(annotation.text_), annotation.font_info_);

    // Apply DPI scaling to match MuPDF rendering
    float dpi_scale_factor = document_->dpi() / 96.0f; // 96 is typical Windows screen DPI
    int scaled_width = int(box_size.width() * dpi_scale_factor);
    int scaled_height = int(box_size.height() * dpi_scale_factor);

    // Calculate display scaling
    QPixmap scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    float display_scale_x = float(scaled_pixmap.width()) / float(page_.pixmap.width());
    float display_scale_y = float(scaled_pixmap.height()) / float(page_.pixmap.height());

    // Apply display scaling to both position and size
    int screen_x = int(pixel_x * display_scale_x);
    int screen_y = int(pixel_y * display_scale_y);
    int screen_w = int(scaled_width * display_scale_x);
    int screen_h = int(scaled_height * display_scale_y);

    return QRect(screen_x, screen_y, screen_w, screen_h);
}

AnnotationHandle PDFViewer::find_annotation_at_point(QMouseEvent *event) const
{
    if (!document_) return AnnotationHandle();

    QPoint click_point = event->pos();

    // Check all annotations on current page
    for (const auto &annotation : document_->annotations()) {
        QRect bounding_box = calculate_annotation_bounding_box(annotation);
        if (!bounding_box.isEmpty() && bounding_box.contains(click_point)) {
            return annotation.handle_;
        }
    }

    return AnnotationHandle(); // No annotation found
}


void PDFViewer::select_annotation(const AnnotationHandle &handle)
{
    selected_annotation_ = handle;
    has_selection_ = true;
    update_image(); // Refresh to show selection
}

void PDFViewer::clear_selection()
{
    selected_annotation_.clear();
    has_selection_ = false;
    update_image(); // Refresh to hide selection
}


void PDFViewer::force_redraw()
{
    if (label_)
        label_->repaint();
}