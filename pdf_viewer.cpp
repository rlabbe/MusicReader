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

#ifdef Q_OS_WIN
#include <Windows.h>
#endif


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
    TRACE_CALL;
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

void PDFViewer::mark_dirty()
{
    TRACE_CALL;
    dirty_ = true;
}

void PDFViewer::refresh_if_dirty()
{
    SAFE_METHOD;
    TRACE_CALL;

    if (dirty_) {
        dirty_ = false;
        refresh();
    }
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

    PrefetchEntry entry = is_double ? make_double_page_entry(current_idx, PageRequestType::CurrentDisplay)
                                    : make_single_page_entry(current_idx, PageRequestType::CurrentDisplay);

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
    auto key = event->key();

    logger::debug("keyPressEvent: key={}, has_selection_={}", key, has_selection_);

    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (has_selection_) {
            bool removed = document_->remove_annotation(selected_annotation_);
            if (removed)
                clear_selection();
            event->accept();
            return;
        }
    }

    if (key == Qt::Key_Escape && has_selection_) {
        // Clear selection on Escape
        clear_selection();
        event->accept();
        return;
    }

    if (key == Qt::Key_Escape && text_annotation_mode_) {
        // Exit annotation mode on Escape
        text_annotation_mode_ = false;
        setCursor(Qt::ArrowCursor);
        emit annotation_mode_changed(false);
        event->accept();
        return;
    }

    switch (key) {
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

    label_->setAlignment(page_vertical_alignment() | page_alignment());
    label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label_->setMinimumSize(1, 1); // Prevent weird shrinking issues
    label_->setScaledContents(false);
    label_->setContentsMargins(0, 0, 0, 0);

    layout_->addWidget(label_, 1); // Stretch document display
    layout_->addWidget(scrollbar_);

    annotation_editor_ = new InPlaceAnnotationEditor(config_, this);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_finished, this,
            &PDFViewer::on_annotation_text_finished);
    connect(annotation_editor_, &InPlaceAnnotationEditor::editing_cancelled, this,
            &PDFViewer::on_annotation_text_cancelled);
    connect(annotation_editor_, &InPlaceAnnotationEditor::text_changed_for_preview, this,
            &PDFViewer::on_annotation_text_changed);
    connect(annotation_editor_, &InPlaceAnnotationEditor::escape_pressed, this, [this]() {
        text_annotation_mode_ = false;
        setCursor(Qt::ArrowCursor);
        emit annotation_mode_changed(false);
    });

    setLayout(layout_);
    update_scrollbar_visibility();

    // Use WidgetWithChildren context so Delete only fires when PDFViewer or its children have focus
    QShortcut* delete_shortcut = new QShortcut(QKeySequence::Delete, this);
    delete_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(delete_shortcut, &QShortcut::activated, this, &PDFViewer::delete_shortcut);
}


void PDFViewer::delete_shortcut()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (has_selection_ && document_) {
        logger::debug("Deleting annotation with handle {}", static_cast<int>(selected_annotation_));
        if (document_->remove_annotation(selected_annotation_))
            clear_selection();
        else
            logger::error("Failed to remove annotation with handle {}", static_cast<int>(selected_annotation_));
    } else {
        logger::debug("delete_shortcut called but no selection (has_selection_={})", has_selection_);
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
    TRACE_CALL;
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
    TRACE_CALL;
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


PDFViewer::PrefetchEntry PDFViewer::make_double_page_entry(int index, PageRequestType request_type) const
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("index={}", index);

    if (!config_ || !document_)
        return PrefetchEntry(index, false, 0);

    PrefetchEntry entry(index, true, config_->border_margin());

    // Get page at this index from renderer
    Page page1 = renderer_.get_page_at_index(index, request_type);
    entry.p1 = PixmapPage(page1);

    // Get page at next index if it exists
    int count = renderer_.page_count();
    if (index + 1 <= count) {
        Page page2 = renderer_.get_page_at_index(index + 1, request_type);
        entry.p2 = PixmapPage(page2);
    } else
        copy_blank_image(entry.p1, entry.p2);

    if (!entry.p1.is_empty() && !entry.p2.is_empty())
        entry.rendered = compose_double_page(entry.p1, entry.p2);

    return entry;
}


