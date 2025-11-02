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
#include "music_reader.h"
#include "bookmark_panel.h"


PDFViewer::PDFViewer(std::shared_ptr<Document> document, ConfigFile* config, int page, StatusBar* sbar, QWidget* parent,
                     MusicReader* reader, BookmarkPanel* panel)
    : QWidget(parent)
    , document_(document)
    , status_bar_(sbar)
    , config_(config)
    , drawing_margin_(false)
    , bookmark_panel_(panel)
    , renderer_(document.get())
{
    setFocusPolicy(Qt::StrongFocus);
    init_ui(page);

    connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);

    // Convert physical page number to index and navigate
    renderer_.goto_physical_page(page);
    get_page(renderer_.current_index());

    connect(this, &PDFViewer::annotation_mode_changed, reader, &MusicReader::on_annotation_mode_changed);
}


PDFViewer::~PDFViewer()
{
    REQUIRES(document_);
    document_->save();
}


void PDFViewer::update_status_bar()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);
    REQUIRES(status_bar_);

    if (!status_bar_)
        return;
    if (!isVisible())
        return;
}


bool PDFViewer::in_single_page_view() const
{
    SAFE_METHOD;
    REQUIRES_RET(config_, true);

    return page_break_edit_mode_ || config_->page_view_count() == 1 || page_count() == 1;
}


void PDFViewer::refresh()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);

    const int count = renderer_.page_count();
    if (count == 0)
        return;

    int current_idx = renderer_.current_index();

    const bool is_double = in_double_page_view();

    PrefetchEntry entry = is_double ? make_double_page_entry(current_idx) : make_single_page_entry(current_idx);

    int physical_page = entry.p1.page_num;

    page_ = PixmapPage(entry.rendered, physical_page, is_double);

    update_image();

    // Prefetch next and previous indices
    if (current_idx + 1 <= count)
        prefetch_async(current_idx + 1);

    if (current_idx - 1 >= 1)
        prefetch_async(current_idx - 1);

    bookmark_panel_->select_page(physical_page);

    manual_scrollbar_change_ = true;
    scrollbar_->setMaximum(count);
    scrollbar_->setValue(current_idx);
    manual_scrollbar_change_ = false;

    update_scrollbar_visibility();
    update_status_bar();

    emit page_changed(current_idx);
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

    int count = renderer_.page_count();
    int current = renderer_.current_index();
    int new_index = qBound(1, current + step, count);

    // don't go to last page if even number of pages
    if (new_index == count && in_double_page_view() && count % 2 == 0)
        new_index = count - 1;

    manual_scrollbar_change_ = true;
    scrollbar_->setValue(new_index);
    manual_scrollbar_change_ = false;

    document_->prioritize();
    get_page(new_index);
}


void PDFViewer::replace_document(std::shared_ptr<Document> document, int page)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document || document == document_)
        return;
    document_ = document;
    renderer_.replace_document(document_.get());
    connect(document_.get(), &Document::page_loaded, this, &PDFViewer::on_page_loaded);
    get_page(page);
}


void PDFViewer::keyPressEvent(QKeyEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Handle selection-related keys first
    if (event->key() == Qt::Key_Delete && has_selection_) {
        if (document_->remove_annotation(selected_annotation_))
            clear_selection();
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
            event->accept();
            break;
        default: QWidget::keyPressEvent(event);
    }
}


void PDFViewer::wheelEvent(QWheelEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->angleDelta().y() > 0)
        page_up();
    else
        page_down();
}


bool PDFViewer::event(QEvent* event)
{
    SAFE_METHOD;

    if (event->type() == QEvent::Gesture) {
        TRACE_FUNCTION;

        auto* gesture = dynamic_cast<QSwipeGesture*>(static_cast<QGestureEvent*>(event)->gesture(Qt::SwipeGesture));
        if (gesture->horizontalDirection() == QSwipeGesture::Left)
            page_down();
        else
            page_up();
        return true;
    }
    return QWidget::event(event);
}


