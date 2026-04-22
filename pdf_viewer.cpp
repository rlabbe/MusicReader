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


PDFViewer::PDFViewer(std::shared_ptr<Document> document,
                     ConfigFile* config,
                     int page,
                     StatusBar* sbar,
                     QWidget* parent,
                     MusicReader* reader,
                     BookmarkPanel* panel)
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
    flush_pending_annotation_move();
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

    // Finish any in-progress annotation editing when mode changes
    if (annotation_editor_->isVisible())
        annotation_editor_->clearFocus();

    // Clear any annotation selection when mode changes (zoom, page view, performance mode)
    clear_selection();

    // PrefetchEntry::valid() does not track every piece of state that triggers
    // a refresh, so drop cached pixmaps to force a re-render on next navigation.
    clear_prefetch();

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

    logger::debug("keyPressEvent: key={}, selected_annotation_={}", key, static_cast<int>(selected_annotation_));

    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (selected_annotation_) {
            bool removed = document_->remove_annotation(selected_annotation_);
            if (removed)
                clear_selection();
            event->accept();
            return;
        }
    }

    if (key == Qt::Key_Escape && selected_annotation_) {
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

    // 'x' inside page break edit mode toggles the paper-crop submode (top/bottom).
    if (page_break_edit_mode_ && key == Qt::Key_X) {
        set_page_break_sub_mode(page_break_sub_mode_ == PageBreakSubMode::EditPaperCrop
                                    ? PageBreakSubMode::EditBreaks
                                    : PageBreakSubMode::EditPaperCrop);
        event->accept();
        return;
    }

    // 'z' inside page break edit mode toggles the paper-crop submode (left/right).
    if (page_break_edit_mode_ && key == Qt::Key_Z) {
        set_page_break_sub_mode(page_break_sub_mode_ == PageBreakSubMode::EditPaperCropLR
                                    ? PageBreakSubMode::EditBreaks
                                    : PageBreakSubMode::EditPaperCropLR);
        event->accept();
        return;
    }

    // Arrow keys move selected annotation, otherwise navigate pages
    constexpr int move_pixels = 2;
    if (selected_annotation_) {
        switch (key) {
            case Qt::Key_Up:
                move_selected_annotation(0, -move_pixels);
                event->accept();
                return;
            case Qt::Key_Down:
                move_selected_annotation(0, move_pixels);
                event->accept();
                return;
            case Qt::Key_Left:
                move_selected_annotation(-move_pixels, 0);
                event->accept();
                return;
            case Qt::Key_Right:
                move_selected_annotation(move_pixels, 0);
                event->accept();
                return;
            case Qt::Key_Plus:
            case Qt::Key_Equal:  // unshifted '+' on US keyboards
            case Qt::Key_Minus: {
                const Annotation* current = nullptr;
                for (const auto& a : document_->annotations())
                    if (a.handle_ == selected_annotation_) { current = &a; break; }
                if (current) {
                    bool larger = (key != Qt::Key_Minus);
                    FontInfo new_font = current->font_info_;
                    new_font.size = next_standard_font_size(new_font.size, larger);
                    if (new_font.size != current->font_info_.size)
                        document_->change_annotation_font(selected_annotation_, new_font);
                }
                event->accept();
                return;
            }
            default: break;
        }
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

    if (selected_annotation_ && document_) {
        // If annotation was moved, save the move first so we delete at the correct PDF position
        flush_pending_annotation_move();

        logger::debug("Deleting annotation with handle {}", static_cast<int>(selected_annotation_));
        if (document_->remove_annotation(selected_annotation_))
            clear_selection();
        else
            logger::error("Failed to remove annotation with handle {}", static_cast<int>(selected_annotation_));
    } else {
        logger::debug("delete_shortcut called but no selection");
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


// Compute the crop QRect for a single page image, taking the user paper crop
// and the segment range into account. The returned rect is in the image's
// pixel space.
//
// seg is the full-page normalized vertical range [0..1] that this image
// actually covers. For a full page or normal (non-performance) mode this is
// [0, 1]; for a split segment (e.g. "14b") it is the slice of the full page
// that the image represents. Crop positions are stored in full-page
// normalized coordinates, so they must be translated into the segment-local
// coordinate space before being converted to pixels. A crop line that lies
// outside the segment's range doesn't apply to this segment.
static QRect compute_page_crop_rect(const PixmapPage& p,
                                    const PaperCrop* crop,
                                    PageRenderer::SegmentRange seg,
                                    int margin,
                                    bool use_border)
{
    const int w = p.width();
    const int h = p.height();

    bool apply_top = false;
    bool apply_bottom = false;
    bool apply_left = false;
    bool apply_right = false;
    int crop_top_px = 0;
    int crop_bottom_px = h;
    int crop_left_px = 0;
    int crop_right_px = w;

    if (crop) {
        // Top/bottom crops must be remapped through the segment range because
        // performance-mode segments subdivide the page vertically.
        const double seg_span = seg.bottom - seg.top;
        if (seg_span > 0.0) {
            if (crop->top) {
                const double local = (*crop->top - seg.top) / seg_span;
                if (local > 0.0 && local < 1.0) {
                    apply_top = true;
                    crop_top_px = std::clamp(static_cast<int>(local * h), 0, h);
                }
            }
            if (crop->bottom) {
                const double local = (*crop->bottom - seg.top) / seg_span;
                if (local > 0.0 && local < 1.0) {
                    apply_bottom = true;
                    crop_bottom_px = std::clamp(static_cast<int>(local * h), 0, h);
                }
            }
        }

        // Left/right crops map directly — segments don't subdivide horizontally.
        if (crop->left) {
            apply_left = true;
            crop_left_px = std::clamp(static_cast<int>(*crop->left * w), 0, w);
        }
        if (crop->right) {
            apply_right = true;
            crop_right_px = std::clamp(static_cast<int>(*crop->right * w), 0, w);
        }
    }

    int left, right, top, bottom;
    if (use_border) {
        left = apply_left ? crop_left_px : std::max(0, p.border.left - margin);
        right = apply_right ? crop_right_px : std::min(w, p.border.right + margin);
        top = apply_top ? crop_top_px : std::max(0, p.border.top - margin);
        bottom = apply_bottom ? crop_bottom_px : std::min(h, p.border.bottom + margin);

        // QPixmap::copy() treats an empty rect as "copy whole pixmap", so fall
        // back to the segment bound on the non-cropped side when the content
        // border and user crop don't overlap.
        if (top >= bottom) {
            if (!apply_top)
                top = 0;
            if (!apply_bottom)
                bottom = h;
        }
        if (left >= right) {
            if (!apply_left)
                left = 0;
            if (!apply_right)
                right = w;
        }
    } else {
        left = apply_left ? crop_left_px : 0;
        right = apply_right ? crop_right_px : w;
        top = apply_top ? crop_top_px : 0;
        bottom = apply_bottom ? crop_bottom_px : h;
    }

    return QRect(left, top, std::max(1, right - left), std::max(1, bottom - top));
}


QRect PDFViewer::compute_display_rect(const PixmapPage& p, PageRenderer::SegmentRange seg) const
{
    const int w = p.width();
    const int h = p.height();

    // While editing paper crops, show the raw full page so the user can click
    // anywhere in the real page extent.
    if (force_full_page_)
        return QRect(0, 0, w, h);

    const bool zoom = config_->zoom_to_content();
    const bool perf = PerformanceMode::is_performance();
    if (!zoom && !perf)
        return QRect(0, 0, w, h);

    const PaperCrop* crop = document_->performance_data().get_paper_crop(p.page_num);
    if (!zoom && !crop)
        return QRect(0, 0, w, h);

    return compute_page_crop_rect(p, crop, seg, config_->border_margin(), zoom);
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
    PageRenderer::SegmentRange seg2;
    if (index + 1 <= count) {
        Page page2 = renderer_.get_page_at_index(index + 1, request_type);
        entry.p2 = PixmapPage(page2);
        seg2 = renderer_.get_segment_range(index + 1);
    } else
        copy_blank_image(entry.p1, entry.p2);

    if (!entry.p1.is_empty() && !entry.p2.is_empty()) {
        const PageRenderer::SegmentRange seg1 = renderer_.get_segment_range(index);
        entry.rendered = compose_double_page(entry.p1, entry.p2, seg1, seg2);
    }

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

    if (entry.p1.is_empty())
        return entry;

    const PageRenderer::SegmentRange seg = renderer_.get_segment_range(index);
    const QRect rect = compute_display_rect(entry.p1, seg);
    entry.rendered = (rect == entry.p1.pixmap.rect()) ? entry.p1.as_pixmap() : entry.p1.pixmap.copy(rect);
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

    // Clear any annotation selection when changing pages
    clear_selection();

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


QPixmap PDFViewer::compose_double_page(const PixmapPage& p1,
                                       const PixmapPage& p2,
                                       PageRenderer::SegmentRange seg1,
                                       PageRenderer::SegmentRange seg2) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    REQUIRES_RET(config_, QPixmap());

    QRect p1_crop = compute_display_rect(p1, seg1);
    QRect p2_crop = compute_display_rect(p2, seg2);

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

    // force_full_page_ means the displayed pixmap is the uncropped page even
    // though zoom_to_content is on in config, so the mapping is direct.
    if (config_->zoom_to_content() && !force_full_page_) {
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

    if (config_->zoom_to_content() && !force_full_page_) {
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


int PDFViewer::normalized_to_display_x(double normalized_pos, int display_width) const
{
    Page full_page = document_->get_page(current_page(), false);
    int full_width = full_page.width();
    int x_full = static_cast<int>(normalized_pos * full_width);

    if (config_->zoom_to_content() && !force_full_page_) {
        int border_left = full_page.border.left;
        int border_width = full_page.border.right - full_page.border.left;
        int x_cropped = x_full - border_left;
        return static_cast<int>((static_cast<float>(x_cropped) / border_width) * display_width);
    } else
        return static_cast<int>((static_cast<float>(x_full) / full_width) * display_width);
}


double PDFViewer::display_x_to_normalized(int display_x, int display_width) const
{
    Page full_page = document_->get_page(current_page(), false);
    int full_width = full_page.width();
    float click_ratio = static_cast<float>(display_x) / display_width;

    if (config_->zoom_to_content() && !force_full_page_) {
        int border_left = full_page.border.left;
        int border_width = full_page.border.right - full_page.border.left;
        int cropped_x = static_cast<int>(click_ratio * border_width);
        int full_x = border_left + cropped_x;
        return static_cast<double>(full_x) / full_width;
    } else {
        int full_x = static_cast<int>(click_ratio * full_width);
        return static_cast<double>(full_x) / full_width;
    }
}


int PDFViewer::label_pixmap_x_offset() const
{
    QPixmap displayed = label_->pixmap();
    if (displayed.isNull())
        return 0;
    if (page_alignment() == Qt::AlignHCenter)
        return (label_->width() - displayed.width()) / 2;
    if (page_alignment() == Qt::AlignRight)
        return label_->width() - displayed.width();
    return 0;
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

        // In double-page mode, compose preview with the other page
        int current = current_page();
        if (in_double_page_view() && last_click_target_.page_num > 0 &&
            current + 1 <= document_->page_count()) {
            bool preview_is_page2 = (last_click_target_.page_num == current + 1);

            // Get the non-preview page
            Page other_page = document_->get_page(preview_is_page2 ? current : current + 1, false);
            QPixmap other_pixmap = other_page.as_pixmap();

            // Create PixmapPage for preview (use the target page's border info)
            Page preview_page_info = document_->get_page(last_click_target_.page_num, false);
            PixmapPage preview_pp(preview_pixmap, last_click_target_.page_num, false);
            preview_pp.border = preview_page_info.border;

            PixmapPage other_pp(other_pixmap, preview_is_page2 ? current : current + 1, false);
            other_pp.border = other_page.border;

            // Compose in correct order. Annotation preview works with full
            // physical pages, so the segments are [0,1] for both.
            PageRenderer::SegmentRange full_seg;
            if (preview_is_page2)
                preview_pixmap = compose_double_page(other_pp, preview_pp, full_seg, full_seg);
            else
                preview_pixmap = compose_double_page(preview_pp, other_pp, full_seg, full_seg);
        }

        scaled_pixmap = preview_pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else if (selected_annotation_moved_ && selected_annotation_) {
        // Render page with annotation at its new in-memory position
        const Annotation* ann = nullptr;
        for (const auto& a : document_->annotations()) {
            if (a.handle_ == selected_annotation_) {
                ann = &a;
                break;
            }
        }

        // Only show moved annotation if it's on a currently visible page
        int current = current_page();
        bool annotation_visible = ann && (ann->page_num_ == current ||
                                          (in_double_page_view() && ann->page_num_ == current + 1));

        if (ann && annotation_visible) {
            QImage moved_image = document_->render_page_with_moved_annotation(
                ann->page_num_, selected_annotation_, selected_annotation_original_x_, selected_annotation_original_y_,
                ann->x_, ann->y_);
            if (!moved_image.isNull()) {
                QPixmap moved_pixmap = QPixmap::fromImage(moved_image);

                // In double-page mode, compose with the other page
                if (in_double_page_view() && current + 1 <= document_->page_count()) {
                    bool moved_is_page2 = (ann->page_num_ == current + 1);

                    Page other_page = document_->get_page(moved_is_page2 ? current : current + 1, false);
                    QPixmap other_pixmap = other_page.as_pixmap();

                    Page moved_page_info = document_->get_page(ann->page_num_, false);
                    PixmapPage moved_pp(moved_pixmap, ann->page_num_, false);
                    moved_pp.border = moved_page_info.border;

                    PixmapPage other_pp(other_pixmap, moved_is_page2 ? current : current + 1, false);
                    other_pp.border = other_page.border;

                    PageRenderer::SegmentRange full_seg;
                    if (moved_is_page2)
                        moved_pixmap = compose_double_page(other_pp, moved_pp, full_seg, full_seg);
                    else
                        moved_pixmap = compose_double_page(moved_pp, other_pp, full_seg, full_seg);
                } else {
                    // Single-page mode: handle performance mode and/or zoom mode cropping
                    if (PerformanceMode::is_performance()) {
                        PageRenderer::Position pos = renderer_.index_to_position(renderer_.current_index());
                        const auto& breaks = document_->performance_data().get_page_breaks(pos.physical_page);
                        if (!breaks.empty()) {
                            double segment_top = 0.0;
                            double segment_bottom = 1.0;
                            if (pos.segment_index == 0) {
                                segment_bottom = breaks[0];
                            } else if (pos.segment_index < static_cast<int>(breaks.size())) {
                                segment_top = breaks[pos.segment_index - 1];
                                segment_bottom = breaks[pos.segment_index];
                            } else {
                                segment_top = breaks.back();
                            }
                            int y_start = static_cast<int>(segment_top * moved_image.height());
                            int y_end = static_cast<int>(segment_bottom * moved_image.height());
                            moved_image = moved_image.copy(0, y_start, moved_image.width(), y_end - y_start);
                        }
                    }
                    if (config_->zoom_to_content()) {
                        Page full_page = document_->get_page(ann->page_num_, false);
                        moved_image = resize_by_border(moved_image, full_page.border, config_->border_margin());
                    }
                    moved_pixmap = QPixmap::fromImage(moved_image);
                }

                scaled_pixmap = moved_pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            } else {
                scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        } else {
            scaled_pixmap = page_.pixmap.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
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
    if (selected_annotation_) {
        QPainter painter(&scaled_pixmap);

        // Find the selected annotation and draw dotted red box
        for (const auto& annotation : document_->annotations()) {
            if (annotation.handle_ == selected_annotation_) {
                QRect bounding_box = calculate_annotation_bounding_box(annotation, scaled_pixmap);
                if (!bounding_box.isEmpty()) {
                    painter.setPen(QPen(Qt::red, 1, Qt::DotLine));
                    painter.drawRect(bounding_box);
                }
                break;
            }
        }
        painter.end();
    }

    // In edit mode, always draw both overlays regardless of submode: page
    // break separators (blue) and paper crop lines + excluded-region hatch
    // (red). The active submode only controls which overlays the mouse edits.
    if (page_break_edit_mode_) {
        QPainter painter(&scaled_pixmap);
        const int display_height = scaled_pixmap.height();
        const int display_width = scaled_pixmap.width();

        const auto& breaks = document_->performance_data().get_page_breaks(current_page());
        const bool dragging_break = (drag_kind_ == DragKind::Break);

        painter.setPen(QPen(Qt::blue, 1, Qt::SolidLine));
        for (double break_pos : breaks) {
            if (dragging_break && drag_is_existing_ && std::abs(break_pos - drag_original_position_) < 0.01)
                continue;
            int y = normalized_to_display_y(break_pos, display_height);
            if (y >= 0 && y < display_height)
                painter.drawLine(0, y, display_width, y);
        }
        if (dragging_break) {
            int y = normalized_to_display_y(drag_position_, display_height);
            if (y >= 0 && y < display_height)
                painter.drawLine(0, y, display_width, y);
        }

        const PaperCrop* stored = document_->performance_data().get_paper_crop(current_page());
        std::optional<double> top_line;
        std::optional<double> bottom_line;
        if (stored) {
            if (stored->top && !(drag_kind_ == DragKind::CropTop && drag_is_existing_))
                top_line = *stored->top;
            if (stored->bottom && !(drag_kind_ == DragKind::CropBottom && drag_is_existing_))
                bottom_line = *stored->bottom;
        }
        if (drag_kind_ == DragKind::CropTop)
            top_line = drag_position_;
        else if (drag_kind_ == DragKind::CropBottom)
            bottom_line = drag_position_;

        QBrush hatch(QColor(255, 0, 0, 60), Qt::BDiagPattern);
        painter.setPen(QPen(Qt::red, 1, Qt::SolidLine));

        if (top_line) {
            int y = normalized_to_display_y(*top_line, display_height);
            y = std::clamp(y, 0, display_height);
            if (y > 0)
                painter.fillRect(QRect(0, 0, display_width, y), hatch);
            painter.drawLine(0, y, display_width, y);
        }
        if (bottom_line) {
            int y = normalized_to_display_y(*bottom_line, display_height);
            y = std::clamp(y, 0, display_height);
            if (y < display_height)
                painter.fillRect(QRect(0, y, display_width, display_height - y), hatch);
            painter.drawLine(0, y, display_width, y);
        }

        // Left/right crop lines (vertical).
        std::optional<double> left_line;
        std::optional<double> right_line;
        if (stored) {
            if (stored->left && !(drag_kind_ == DragKind::CropLeft && drag_is_existing_))
                left_line = *stored->left;
            if (stored->right && !(drag_kind_ == DragKind::CropRight && drag_is_existing_))
                right_line = *stored->right;
        }
        if (drag_kind_ == DragKind::CropLeft)
            left_line = drag_position_;
        else if (drag_kind_ == DragKind::CropRight)
            right_line = drag_position_;

        if (left_line) {
            int x = normalized_to_display_x(*left_line, display_width);
            x = std::clamp(x, 0, display_width);
            if (x > 0)
                painter.fillRect(QRect(0, 0, x, display_height), hatch);
            painter.drawLine(x, 0, x, display_height);
        }
        if (right_line) {
            int x = normalized_to_display_x(*right_line, display_width);
            x = std::clamp(x, 0, display_width);
            if (x < display_width)
                painter.fillRect(QRect(x, 0, display_width - x, display_height), hatch);
            painter.drawLine(x, 0, x, display_height);
        }

        painter.end();
    }

    label_->setPixmap(scaled_pixmap);
    adjust_initial_subwindow_size();
    // QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}


void PDFViewer::adjust_initial_subwindow_size()
{
    SAFE_METHOD;
    TRACE_CALL;
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
    if (!enabled)
        pending_music_symbol_codepoint_ = 0;
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
}


void PDFViewer::set_pending_music_symbol(int codepoint)
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("U+{:04X}", codepoint);

    // Editor commits text via its focusOutEvent path, so dropping focus is
    // the public way to flush an in-progress edit.
    if (annotation_editor_ && annotation_editor_->isVisible())
        annotation_editor_->clearFocus();

    pending_music_symbol_codepoint_ = codepoint;
    setCursor(codepoint != 0 ? Qt::CrossCursor : (text_annotation_mode_ ? Qt::IBeamCursor : Qt::ArrowCursor));
}

void PDFViewer::set_page_break_edit_mode(bool enabled)
{
    SAFE_METHOD;
    TRACE_CALL;
    page_break_edit_mode_ = enabled;
    // Always reset submode state; while editing we force full-page display so
    // titles/footers are visible, then restore normal zoom on exit.
    page_break_sub_mode_ = PageBreakSubMode::EditBreaks;
    force_full_page_ = enabled;
    drag_kind_ = DragKind::None;
    drag_is_existing_ = false;
    clear_prefetch();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    refresh();
    emit page_break_edit_mode_changed(enabled);
}


void PDFViewer::set_page_break_sub_mode(PageBreakSubMode mode)
{
    SAFE_METHOD;
    TRACE_CALL;
    if (!page_break_edit_mode_)
        return;
    if (page_break_sub_mode_ == mode)
        return;

    page_break_sub_mode_ = mode;
    drag_kind_ = DragKind::None;
    drag_is_existing_ = false;

    switch (mode) {
        case PageBreakSubMode::EditBreaks: setCursor(Qt::CrossCursor); break;
        case PageBreakSubMode::EditPaperCrop: setCursor(Qt::SplitVCursor); break;
        case PageBreakSubMode::EditPaperCropLR: setCursor(Qt::SplitHCursor); break;
    }

    // Submode switches only change interaction and cursor; the rendered page
    // is identical (edit mode always renders full-page) and both break and
    // crop overlays are drawn in both submodes. Just repaint overlays.
    update_image();
    emit page_break_sub_mode_changed(mode);
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

        // Calculate the effective PDF width that maps to displayed pixels
        // In zoom mode, this is the cropped portion's width in points
        auto [pdf_width_pts, _] = document_->get_page_dimensions_points(last_click_target_.page_num);
        float effective_pdf_width = pdf_width_pts;
        if (config_->zoom_to_content()) {
            Page full_page = document_->get_page(last_click_target_.page_num, false);
            float cropped_width_ratio =
                static_cast<float>(full_page.border.right - full_page.border.left) / full_page.width();
            effective_pdf_width = cropped_width_ratio * pdf_width_pts;
        }

        // Adjust the stored target Y for cursor offset
        if (cursor_offset > 0) {
            float pixels_to_points_ratio = effective_pdf_width / static_cast<float>(displayed.width());
            last_click_target_.points_y -= cursor_offset * pixels_to_points_ratio;
        }

        float points_to_pixels = static_cast<float>(displayed.width()) / effective_pdf_width;
        annotation_editor_->set_dpi_scale(points_to_pixels);

        if (pending_music_symbol_codepoint_ != 0) {
            // The IBeam cursor compensation above shifts last_click_target_.points_y
            // downward in PDF coords to match where users perceive the IBeam
            // hotspot. With the crosshair cursor used for music symbols, the
            // hotspot is the actual click point, so undo that shift here.
            ClickTarget raw = get_click_target(event);
            const FontInfo& fi = config_->annotation_font();
            auto [cr, cg, cb] = fi.color;
            document_->add_music_symbol_annotation(raw.page_num, raw.points_x, raw.points_y,
                                                   pending_music_symbol_codepoint_, fi.size,
                                                   cr, cg, cb);
            // Stay in this mode so consecutive clicks place more of the same glyph.
            event->accept();
            return;
        }

        annotation_editor_->start_editing(adjusted_pos);

        event->accept();
        return;
    } else if (event->button() == Qt::LeftButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditBreaks) {
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

        drag_kind_ = DragKind::Break;
        if (clicked_break >= 0.0) {
            // Clicked on existing break - start dragging it
            drag_is_existing_ = true;
            drag_position_ = clicked_break;
            drag_original_position_ = clicked_break;
        } else {
            // Clicked in empty space - start creating new break
            drag_is_existing_ = false;
            drag_position_ = normalized_pos;
        }

        update_image();
        event->accept();
        return;
    } else if (event->button() == Qt::RightButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditBreaks) {
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
                clear_prefetch();
                update_image();
                break;
            }
        }

        event->accept();
        return;
    } else if (event->button() == Qt::LeftButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditPaperCrop) {
        // Handle paper crop editing - left button to add/move
        int click_y = event->pos().y();
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int display_height = displayed.height();
        double normalized_pos = display_y_to_normalized(click_y, display_height);
        normalized_pos = std::max(0.0, std::min(1.0, normalized_pos));

        // Hit-test against existing crop lines first (5 pixel tolerance).
        const PaperCrop* crop = document_->performance_data().get_paper_crop(current_page());
        DragKind hit = DragKind::None;
        double hit_pos = 0.0;
        if (crop) {
            if (crop->top) {
                int top_y = normalized_to_display_y(*crop->top, display_height);
                if (std::abs(click_y - top_y) <= 5) {
                    hit = DragKind::CropTop;
                    hit_pos = *crop->top;
                }
            }
            if (hit == DragKind::None && crop->bottom) {
                int bot_y = normalized_to_display_y(*crop->bottom, display_height);
                if (std::abs(click_y - bot_y) <= 5) {
                    hit = DragKind::CropBottom;
                    hit_pos = *crop->bottom;
                }
            }
        }

        if (hit != DragKind::None) {
            drag_kind_ = hit;
            drag_is_existing_ = true;
            drag_position_ = hit_pos;
            drag_original_position_ = hit_pos;
        } else {
            // No hit - decide by which half of the page was clicked.
            drag_kind_ = (normalized_pos < 0.5) ? DragKind::CropTop : DragKind::CropBottom;
            drag_is_existing_ = false;
            drag_position_ = normalized_pos;
        }

        update_image();
        event->accept();
        return;
    } else if (event->button() == Qt::RightButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditPaperCrop) {
        // Handle paper crop deletion - right button
        int click_y = event->pos().y();
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int display_height = displayed.height();

        const PaperCrop* crop = document_->performance_data().get_paper_crop(current_page());
        if (crop) {
            bool deleted = false;
            if (crop->top) {
                int top_y = normalized_to_display_y(*crop->top, display_height);
                if (std::abs(click_y - top_y) <= 5) {
                    document_->performance_data().clear_paper_crop_top(current_page());
                    deleted = true;
                }
            }
            if (!deleted && crop->bottom) {
                int bot_y = normalized_to_display_y(*crop->bottom, display_height);
                if (std::abs(click_y - bot_y) <= 5) {
                    document_->performance_data().clear_paper_crop_bottom(current_page());
                    deleted = true;
                }
            }
            if (deleted) {
                document_->performance_data().save(document_->path());
                clear_prefetch();
                update_image();
            }
        }

        event->accept();
        return;
    } else if (event->button() == Qt::LeftButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditPaperCropLR) {
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int click_x = event->pos().x() - label_pixmap_x_offset();
        int display_width = displayed.width();
        double normalized_pos = display_x_to_normalized(click_x, display_width);
        normalized_pos = std::clamp(normalized_pos, 0.0, 1.0);

        const PaperCrop* crop = document_->performance_data().get_paper_crop(current_page());
        DragKind hit = DragKind::None;
        double hit_pos = 0.0;
        if (crop) {
            if (crop->left) {
                int left_x = normalized_to_display_x(*crop->left, display_width);
                if (std::abs(click_x - left_x) <= 5) {
                    hit = DragKind::CropLeft;
                    hit_pos = *crop->left;
                }
            }
            if (hit == DragKind::None && crop->right) {
                int right_x = normalized_to_display_x(*crop->right, display_width);
                if (std::abs(click_x - right_x) <= 5) {
                    hit = DragKind::CropRight;
                    hit_pos = *crop->right;
                }
            }
        }

        if (hit != DragKind::None) {
            drag_kind_ = hit;
            drag_is_existing_ = true;
            drag_position_ = hit_pos;
            drag_original_position_ = hit_pos;
        } else {
            drag_kind_ = (normalized_pos < 0.5) ? DragKind::CropLeft : DragKind::CropRight;
            drag_is_existing_ = false;
            drag_position_ = normalized_pos;
        }

        update_image();
        event->accept();
        return;
    } else if (event->button() == Qt::RightButton && page_break_edit_mode_ &&
               page_break_sub_mode_ == PageBreakSubMode::EditPaperCropLR) {
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        int click_x = event->pos().x() - label_pixmap_x_offset();
        int display_width = displayed.width();

        const PaperCrop* crop = document_->performance_data().get_paper_crop(current_page());
        if (crop) {
            bool deleted = false;
            if (crop->left) {
                int left_x = normalized_to_display_x(*crop->left, display_width);
                if (std::abs(click_x - left_x) <= 5) {
                    document_->performance_data().clear_paper_crop_left(current_page());
                    deleted = true;
                }
            }
            if (!deleted && crop->right) {
                int right_x = normalized_to_display_x(*crop->right, display_width);
                if (std::abs(click_x - right_x) <= 5) {
                    document_->performance_data().clear_paper_crop_right(current_page());
                    deleted = true;
                }
            }
            if (deleted) {
                document_->performance_data().save(document_->path());
                clear_prefetch();
                update_image();
            }
        }

        event->accept();
        return;
    } else if (event->button() == Qt::RightButton && selected_annotation_) {
        // Right-click on the selected annotation opens a per-annotation font
        // picker. Changes apply only to this annotation; global config is
        // untouched.
        AnnotationHandle clicked = find_annotation_at_point(event);
        if (clicked && clicked == selected_annotation_) {
            const Annotation* current = nullptr;
            for (const auto& a : document_->annotations())
                if (a.handle_ == selected_annotation_) { current = &a; break; }
            if (current) {
                auto picked = MusicReader::show_font_picker(this, current->font_info_);
                if (picked)
                    document_->change_annotation_font(selected_annotation_, *picked);
            }
            event->accept();
            return;
        }
    } else if (event->button() == Qt::LeftButton) {
        // Handle annotation selection and dragging
        AnnotationHandle clicked_annotation = find_annotation_at_point(event);
        if (clicked_annotation) {
            if (clicked_annotation != selected_annotation_)
                select_annotation(clicked_annotation);
            dragging_annotation_ = true;
            drag_start_pos_ = event->pos();
            setFocus();
            event->accept();
            return;
        } else {
            // Clicked elsewhere - clear selection
            clear_selection();
        }
    }

    QWidget::mousePressEvent(event);
}


void PDFViewer::mouseMoveEvent(QMouseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (drag_kind_ != DragKind::None) {
        QPixmap displayed = label_->pixmap();
        if (displayed.isNull())
            return;

        double normalized_pos;
        if (drag_kind_ == DragKind::CropLeft || drag_kind_ == DragKind::CropRight)
            normalized_pos = display_x_to_normalized(event->pos().x() - label_pixmap_x_offset(), displayed.width());
        else
            normalized_pos = display_y_to_normalized(event->pos().y(), displayed.height());

        drag_position_ = std::clamp(normalized_pos, 0.0, 1.0);
        update_image();
        event->accept();
        return;
    }

    if (dragging_annotation_ && selected_annotation_) {
        QPoint delta = event->pos() - drag_start_pos_;
        if (delta.manhattanLength() > 3) {
            move_selected_annotation(delta.x(), delta.y());
            drag_start_pos_ = event->pos();
        }
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}


void PDFViewer::mouseReleaseEvent(QMouseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->button() == Qt::LeftButton && drag_kind_ != DragKind::None) {
        const int page = current_page();
        auto& perf = document_->performance_data();

        switch (drag_kind_) {
            case DragKind::Break: {
                if (drag_is_existing_)
                    perf.remove_page_break(page, drag_original_position_);
                perf.add_page_break(page, drag_position_);
                break;
            }
            case DragKind::CropTop: {
                // Clamp above any existing bottom crop on this page.
                double pos = drag_position_;
                if (const PaperCrop* existing = perf.get_paper_crop(page); existing && existing->bottom) {
                    double limit = *existing->bottom - 1e-4;
                    if (pos >= limit)
                        pos = std::max(0.0, limit);
                }
                perf.set_paper_crop_top(page, pos);
                break;
            }
            case DragKind::CropBottom: {
                // Clamp below any existing top crop on this page.
                double pos = drag_position_;
                if (const PaperCrop* existing = perf.get_paper_crop(page); existing && existing->top) {
                    double limit = *existing->top + 1e-4;
                    if (pos <= limit)
                        pos = std::min(1.0, limit);
                }
                perf.set_paper_crop_bottom(page, pos);
                break;
            }
            case DragKind::CropLeft: {
                double pos = drag_position_;
                if (const PaperCrop* existing = perf.get_paper_crop(page); existing && existing->right) {
                    double limit = *existing->right - 1e-4;
                    if (pos >= limit)
                        pos = std::max(0.0, limit);
                }
                perf.set_paper_crop_left(page, pos);
                break;
            }
            case DragKind::CropRight: {
                double pos = drag_position_;
                if (const PaperCrop* existing = perf.get_paper_crop(page); existing && existing->left) {
                    double limit = *existing->left + 1e-4;
                    if (pos <= limit)
                        pos = std::min(1.0, limit);
                }
                perf.set_paper_crop_right(page, pos);
                break;
            }
            case DragKind::None: break;
        }

        perf.save(document_->path());
        clear_prefetch();

        drag_kind_ = DragKind::None;
        drag_is_existing_ = false;
        update_image();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && dragging_annotation_) {
        dragging_annotation_ = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PDFViewer::on_annotation_text_finished(const QString& text)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Clear preview state before adding annotation so any update_image() calls show actual page
    preview_page_image_.reset();
    annotation_editor_->set_preview_mode(false);

    if (!text.trimmed().isEmpty() && last_click_target_.page_num > 0) {

        const FontInfo& font_info = config_->annotation_font();

        // Use MuPDF's own font metrics for accurate sizing
        float width_points = text_width(font_info.family, font_info.size, text.toStdString());
        float height_points = text_height(font_info.family, font_info.size);

        Annotation annotation(text.toStdString(), last_click_target_.page_num, last_click_target_.points_x,
                              last_click_target_.points_y, width_points, height_points, font_info);
        document_->add_annotation(annotation);

        // Save text for debug rendering
        last_annotation_text_ = text;
    }

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
    float width_points = text_width(font_info.family, font_info.size, measure_text);
    float height_points = text_height(font_info.family, font_info.size);

    // Pass raw click coordinates as baseline - render_page_with_preview_annotation
    // will handle positioning the rect so baseline lands at click point
    Annotation preview_annotation(text.toStdString(), last_click_target_.page_num, last_click_target_.points_x,
                                  last_click_target_.points_y, // raw baseline in PDF coords
                                  width_points, height_points, font_info);

    // Render page with preview annotation
    QImage preview_image =
        document_->render_page_with_preview_annotation(last_click_target_.page_num, preview_annotation);

    if (!preview_image.isNull()) {
        // Crop for performance mode segmentation
        if (PerformanceMode::is_performance()) {
            PageRenderer::Position pos = renderer_.index_to_position(renderer_.current_index());
            const auto& breaks = document_->performance_data().get_page_breaks(pos.physical_page);
            if (!breaks.empty()) {
                int height = preview_image.height();
                double top_normalized = 0.0;
                double bottom_normalized = 1.0;

                if (pos.segment_index == 0) {
                    bottom_normalized = breaks[0];
                } else if (pos.segment_index < static_cast<int>(breaks.size())) {
                    top_normalized = breaks[pos.segment_index - 1];
                    bottom_normalized = breaks[pos.segment_index];
                } else {
                    top_normalized = breaks.back();
                }

                int top_pixel = static_cast<int>(top_normalized * height);
                int bottom_pixel = static_cast<int>(bottom_normalized * height);
                int crop_height = bottom_pixel - top_pixel;
                if (crop_height > 0)
                    preview_image = preview_image.copy(0, top_pixel, preview_image.width(), crop_height);
            }
        }

        // Crop to match displayed page when in zoom mode (only for single-page view)
        // In double-page view, compose_double_page() handles cropping
        if (config_->zoom_to_content() && in_single_page_view()) {
            // Detect borders on the (possibly segment-cropped) image
            Border segment_border = find_content_edges(preview_image);
            preview_image = resize_by_border(preview_image, segment_border, config_->border_margin());
        }
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

    bool double_page = in_double_page_view();
    bool zoom_mode = config_ && config_->zoom_to_content();

    // In double-page mode, determine which page was clicked and adjust coordinates
    int page1_display_width = pixmap_width;
    int page1_display_height = pixmap_height;
    int page_y_offset = 0; // Vertical offset for the target page within the combined display

    if (double_page && target_page + 1 <= document_->page_count()) {
        constexpr int line_width = 8; // Must match compose_double_page()
        Page p1 = document_->get_page(target_page, false);
        Page p2 = document_->get_page(target_page + 1, false);
        int margin = config_->border_margin();

        // Calculate dimensions at render DPI (before scaling)
        int p1_render_width, p1_render_height, p2_render_width, p2_render_height;
        if (zoom_mode) {
            p1_render_width = p1.border.right - p1.border.left + 2 * margin;
            p1_render_height = p1.border.bottom - p1.border.top + 2 * margin;
            p2_render_width = p2.border.right - p2.border.left + 2 * margin;
            p2_render_height = p2.border.bottom - p2.border.top + 2 * margin;
        } else {
            p1_render_width = p1.width();
            p1_render_height = p1.height();
            p2_render_width = p2.width();
            p2_render_height = p2.height();
        }

        int total_render_width = p1_render_width + line_width + p2_render_width;
        int max_render_height = std::max(p1_render_height, p2_render_height);

        // Calculate scaled display dimensions (preserving aspect ratio)
        float scale = static_cast<float>(pixmap_width) / total_render_width;
        int p1_display_w = static_cast<int>(p1_render_width * scale);
        int p1_display_h = static_cast<int>(p1_render_height * scale);
        int p2_display_w = static_cast<int>(p2_render_width * scale);
        int p2_display_h = static_cast<int>(p2_render_height * scale);
        int line_display_w = static_cast<int>(line_width * scale);
        int max_display_h = static_cast<int>(max_render_height * scale);

        int p1_y_offset = (max_display_h - p1_display_h) / 2;
        int p2_y_offset = (max_display_h - p2_display_h) / 2;

        // Determine which page was clicked based on x position
        if (mouse_x > p1_display_w + line_display_w / 2) {
            // Clicked on second page
            target_page++;
            mouse_x -= (p1_display_w + line_display_w);
            page1_display_width = p2_display_w;
            page1_display_height = p2_display_h;
            page_y_offset = p2_y_offset;
        } else {
            page1_display_width = p1_display_w;
            page1_display_height = p1_display_h;
            page_y_offset = p1_y_offset;
        }

        // Adjust mouse_y for the vertical centering offset of this page
        mouse_y -= page_y_offset;
    }

    // Get the full page to check for border/cropping
    Page full_page = document_->get_page(target_page, false);
    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(target_page);

    // Calculate click ratio in display space (relative to this page's display area)
    float click_ratio_x = float(mouse_x) / page1_display_width;
    float click_ratio_y = float(mouse_y) / page1_display_height;

    // In performance mode, map the click ratio to the segment's portion of the full page
    double segment_top_normalized = 0.0;
    double segment_bottom_normalized = 1.0;

    if (PerformanceMode::is_performance()) {
        PageRenderer::Position pos = renderer_.index_to_position(renderer_.current_index());
        const auto& breaks = document_->performance_data().get_page_breaks(pos.physical_page);
        if (!breaks.empty()) {
            if (pos.segment_index == 0) {
                segment_bottom_normalized = breaks[0];
            } else if (pos.segment_index < static_cast<int>(breaks.size())) {
                segment_top_normalized = breaks[pos.segment_index - 1];
                segment_bottom_normalized = breaks[pos.segment_index];
            } else {
                segment_top_normalized = breaks.back();
            }
        }
    }

    // Map click_ratio_y from segment space to full page space
    double segment_height_normalized = segment_bottom_normalized - segment_top_normalized;
    float full_page_ratio_y = static_cast<float>(segment_top_normalized + click_ratio_y * segment_height_normalized);

    float points_x, points_y;

    float full_width = full_page.width();
    float full_height = full_page.height();

    if (zoom_mode) {
        // When zoomed, the displayed image is cropped to border
        // Need to get border for the segment, not the full page
        Border effective_border = full_page.border;

        if (PerformanceMode::is_performance() && segment_height_normalized < 1.0) {
            // Recalculate border for the segment
            int seg_top_pixel = static_cast<int>(segment_top_normalized * full_height);
            int seg_bottom_pixel = static_cast<int>(segment_bottom_normalized * full_height);
            int seg_height = seg_bottom_pixel - seg_top_pixel;
            if (seg_height > 0) {
                QImage segment_img = full_page.img.copy(0, seg_top_pixel, full_page.width(), seg_height);
                effective_border = find_content_edges(segment_img);
                // Adjust border coordinates to full page space
                effective_border.top += seg_top_pixel;
                effective_border.bottom += seg_top_pixel;
            }
        }

        float border_left = effective_border.left;
        float border_top = effective_border.top;
        float border_width = effective_border.right - effective_border.left;
        float border_height = effective_border.bottom - effective_border.top;

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
        // When not zoomed, direct mapping using full page ratio
        points_x = click_ratio_x * pdf_width_points;
        points_y = pdf_height_points - (full_page_ratio_y * pdf_height_points);
    }

    return {target_page, points_x, points_y};
}


QRect PDFViewer::calculate_annotation_bounding_box(const Annotation& annotation, const QPixmap& displayed) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    int current_page_num = current_page();
    bool double_page = in_double_page_view();
    bool is_on_second_page = double_page && annotation.page_num_ == current_page_num + 1;

    // Check if annotation is on a visible page
    if (annotation.page_num_ != current_page_num && !is_on_second_page)
        return QRect();

    if (displayed.isNull())
        return QRect();

    int annot_page = annotation.page_num_;
    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(annot_page);

    // annotation.y_ is the BASELINE in PDF coords (y from bottom)
    // Music symbols use the saved /Rect directly with a small visual pad so
    // the dotted outline doesn't sit on the glyph's ink.
    float annot_x_left, annot_x_right, annot_top_pdf, annot_bottom_pdf;
    if (annotation.is_music_symbol_) {
        constexpr float kMusicSelectionPadPt = 2.0f;
        annot_x_left = annotation.x_ - kMusicSelectionPadPt;
        annot_x_right = annotation.x_ + annotation.width_ + kMusicSelectionPadPt;
        annot_top_pdf = annotation.rect_top_pdf_ + kMusicSelectionPadPt;
        annot_bottom_pdf = annotation.rect_top_pdf_ - annotation.height_ - kMusicSelectionPadPt;
    } else {
        float ascent = font_ascent(annotation.font_info_.family, annotation.font_info_.size);
        float descent = font_descent(annotation.font_info_.family, annotation.font_info_.size);
        float width = text_width(annotation.font_info_.family, annotation.font_info_.size, annotation.text_);
        annot_x_left = annotation.x_;
        annot_x_right = annotation.x_ + width;
        annot_top_pdf = annotation.y_ + ascent;
        annot_bottom_pdf = annotation.y_ - descent;
    }

    float display_x, display_y, display_w, display_h;
    int x_offset = 0;
    int y_offset = 0;

    bool zoom_mode = config_ && config_->zoom_to_content();
    Page full_page = document_->get_page(annot_page, false);
    float full_width = full_page.width();
    float full_height = full_page.height();

    // Performance mode segment boundaries
    double segment_top_normalized = 0.0;
    double segment_bottom_normalized = 1.0;

    if (PerformanceMode::is_performance()) {
        PageRenderer::Position pos = renderer_.index_to_position(renderer_.current_index());
        const auto& breaks = document_->performance_data().get_page_breaks(pos.physical_page);
        if (!breaks.empty()) {
            if (pos.segment_index == 0) {
                segment_bottom_normalized = breaks[0];
            } else if (pos.segment_index < static_cast<int>(breaks.size())) {
                segment_top_normalized = breaks[pos.segment_index - 1];
                segment_bottom_normalized = breaks[pos.segment_index];
            } else {
                segment_top_normalized = breaks.back();
            }
        }
    }

    // Calculate offsets for double-page mode
    float page_display_width = static_cast<float>(displayed.width());
    float page_display_height = static_cast<float>(displayed.height());

    if (double_page && current_page_num + 1 <= document_->page_count()) {
        constexpr int line_width = 8;
        Page p1 = document_->get_page(current_page_num, false);
        Page p2 = document_->get_page(current_page_num + 1, false);
        int margin = config_->border_margin();

        int p1_render_width, p1_render_height, p2_render_width, p2_render_height;
        if (zoom_mode) {
            p1_render_width = p1.border.right - p1.border.left + 2 * margin;
            p1_render_height = p1.border.bottom - p1.border.top + 2 * margin;
            p2_render_width = p2.border.right - p2.border.left + 2 * margin;
            p2_render_height = p2.border.bottom - p2.border.top + 2 * margin;
        } else {
            p1_render_width = p1.width();
            p1_render_height = p1.height();
            p2_render_width = p2.width();
            p2_render_height = p2.height();
        }

        int total_render_width = p1_render_width + line_width + p2_render_width;
        int max_render_height = std::max(p1_render_height, p2_render_height);
        float scale = static_cast<float>(displayed.width()) / total_render_width;

        if (is_on_second_page) {
            x_offset = static_cast<int>((p1_render_width + line_width) * scale);
            page_display_width = p2_render_width * scale;
            page_display_height = p2_render_height * scale;
            y_offset = static_cast<int>((max_render_height - p2_render_height) * scale / 2);
        } else {
            page_display_width = p1_render_width * scale;
            page_display_height = p1_render_height * scale;
            y_offset = static_cast<int>((max_render_height - p1_render_height) * scale / 2);
        }
    }

    // Convert annotation PDF coords to ratios within the full page
    float annot_left_ratio = annot_x_left / pdf_width_points;
    float annot_top_ratio = (pdf_height_points - annot_top_pdf) / pdf_height_points;
    float annot_right_ratio = annot_x_right / pdf_width_points;
    float annot_bottom_ratio = (pdf_height_points - annot_bottom_pdf) / pdf_height_points;

    // In performance mode, map from full page ratio to segment ratio
    double segment_height = segment_bottom_normalized - segment_top_normalized;
    float segment_annot_top_ratio = static_cast<float>((annot_top_ratio - segment_top_normalized) / segment_height);
    float segment_annot_bottom_ratio = static_cast<float>((annot_bottom_ratio - segment_top_normalized) / segment_height);

    if (zoom_mode) {
        // Get effective border for the segment (or full page if not in performance mode)
        Border effective_border = full_page.border;

        if (PerformanceMode::is_performance() && segment_height < 1.0) {
            int seg_top_pixel = static_cast<int>(segment_top_normalized * full_height);
            int seg_bottom_pixel = static_cast<int>(segment_bottom_normalized * full_height);
            int seg_height = seg_bottom_pixel - seg_top_pixel;
            if (seg_height > 0) {
                QImage segment_img = full_page.img.copy(0, seg_top_pixel, full_page.width(), seg_height);
                effective_border = find_content_edges(segment_img);
            }
        }

        float border_left = effective_border.left;
        float border_top = effective_border.top;
        float border_width = effective_border.right - effective_border.left;
        float border_height = effective_border.bottom - effective_border.top;

        // Convert annotation to segment pixel coords, then to cropped coords
        float seg_height_pixels = static_cast<float>(segment_height * full_height);
        float annot_left_seg_px = annot_left_ratio * full_width;
        float annot_top_seg_px = segment_annot_top_ratio * seg_height_pixels;
        float annot_right_seg_px = annot_right_ratio * full_width;
        float annot_bottom_seg_px = segment_annot_bottom_ratio * seg_height_pixels;

        // Convert from segment pixel coords to cropped/border coords
        float cropped_left = annot_left_seg_px - border_left;
        float cropped_top = annot_top_seg_px - border_top;
        float cropped_right = annot_right_seg_px - border_left;
        float cropped_bottom = annot_bottom_seg_px - border_top;

        // Convert to display coords (ratio within cropped area * page display size)
        display_x = (cropped_left / border_width) * page_display_width + x_offset;
        display_y = (cropped_top / border_height) * page_display_height + y_offset;
        display_w = ((cropped_right - cropped_left) / border_width) * page_display_width;
        display_h = ((cropped_bottom - cropped_top) / border_height) * page_display_height;
    } else {
        // No zoom - use segment ratios directly
        display_x = annot_left_ratio * page_display_width + x_offset;
        display_y = segment_annot_top_ratio * page_display_height + y_offset;
        display_w = (annot_right_ratio - annot_left_ratio) * page_display_width;
        display_h = (segment_annot_bottom_ratio - segment_annot_top_ratio) * page_display_height;
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
    logger::info("find_annotation_at_point: click at page={} x={:.1f} y={:.1f}", click.page_num, click.points_x,
                 click.points_y);
    for (const auto& annotation : document_->annotations()) {
        if (annotation.page_num_ != click.page_num)
            continue;

        // Annotation bounds in PDF points
        // annotation.y_ stores the BASELINE (not top edge)
        float annot_left, annot_right, annot_top, annot_bottom;
        if (annotation.is_music_symbol_) {
            // Use the saved /Rect directly — same hit-test Foxit/Acrobat do.
            // Pad a couple points all around so the click target and outline
            // don't sit on the glyph's ink.
            constexpr float kMusicSelectionPadPt = 2.0f;
            annot_left = annotation.x_ - kMusicSelectionPadPt;
            annot_right = annotation.x_ + annotation.width_ + kMusicSelectionPadPt;
            annot_top = annotation.rect_top_pdf_ + kMusicSelectionPadPt;
            annot_bottom = annotation.rect_top_pdf_ - annotation.height_ - kMusicSelectionPadPt;
        } else {
            float ascent = font_ascent(annotation.font_info_.family, annotation.font_info_.size);
            float descent = font_descent(annotation.font_info_.family, annotation.font_info_.size);
            float width = text_width(annotation.font_info_.family, annotation.font_info_.size, annotation.text_);
            annot_left = annotation.x_;
            annot_right = annotation.x_ + width;
            annot_top = annotation.y_ + ascent;
            annot_bottom = annotation.y_ - descent;
        }

        logger::info("  annotation '{}': x={:.1f} y={:.1f} bounds=[{:.1f},{:.1f}]-[{:.1f},{:.1f}]", annotation.text_,
                     annotation.x_, annotation.y_, annot_left, annot_bottom, annot_right, annot_top);

        if (click.points_x >= annot_left && click.points_x <= annot_right && click.points_y >= annot_bottom &&
            click.points_y <= annot_top) {
            logger::info("  -> HIT");
            return annotation.handle_;
        }
    }

    logger::info("  -> no annotation found");
    return AnnotationHandle(); // No annotation found
}


void PDFViewer::select_annotation(const AnnotationHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Flush any pending move from previously selected annotation
    flush_pending_annotation_move();

    selected_annotation_ = handle;
    selected_annotation_moved_ = false;

    // Capture original position for later save
    if (document_) {
        for (const auto& ann : document_->annotations()) {
            if (ann.handle_ == handle) {
                selected_annotation_original_x_ = ann.x_;
                selected_annotation_original_y_ = ann.y_;
                break;
            }
        }
    }

    update_image(); // Refresh to show selection
}


void PDFViewer::clear_selection()
{
    SAFE_METHOD;
    TRACE_CALL;

    if (!selected_annotation_)
        return;

    flush_pending_annotation_move();
    selected_annotation_.clear();
    update_image(); // Refresh to hide selection
}


void PDFViewer::move_selected_annotation(int dx_pixels, int dy_pixels)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!selected_annotation_ || !document_)
        return;

    // Find the selected annotation to get its current position
    const Annotation* annotation = nullptr;
    for (const auto& ann : document_->annotations()) {
        if (ann.handle_ == selected_annotation_) {
            annotation = &ann;
            break;
        }
    }
    if (!annotation)
        return;

    QPixmap displayed = label_->pixmap();
    if (displayed.isNull())
        return;

    auto [pdf_width_points, pdf_height_points] = document_->get_page_dimensions_points(annotation->page_num_);

    // Calculate the effective display dimensions for this annotation's page
    // Must account for double-page mode and zoom mode
    float page_display_width = static_cast<float>(displayed.width());
    float page_display_height = static_cast<float>(displayed.height());

    int current = current_page();
    bool double_page = in_double_page_view();
    bool zoom_mode = config_ && config_->zoom_to_content();

    if (double_page && current + 1 <= document_->page_count()) {
        Page p1 = document_->get_page(current, false);
        Page p2 = document_->get_page(current + 1, false);
        int margin = config_->border_margin();
        constexpr int line_width = 8;

        int p1_render_width, p1_render_height, p2_render_width, p2_render_height;
        if (zoom_mode) {
            p1_render_width = p1.border.right - p1.border.left + 2 * margin;
            p1_render_height = p1.border.bottom - p1.border.top + 2 * margin;
            p2_render_width = p2.border.right - p2.border.left + 2 * margin;
            p2_render_height = p2.border.bottom - p2.border.top + 2 * margin;
        } else {
            p1_render_width = p1.width();
            p1_render_height = p1.height();
            p2_render_width = p2.width();
            p2_render_height = p2.height();
        }

        int total_render_width = p1_render_width + line_width + p2_render_width;
        float scale = displayed.width() / static_cast<float>(total_render_width);

        bool is_on_page2 = (annotation->page_num_ == current + 1);
        if (is_on_page2) {
            page_display_width = p2_render_width * scale;
            page_display_height = p2_render_height * scale;
        } else {
            page_display_width = p1_render_width * scale;
            page_display_height = p1_render_height * scale;
        }
    }

    // Calculate effective PDF dimensions (accounting for zoom cropping and performance mode)
    float effective_pdf_width = pdf_width_points;
    float effective_pdf_height = pdf_height_points;

    // Performance mode: we're only showing a segment of the page
    if (PerformanceMode::is_performance()) {
        PageRenderer::Position pos = renderer_.index_to_position(renderer_.current_index());
        const auto& breaks = document_->performance_data().get_page_breaks(pos.physical_page);
        if (!breaks.empty()) {
            double segment_top = 0.0;
            double segment_bottom = 1.0;
            if (pos.segment_index == 0) {
                segment_bottom = breaks[0];
            } else if (pos.segment_index < static_cast<int>(breaks.size())) {
                segment_top = breaks[pos.segment_index - 1];
                segment_bottom = breaks[pos.segment_index];
            } else {
                segment_top = breaks.back();
            }
            effective_pdf_height = static_cast<float>(segment_bottom - segment_top) * pdf_height_points;
        }
    }

    if (zoom_mode) {
        Page full_page = document_->get_page(annotation->page_num_, false);
        float cropped_width_ratio =
            static_cast<float>(full_page.border.right - full_page.border.left) / full_page.width();
        float cropped_height_ratio =
            static_cast<float>(full_page.border.bottom - full_page.border.top) / full_page.height();
        effective_pdf_width = cropped_width_ratio * pdf_width_points;
        effective_pdf_height = cropped_height_ratio * effective_pdf_height;
    }

    float points_per_pixel_x = effective_pdf_width / page_display_width;
    float points_per_pixel_y = effective_pdf_height / page_display_height;

    float dx_points = dx_pixels * points_per_pixel_x;
    float dy_points = dy_pixels * points_per_pixel_y;

    // PDF y-axis is from bottom, so positive dy_pixels (down on screen) means negative dy_points
    float new_x = annotation->x_ + dx_points;
    float new_y = annotation->y_ - dy_points;

    document_->move_annotation_in_memory(selected_annotation_, new_x, new_y);
    selected_annotation_moved_ = true;
    update_image();
}


void PDFViewer::flush_pending_annotation_move()
{
    SAFE_METHOD;
    TRACE_CALL;

    if (!selected_annotation_moved_ || !selected_annotation_ || !document_)
        return;

    // Find the page number before saving
    int page_num = -1;
    for (const auto& ann : document_->annotations()) {
        if (ann.handle_ == selected_annotation_) {
            page_num = ann.page_num_;
            break;
        }
    }

    document_->save_moved_annotation(selected_annotation_, selected_annotation_original_x_,
                                     selected_annotation_original_y_);
    selected_annotation_moved_ = false;

    // Reload page so annotation renders in new position
    if (page_num > 0)
        document_->reload_page(page_num);
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