PDFViewer::PrefetchEntry PDFViewer::make_single_page_entry(int index, PageRequestType request_type) const
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("index={}", index);

    if (!config_ || !document_)
        return PrefetchEntry(index, false, 0);

    PrefetchEntry entry(index, false, config_->border_margin());

    // Get page at this index from renderer
    Page page = renderer_.get_page_at_index(index, request_type);
    entry.p1 = page;

    if (!entry.p1.is_empty()) {
        if (config_->zoom_to_content())
            entry.rendered = entry.p1.resize_by_border(config_->border_margin());
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
        entry = is_double ? make_double_page_entry(index, PageRequestType::CurrentDisplay)
                          : make_single_page_entry(index, PageRequestType::CurrentDisplay);

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
    TRACE_CALL_MSG("page_index={}", page_index);

    if (closing_)
        return;

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
        PrefetchEntry entry = make_single_page_entry(current_idx, PageRequestType::CurrentDisplay);
        page_ = PixmapPage(entry.rendered, current_pos.physical_page, false);
        update_image();
    } else {
        PrefetchEntry entry = make_double_page_entry(current_idx, PageRequestType::CurrentDisplay);
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

    if (page_.pixmap.isNull()) {
        label_->setText(message.isEmpty() ? "Loading..." : message);
        label_->setAlignment(Qt::AlignCenter);
        label_->setStyleSheet("background-color: white; color: black; font-size: 16pt;");
        return;
    } else {
        label_->setStyleSheet("");
        label_->setAlignment(page_vertical_alignment() | page_alignment());
    }

    REQUIRES(document_);
    QSize max_size;
    if (config_->allow_oversize())
        max_size = label_->size();
    else
        max_size = page_.pixmap.size().boundedTo(label_->size());

    // Use preview image if available (during annotation editing), otherwise use normal page
    QPixmap scaled_pixmap;
    if (preview_page_image_.has_value()) {
        QPixmap preview_pixmap = QPixmap::fromImage(preview_page_image_.value());
        scaled_pixmap = preview_pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Draw crosshair at last click position (for debugging annotation placement)
    if (ConfigFile::instance().debug_annotations() && !last_click_display_pos_.isNull()) {
        QPainter painter(&scaled_pixmap);
        painter.setPen(QPen(Qt::black, 1, Qt::DotLine));

        // Draw horizontal line
        painter.drawLine(0, last_click_display_pos_.y(), scaled_pixmap.width(), last_click_display_pos_.y());

        // Draw vertical line
        painter.drawLine(last_click_display_pos_.x(), 0, last_click_display_pos_.x(), scaled_pixmap.height());

        painter.end();
    }

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
    // QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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
    TRACE_CALL;
    REQUIRES_RET(config_, Qt::AlignmentFlag::AlignLeft);

    switch (config_->page_location()) {
        case PageLocation::Left: return Qt::AlignmentFlag::AlignLeft;
        default: return Qt::AlignmentFlag::AlignHCenter;
    };
}


Qt::AlignmentFlag PDFViewer::page_vertical_alignment() const
{
    SAFE_METHOD;
    TRACE_CALL;
    REQUIRES_RET(config_, Qt::AlignmentFlag::AlignTop);

    switch (config_->page_vertical_location()) {
        case PageVerticalLocation::Bottom: return Qt::AlignmentFlag::AlignBottom;
        default: return Qt::AlignmentFlag::AlignTop;
    };
}


bool PDFViewer::PrefetchEntry::valid(int target_index, ConfigFile& config) const
{
    SAFE_METHOD;
    TRACE_CALL;

    return index == target_index && double_page == (config.page_view_count() == 2) &&
           border_margin == config.border_margin() && !rendered.isNull();
}


void PDFViewer::set_text_annotation_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_CALL;
    text_annotation_mode_ = enabled;
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void PDFViewer::set_page_break_edit_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_CALL;
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

        // Calculate pixmap coordinates for crosshair (accounting for alignment offset)
        QSize label_size = label_->size();
        int offset_x = 0;
        int offset_y = 0;
        Qt::Alignment h_align = page_alignment();
        Qt::Alignment v_align = page_vertical_alignment();
        if (h_align == Qt::AlignHCenter)
            offset_x = (label_size.width() - displayed.width()) / 2;
        else if (h_align == Qt::AlignRight)
            offset_x = label_size.width() - displayed.width();
        if (v_align == Qt::AlignVCenter)
            offset_y = (label_size.height() - displayed.height()) / 2;
        else if (v_align == Qt::AlignBottom)
            offset_y = label_size.height() - displayed.height();

        // IBeam cursor hotspot is at center, but user clicks with bottom of cursor on the line.
        // Adjust click position by half the cursor height so baseline lands where bottom of IBeam was.
        int cursor_offset = 0;
#ifdef Q_OS_WIN
        HCURSOR hCursor = LoadCursor(nullptr, IDC_IBEAM);
        if (hCursor) {
            ICONINFO iconInfo;
            if (GetIconInfo(hCursor, &iconInfo)) {
                // yHotspot is distance from top to hotspot (center for IBeam)
                // We want distance from hotspot to bottom = height - yHotspot
                BITMAP bm;
                if (GetObject(iconInfo.hbmMask, sizeof(bm), &bm))
                    cursor_offset = bm.bmHeight - static_cast<int>(iconInfo.yHotspot);
                if (iconInfo.hbmMask)
                    DeleteObject(iconInfo.hbmMask);
                if (iconInfo.hbmColor)
                    DeleteObject(iconInfo.hbmColor);
            }
        }
#endif

        QPoint adjusted_pos(event->pos().x(), event->pos().y() + cursor_offset);
        last_click_display_pos_ = QPoint(adjusted_pos.x() - offset_x, adjusted_pos.y() - offset_y);
        last_click_target_ = get_click_target(event);
        // Adjust the stored target Y as well
        if (cursor_offset > 0) {
            auto [pdf_width_pts, pdf_height_pts] = document_->get_page_dimensions_points(last_click_target_.page_num);
            float pixels_to_points_ratio = pdf_width_pts / static_cast<float>(displayed.width());
            last_click_target_.points_y -= cursor_offset * pixels_to_points_ratio;
        }

        auto [pdf_width, _] = document_->get_page_dimensions_points(last_click_target_.page_num);
        float points_to_pixels = static_cast<float>(displayed.width()) / pdf_width;
        annotation_editor_->set_dpi_scale(points_to_pixels);

        annotation_editor_->start_editing(adjusted_pos);

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
            select_annotation(clicked_annotation);
            setFocus(); // Ensure PDFViewer has focus so Delete shortcut works (after update_image)
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

        const FontInfo& font_info = config_->annotation_font();

        // Use MuPDF's own font metrics for accurate sizing
        float width_points = mupdf_measure_text_width(font_info.family, font_info.size, text.toStdString());
        float height_points = mupdf_measure_text_height(font_info.family, font_info.size);

        Annotation annotation(text.toStdString(), last_click_target_.page_num, last_click_target_.points_x,
                              last_click_target_.points_y, width_points, height_points, font_info);
        document_->add_annotation(annotation);

        // Save text for debug rendering
        last_annotation_text_ = text;
    }

    preview_page_image_.reset();
    annotation_editor_->set_preview_mode(false);
    setFocus(); // Return focus so we can receive Escape key
    // Stay in annotation mode - user can click again to add more annotations
}