void PDFViewer::resizeEvent(QResizeEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QWidget::resizeEvent(event);
    if (!skip_resize_update_)
        update_image();
}


void PDFViewer::init_ui(int page)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Initialize renderer to requested page
    renderer_.goto_physical_page(page);

    layout_ = new QHBoxLayout(this);

    // Remove extra spacing/margins
    layout_->setContentsMargins(0, 4, 0, 4);
    layout_->setSpacing(0);

    scrollbar_ = new QScrollBar(Qt::Vertical, this);
    scrollbar_->setMinimum(1);
    scrollbar_->setMaximum(page_count());
    manual_scrollbar_change_ = true;
    scrollbar_->setValue(renderer_.current_index());
    manual_scrollbar_change_ = false;

    connect(scrollbar_, &QScrollBar::valueChanged, this, &PDFViewer::on_scrollbar_value_changed);

    label_ = new QLabel(this);
    label_->setStyleSheet("border: 0px;");

    label_->setAlignment(Qt::AlignTop | page_alignment());
    label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label_->setMinimumSize(1, 1); // Prevent weird shrinking issues
    label_->setScaledContents(false);
    label_->setContentsMargins(0, 0, 0, 0);

    layout_->addWidget(label_, 1); // Stretch document display
    layout_->addWidget(scrollbar_);

    annotation_editor_ = new InPlaceAnnotationEditor(annotation_font_, this);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_finished, this,
            &PDFViewer::on_annotation_text_finished);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_cancelled, this,
            &PDFViewer::on_annotation_text_cancelled);

    setLayout(layout_);
    update_scrollbar_visibility();

    QShortcut* delete_shortcut = new QShortcut(QKeySequence::Delete, this);
    delete_shortcut->setContext(Qt::WidgetShortcut); // Only when this widget has focus
    connect(delete_shortcut, &QShortcut::activated, this, &PDFViewer::delete_shortcut);
}


void PDFViewer::delete_shortcut()
{
    if (has_selection_ && document_) {
        if (document_->remove_annotation(selected_annotation_))
            clear_selection();
    }
}


void PDFViewer::on_scrollbar_value_changed(int new_index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (manual_scrollbar_change_)
        return;

    if (new_index != renderer_.current_index())
        get_page(new_index);
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


void PDFViewer::prefetch_async(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES(document_);


    if (!config_->do_async_loads())
        return;

    std::jthread([this, index]() {
        const int count = renderer_.page_count();
        if (index < 1 || index > count)
            return;

        const bool is_double = in_double_page_view();
        int current_idx = renderer_.current_index();

        // Choose correct slot
        PrefetchEntry& slot = (index > current_idx) ? prefetch_.next : prefetch_.prev;
        if (slot.index == index && slot.double_page == is_double)
            return; // Already prefetched, matching mode

        PrefetchEntry entry = is_double ? make_double_page_entry(index) : make_single_page_entry(index);

        if (entry.rendered.isNull())
            return;

        {
            std::lock_guard lock(prefetch_mutex_);
            PrefetchEntry& dest = (entry.index > current_idx) ? prefetch_.next : prefetch_.prev;
            dest = std::move(entry);
            logger::info("Prefetched page index {}", index);
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


PDFViewer::PrefetchEntry PDFViewer::make_double_page_entry(int index) const
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("index={}", index);

    if (!config_ || !document_)
        return PrefetchEntry(index, false, 0);

    PrefetchEntry entry(index, true, config_->border_margin());

    // Get page at this index from renderer
    Page page1 = renderer_.get_page_at_index(index);
    entry.p1 = PixmapPage(page1);

    // Get page at next index if it exists
    int count = renderer_.page_count();
    if (index + 1 <= count) {
        Page page2 = renderer_.get_page_at_index(index + 1);
        entry.p2 = PixmapPage(page2);
    } else
        copy_blank_image(entry.p1, entry.p2);

    if (!entry.p1.is_empty() && !entry.p2.is_empty())
        entry.rendered = compose_double_page(entry.p1, entry.p2);

    return entry;
}


PDFViewer::PrefetchEntry PDFViewer::make_single_page_entry(int index) const
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("index={}", index);

    if (!config_ || !document_)
        return PrefetchEntry(index, false, 0);

    PrefetchEntry entry(index, false, config_->border_margin());

    // Get page at this index from renderer
    Page page = renderer_.get_page_at_index(index);
    entry.p1 = page;

    if (!entry.p1.is_empty()) {
        if (config_->zoom_to_content())
            entry.rendered = Page::as_pixmap(entry.p1.resize_by_border(config_->border_margin()));
        else
            entry.rendered = entry.p1.as_pixmap();
    }
    return entry;
}


void PDFViewer::goto_physical_page(int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("page_num={}", page_num);
    REQUIRES(document_);

    // Map physical page to index and navigate there
    renderer_.goto_physical_page(page_num);
    int index = renderer_.current_index();
    get_page(index);
}


void PDFViewer::get_page(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("index={}", index);
    REQUIRES(document_);

    const int count = renderer_.page_count();
    if (count == 0)
        return;
    if (index < 1 || index > count)
        return;

    // Update renderer position
    renderer_.goto_index(index);

    const bool is_double = in_double_page_view();

    // Check if this index is already in prefetch cache
    PrefetchEntry entry;
    bool found_in_cache = false;

    {
        std::lock_guard lock(prefetch_mutex_);
        if (prefetch_.next.valid(index, *config_)) {
            logger::debug("CACHE HIT next, moving entry");
            entry = std::move(prefetch_.next);
            prefetch_.next.clear();
            found_in_cache = true;
        } else if (prefetch_.prev.valid(index, *config_)) {
            logger::debug("CACHE HIT prev, moving entry");
            entry = std::move(prefetch_.prev);
            prefetch_.prev.clear();
            found_in_cache = true;
        }
    }

    // If not in cache, render it now
    if (!found_in_cache)
        entry = is_double ? make_double_page_entry(index) : make_single_page_entry(index);

    // Get physical page from the entry
    int physical_page = entry.p1.page_num;

    // Avoid expensive pixmap.toImage() conversion - just assign directly
    page_.pixmap = entry.rendered;
    page_.page_num = physical_page;
    page_.double_page = is_double;

    update_image();

    // Prefetch next and previous indices
    if (index + 1 <= count)
        prefetch_async(index + 1);

    if (index - 1 >= 1)
        prefetch_async(index - 1);

    bookmark_panel_->select_page(physical_page);

    manual_scrollbar_change_ = true;
    scrollbar_->setValue(index);
    manual_scrollbar_change_ = false;

    emit page_changed(index);
}


QPixmap PDFViewer::compose_double_page(const PixmapPage& p1, const PixmapPage& p2) const
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
    painter.drawPixmap(p1_crop.width() + line_width, p2_offset, p2.pixmap, p2_crop.x(), p2_crop.y(), p2_crop.width(),
                       p2_crop.height());
    painter.setPen(QPen(Qt::black, line_width));
    painter.drawLine(p1_crop.width() + line_width / 2, 0, p1_crop.width() + line_width / 2, max_height);
    painter.end();

    return combined_image;
}


void PDFViewer::on_page_loaded(std::string name, int page_index)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("page_index={}", page_index);

    if (name != document_->filename())
        return;

    // Map physical page to index for comparison
    int current_idx = renderer_.current_index();
    PageRenderer::Position current_pos = renderer_.index_to_position(current_idx);

    // Check if loaded page is relevant to current display
    bool relevant = false;
    if (in_single_page_view()) {
        relevant = (page_index == current_pos.physical_page);
    } else {
        // In double page mode, check if it's current or next page
        if (page_index == current_pos.physical_page)
            relevant = true;
        else if (current_idx + 1 <= renderer_.page_count()) {
            PageRenderer::Position next_pos = renderer_.index_to_position(current_idx + 1);
            relevant = (page_index == next_pos.physical_page);
        }
    }

    if (!relevant)
        return;

    skip_resize_update_ = true;

    int count = page_count();
    if (in_single_page_view() || count == 1) {
        PrefetchEntry entry = make_single_page_entry(current_idx);
        page_ = PixmapPage(entry.rendered, current_pos.physical_page, false);
        update_image();
    } else {
        PrefetchEntry entry = make_double_page_entry(current_idx);
        page_ = PixmapPage(entry.rendered, current_pos.physical_page, true);
        update_image();
    }

    skip_resize_update_ = false;
}


int PDFViewer::normalized_to_display_y(double normalized_pos, int display_height) const
{
    // Get the FULL uncropped page to get proper dimensions and border
    Page full_page = document_->get_page(current_page(), false);
    int full_height = full_page.height();
    int break_y_full = static_cast<int>(normalized_pos * full_height);

    if (config_->zoom_to_content()) {
        // When zoomed, the displayed image is cropped to border
        int border_top = full_page.border.top;
        int border_height = full_page.border.bottom - full_page.border.top;
        int break_y_cropped = break_y_full - border_top;
        return static_cast<int>((static_cast<float>(break_y_cropped) / border_height) * display_height);
    } else
        // When not zoomed, direct mapping from full image to display
        return static_cast<int>((static_cast<float>(break_y_full) / full_height) * display_height);
}


double PDFViewer::display_y_to_normalized(int display_y, int display_height) const
{
    // Get the FULL uncropped page to get proper dimensions and border
    Page full_page = document_->get_page(current_page(), false);
    int full_height = full_page.height();
    float click_ratio = static_cast<float>(display_y) / display_height;

    if (config_->zoom_to_content()) {
        // When zoomed, the displayed image is cropped to border
        int border_top = full_page.border.top;
        int border_height = full_page.border.bottom - full_page.border.top;
        int cropped_y = static_cast<int>(click_ratio * border_height);
        int full_y = border_top + cropped_y;
        return static_cast<double>(full_y) / full_height;
    } else {
        // When not zoomed, direct mapping from display to full image
        int full_y = static_cast<int>(click_ratio * full_height);
        return static_cast<double>(full_y) / full_height;
    }
}


void PDFViewer::update_image(const QString& message)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("{} {} page:{}", document_->filename(), message.toStdString(), page_.page_num);
    REQUIRES(label_);
    REQUIRES(config_);

    if (page_.is_empty()) {
        logger::info("page is empty");
        label_->setText(message.isEmpty() ? "Loading..." : message);
        label_->setAlignment(Qt::AlignCenter);
        label_->setStyleSheet("background-color: white; color: black; font-size: 16pt;");
        return;
    } else {
        logger::info("page ain't empty");
        label_->setStyleSheet("");
        label_->setAlignment(Qt::AlignTop | page_alignment());
    }

    REQUIRES(document_);
    QSize max_size;
    if (config_->allow_oversize())
        max_size = label_->size();
    else
        max_size = page_.pixmap.size().boundedTo(label_->size());

    QPixmap scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // scaled_pixmap = page_.pixmap; //debug render at size given by mupdf


    // Draw selection box around selected annotation only
    if (has_selection_) {
        QPainter painter(&scaled_pixmap);

        // Find the selected annotation and draw dotted red box
        for (const auto& annotation : document_->annotations()) {
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

    // Draw page break lines in edit mode
    if (page_break_edit_mode_) {
        const auto& breaks = document_->performance_data().get_page_breaks(current_page());
        if (!breaks.empty() || dragging_page_break_) {
            QPainter painter(&scaled_pixmap);
            painter.setPen(QPen(Qt::red, 1, Qt::SolidLine));

            int display_height = scaled_pixmap.height();

            // Draw existing breaks (skip the one being dragged)
            for (double break_pos : breaks) {
                // Skip drawing the break we're currently dragging
                if (dragging_existing_break_ && std::abs(break_pos - original_break_position_) < 0.01)
                    continue;

                int break_y_display = normalized_to_display_y(break_pos, display_height);

                // Only draw if within visible area
                if (break_y_display >= 0 && break_y_display < display_height)
                    painter.drawLine(0, break_y_display, scaled_pixmap.width(), break_y_display);
            }

            // Draw the line being dragged (if any)
            if (dragging_page_break_) {
                int break_y_display = normalized_to_display_y(dragging_break_position_, display_height);

                if (break_y_display >= 0 && break_y_display < display_height)
                    painter.drawLine(0, break_y_display, scaled_pixmap.width(), break_y_display);
            }

            painter.end();
        }
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

    if (page_.is_empty())
        return;

    static bool first_time = true;
    if (!first_time)
        return;
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
        default: return Qt::AlignmentFlag::AlignHCenter;
    };
}


bool PDFViewer::PrefetchEntry::valid(int target_index, ConfigFile& config) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    return index == target_index && double_page == (config.page_view_count() == 2) &&
           border_margin == config.border_margin() && !rendered.isNull();
}


void PDFViewer::set_text_annotation_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    text_annotation_mode_ = enabled;
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
}