void PDFViewer::on_annotation_text_cancelled()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    preview_page_image_.reset();
    annotation_editor_->set_preview_mode(false);
    text_annotation_mode_ = false;
    setCursor(Qt::ArrowCursor);
    emit annotation_mode_changed(false);
}


void PDFViewer::on_annotation_text_changed(const QString& text)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document_ || last_click_target_.page_num <= 0)
        return;

    // Build a preview annotation with current text
    const FontInfo& font_info = config_->annotation_font();

    // Use MuPDF metrics for accurate sizing
    std::string measure_text = text.isEmpty() ? "M" : text.toStdString();
    float width_points = mupdf_measure_text_width(font_info.family, font_info.size, measure_text);
    float height_points = mupdf_measure_text_height(font_info.family, font_info.size);

    // Pass raw click coordinates as baseline - render_page_with_preview_annotation
    // will handle positioning the rect so baseline lands at click point
    Annotation preview_annotation(text.toStdString(), last_click_target_.page_num, last_click_target_.points_x,
                                  last_click_target_.points_y, // raw baseline in PDF coords
                                  width_points, height_points, font_info);

    // Render page with preview annotation
    QImage preview_image =
        document_->render_page_with_preview_annotation(last_click_target_.page_num, preview_annotation);

    if (!preview_image.isNull()) {
        preview_page_image_ = preview_image;
        annotation_editor_->set_preview_mode(true);
        update_image();
    }
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

    // Get mouse position relative to the label widget
    int target_page = current_page();
    QPoint mouse_pos = event->pos();

    // The pixmap is aligned within the label, so calculate the actual pixmap position
    QSize label_size = label_->size();
    int pixmap_width = displayed.width();
    int pixmap_height = displayed.height();

    // Calculate offset due to alignment (centered or left/top aligned)
    int offset_x = 0;
    int offset_y = 0;

    Qt::Alignment h_align = page_alignment();
    Qt::Alignment v_align = page_vertical_alignment();

    if (h_align == Qt::AlignHCenter)
        offset_x = (label_size.width() - pixmap_width) / 2;
    else if (h_align == Qt::AlignRight)
        offset_x = label_size.width() - pixmap_width;

    if (v_align == Qt::AlignVCenter)
        offset_y = (label_size.height() - pixmap_height) / 2;
    else if (v_align == Qt::AlignBottom)
        offset_y = label_size.height() - pixmap_height;

    // Adjust mouse position to be relative to the pixmap, not the label
    int mouse_x = mouse_pos.x() - offset_x;
    int mouse_y = mouse_pos.y() - offset_y;

    // Use pixmap dimensions for calculations
    int display_width = pixmap_width;
    int display_height = pixmap_height;

    bool double_page = in_double_page_view();
    if (double_page)
        display_width /= 2;
    if (double_page && mouse_x > display_width) {
        target_page++;
        mouse_x -= display_width;
    }

    // Get the full page to check for border/cropping
    Page full_page = document_->get_page(target_page, false);
    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(target_page);

    // Calculate click ratio in display space
    float click_ratio_x = float(mouse_x) / display_width;
    float click_ratio_y = float(mouse_y) / display_height;

    float points_x, points_y;

    float full_width = full_page.width();
    float full_height = full_page.height();

    bool zoom_mode = config_ && config_->zoom_to_content();

    if (zoom_mode) {
        // When zoomed, the displayed image is cropped to border
        float border_left = full_page.border.left;
        float border_top = full_page.border.top;
        float border_width = full_page.border.right - full_page.border.left;
        float border_height = full_page.border.bottom - full_page.border.top;

        // Click position within the cropped area (in full page pixels at render DPI)
        float cropped_x = click_ratio_x * border_width;
        float cropped_y = click_ratio_y * border_height;

        // Convert to full page pixels (at render DPI)
        float full_x = border_left + cropped_x;
        float full_y = border_top + cropped_y;

        // Convert ratio in full page to ratio in PDF points
        points_x = (full_x / full_width) * pdf_width_points;
        points_y = pdf_height_points - (full_y / full_height) * pdf_height_points;
    } else {
        // When not zoomed, direct mapping
        points_x = click_ratio_x * pdf_width_points;
        points_y = pdf_height_points - (click_ratio_y * pdf_height_points);
    }

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

    // Get the displayed pixmap (already scaled)
    QPixmap displayed = label_->pixmap();
    if (displayed.isNull())
        return QRect();

    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(current_page_num);

    // annotation.y_ is the BASELINE in PDF coords (y from bottom)
    // Use MuPDF font metrics for accurate positioning (recalculate from text, don't use stored values)
    float ascent = mupdf_font_ascent(annotation.font_info_.family, annotation.font_info_.size);
    float descent = mupdf_font_descent(annotation.font_info_.family, annotation.font_info_.size);
    float text_width = mupdf_measure_text_width(annotation.font_info_.family, annotation.font_info_.size, annotation.text_);
    float annot_top_pdf = annotation.y_ + ascent;
    float annot_bottom_pdf = annotation.y_ - descent;

    float display_x, display_y, display_w, display_h;

    bool zoom_mode = config_ && config_->zoom_to_content();
    if (zoom_mode) {
        // Get full page to access border info for zoom mode
        Page full_page = document_->get_page(current_page_num, false);
        float full_width = full_page.width();
        float full_height = full_page.height();

        // Convert PDF points to full page pixels (at render DPI)
        float annot_left_full = (annotation.x_ / pdf_width_points) * full_width;
        float annot_top_full = ((pdf_height_points - annot_top_pdf) / pdf_height_points) * full_height;
        float annot_right_full = ((annotation.x_ + text_width) / pdf_width_points) * full_width;
        float annot_bottom_full = ((pdf_height_points - annot_bottom_pdf) / pdf_height_points) * full_height;

        // When zoomed, displayed image is cropped to border
        float border_left = full_page.border.left;
        float border_top = full_page.border.top;
        float border_width = full_page.border.right - full_page.border.left;
        float border_height = full_page.border.bottom - full_page.border.top;

        // Convert from full page coords to cropped/border coords
        float cropped_left = annot_left_full - border_left;
        float cropped_top = annot_top_full - border_top;
        float cropped_right = annot_right_full - border_left;
        float cropped_bottom = annot_bottom_full - border_top;

        // Convert to display coords (ratio within cropped area * display size)
        display_x = (cropped_left / border_width) * displayed.width();
        display_y = (cropped_top / border_height) * displayed.height();
        display_w = ((cropped_right - cropped_left) / border_width) * displayed.width();
        display_h = ((cropped_bottom - cropped_top) / border_height) * displayed.height();
    } else {
        // No zoom - page_.pixmap is the full page, displayed is scaled from it
        // Convert PDF points directly to display coordinates
        float annot_left_ratio = annotation.x_ / pdf_width_points;
        float annot_top_ratio = (pdf_height_points - annot_top_pdf) / pdf_height_points;
        float annot_right_ratio = (annotation.x_ + text_width) / pdf_width_points;
        float annot_bottom_ratio = (pdf_height_points - annot_bottom_pdf) / pdf_height_points;

        display_x = annot_left_ratio * displayed.width();
        display_y = annot_top_ratio * displayed.height();
        display_w = (annot_right_ratio - annot_left_ratio) * displayed.width();
        display_h = (annot_bottom_ratio - annot_top_ratio) * displayed.height();
    }

    return QRect(int(display_x), int(display_y), int(display_w), int(display_h));
}