void PDFViewer::set_page_break_edit_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    page_break_edit_mode_ = enabled;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    refresh();
    emit page_break_edit_mode_changed(enabled);
}


void PDFViewer::mousePressEvent(QMouseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    if (!document_)
        return;
    if (page_.is_empty())
        return;

    setFocus();

    if (event->button() == Qt::LeftButton && text_annotation_mode_) {
        QPixmap displayed = label_->pixmap();
        last_click_target_ = get_click_target(event);

        auto [pdf_width, pdf_height] = document_->get_page_dimensions_points(last_click_target_.page_num);

        // Position editor directly at click point (screen coordinates)
        annotation_editor_->start_editing(event->pos());

        event->accept();
        return;
    } else if (event->button() == Qt::LeftButton && page_break_edit_mode_) {
        // Handle page break editing - left button to add/move
        int click_y = event->pos().y();
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int display_height = displayed.height();
        double normalized_pos = display_y_to_normalized(click_y, display_height);

        // Check if clicking near an existing break (within 5 pixels)
        const auto& breaks = document_->performance_data().get_page_breaks(current_page());
        double clicked_break = -1.0;
        for (double break_pos : breaks) {
            int break_y_display = normalized_to_display_y(break_pos, display_height);

            if (std::abs(click_y - break_y_display) <= 5) {
                clicked_break = break_pos;
                break;
            }
        }

        if (clicked_break >= 0.0) {
            // Clicked on existing break - start dragging it
            dragging_page_break_ = true;
            dragging_existing_break_ = true;
            dragging_break_position_ = clicked_break;
            original_break_position_ = clicked_break;
        } else {
            // Clicked in empty space - start creating new break
            dragging_page_break_ = true;
            dragging_existing_break_ = false;
            dragging_break_position_ = normalized_pos;
        }

        update_image();
        event->accept();
        return;
    } else if (event->button() == Qt::RightButton && page_break_edit_mode_) {
        // Handle page break deletion - right button
        int click_y = event->pos().y();
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int display_height = displayed.height();

        // Find and delete the break
        const auto& breaks = document_->performance_data().get_page_breaks(current_page());
        for (double break_pos : breaks) {
            int break_y_display = normalized_to_display_y(break_pos, display_height);

            if (std::abs(click_y - break_y_display) <= 5) {
                document_->performance_data().remove_page_break(current_page(), break_pos);
                document_->performance_data().save(document_->path());
                update_image();
                break;
            }
        }

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
            if (has_selection_)
                clear_selection();
        }
    }

    QWidget::mousePressEvent(event);
}


void PDFViewer::mouseMoveEvent(QMouseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (dragging_page_break_) {
        int click_y = event->pos().y();
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int display_height = displayed.height();
        double normalized_pos = display_y_to_normalized(click_y, display_height);

        // Clamp to valid range
        normalized_pos = std::max(0.0, std::min(1.0, normalized_pos));

        dragging_break_position_ = normalized_pos;
        update_image();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}


void PDFViewer::mouseReleaseEvent(QMouseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->button() == Qt::LeftButton && dragging_page_break_) {
        // Finalize the break
        if (dragging_existing_break_) {
            // Moving existing break - remove from original position
            document_->performance_data().remove_page_break(current_page(), original_break_position_);
        }

        // Add at new position
        document_->performance_data().add_page_break(current_page(), dragging_break_position_);
        document_->performance_data().save(document_->path());

        dragging_page_break_ = false;
        dragging_existing_break_ = false;
        update_image();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PDFViewer::on_annotation_text_finished(const QString& text)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!text.trimmed().isEmpty() && last_click_target_.page_num > 0) {

        QSize size = calculate_text_size(text, annotation_font_);

        Annotation annotation(text.toStdString(), last_click_target_.page_num, last_click_target_.points_x,
                              last_click_target_.points_y, static_cast<float>(size.width()),
                              static_cast<float>(size.height()), // Back to pixels
                              annotation_font_);

        // In on_annotation_text_finished(), replace the existing debug with:
        // QFont font(annotation_font_.family, static_cast<int>(annotation_font_.size));
        // QFontMetrics fm(font);
        // int fm_width = fm.horizontalAdvance(text);
        // int fm_height = fm.height();
        // int calculated_width = fm_width + 4;
        // int calculated_height = fm_height + 4;

        document_->add_annotation(annotation);
    }

    text_annotation_mode_ = false;
    setCursor(Qt::ArrowCursor);
    emit annotation_mode_changed(false);
}