AnnotationHandle PDFViewer::find_annotation_at_point(QMouseEvent* event) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!document_)
        return AnnotationHandle();

    // Use get_click_target to convert click to PDF points (handles zoom, alignment, etc.)
    ClickTarget click = get_click_target(event);
    if (click.page_num <= 0)
        return AnnotationHandle();

    // Check all annotations on the clicked page
    for (const auto& annotation : document_->annotations()) {
        if (annotation.page_num_ != click.page_num)
            continue;

        // Annotation bounds in PDF points
        // annotation.y_ stores the BASELINE (not top edge)
        // Use MuPDF metrics for accurate hit testing (recalculate from text)
        float ascent = mupdf_font_ascent(annotation.font_info_.family, annotation.font_info_.size);
        float descent = mupdf_font_descent(annotation.font_info_.family, annotation.font_info_.size);
        float text_width = mupdf_measure_text_width(annotation.font_info_.family, annotation.font_info_.size, annotation.text_);
        float annot_left = annotation.x_;
        float annot_top = annotation.y_ + ascent;
        float annot_right = annotation.x_ + text_width;
        float annot_bottom = annotation.y_ - descent;

        if (click.points_x >= annot_left && click.points_x <= annot_right && click.points_y >= annot_bottom &&
            click.points_y <= annot_top) {
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