void PDFViewer::on_annotation_text_cancelled()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    text_annotation_mode_ = false;
    setCursor(Qt::ArrowCursor);
    emit annotation_mode_changed(false);
}


PDFViewer::ClickTarget PDFViewer::get_click_target(QMouseEvent* event) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document_ || page_.is_empty())
        return {0, 0.0f, 0.0f};

    QPixmap displayed = label_->pixmap();
    if (displayed.isNull())
        return {0, 0.0f, 0.0f};

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
    auto [points_x, points_y] = pixels_to_pdf_points(scaled_mouse_x, scaled_mouse_y, pixel_width, pixel_height,
                                                     pdf_width_points, pdf_height_points);

    return {target_page, points_x, points_y};
}


QRect PDFViewer::calculate_annotation_bounding_box(const Annotation& annotation) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

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


AnnotationHandle PDFViewer::find_annotation_at_point(QMouseEvent* event) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document_)
        return AnnotationHandle();

    QPoint click_point = event->pos();

    // Check all annotations on current page
    for (const auto& annotation : document_->annotations()) {
        QRect bounding_box = calculate_annotation_bounding_box(annotation);
        if (!bounding_box.isEmpty() && bounding_box.contains(click_point)) {
            return annotation.handle_;
        }
    }

    return AnnotationHandle(); // No annotation found
}


void PDFViewer::select_annotation(const AnnotationHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    selected_annotation_ = handle;
    has_selection_ = true;
    update_image(); // Refresh to show selection
}


void PDFViewer::clear_selection()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    selected_annotation_.clear();
    has_selection_ = false;
    update_image(); // Refresh to hide selection
}


void PDFViewer::force_redraw()
{
    if (label_)
        label_->repaint();
}


void PDFViewer::set_performance_mode(PerformanceMode::Mode mode)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (PerformanceMode::get() == mode)
        return;

    int current_physical = page_.page_num;
    PerformanceMode::set(mode);
    clear_prefetch();
    goto_physical_page(current_physical);
}


PerformanceMode::Mode PDFViewer::performance_mode() const
{
    SAFE_METHOD;
    return PerformanceMode::get();
}
