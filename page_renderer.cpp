#include "page_renderer.h"
#include "document.h"
#include "performance_mode.h"
#include <algorithm>


PageRenderer::PageRenderer(Document* document)
    : document_(document)
    , current_physical_page_(1)
    , current_segment_index_(0)
{
}


Page PageRenderer::get_current_page() const
{
    if (!document_)
        return Page(1);

    Page page = document_->get_page(current_physical_page_, true);

    if (PerformanceMode::is_performance()) {
        int seg_count = segment_count(current_physical_page_);
        if (seg_count > 1)
            page = crop_page(page, current_physical_page_, current_segment_index_);
    }

    return page;
}


void PageRenderer::next()
{
    if (!can_go_next())
        return;

    if (PerformanceMode::get() == PerformanceMode::Mode::Normal) {
        current_physical_page_++;
        current_segment_index_ = 0;
    } else {
        // Performance mode: check if there are more segments on current page
        int seg_count = segment_count(current_physical_page_);
        if (current_segment_index_ + 1 < seg_count) {
            current_segment_index_++;
        } else {
            // Move to next physical page
            current_physical_page_++;
            current_segment_index_ = 0;
        }
    }
}


void PageRenderer::prev()
{
    if (!can_go_prev())
        return;

    if (PerformanceMode::get() == PerformanceMode::Mode::Normal) {
        current_physical_page_--;
        current_segment_index_ = 0;
    } else {
        // Performance mode: check if we're not on first segment
        if (current_segment_index_ > 0) {
            current_segment_index_--;
        } else {
            // Move to previous physical page, last segment
            current_physical_page_--;
            int seg_count = segment_count(current_physical_page_);
            current_segment_index_ = seg_count - 1;
        }
    }
}


void PageRenderer::goto_page(int physical_page) const
{
    if (!document_)
        return;

    int total = total_pages();
    if (physical_page < 1 || physical_page > total)
        return;

    current_physical_page_ = physical_page;
    current_segment_index_ = 0;
}


std::string PageRenderer::current_page_display() const
{
    if (PerformanceMode::get() == PerformanceMode::Mode::Normal)
        return std::to_string(current_physical_page_);

    return format_page_display(current_physical_page_, current_segment_index_);
}


int PageRenderer::total_pages() const
{
    return document_ ? document_->page_count() : 0;
}


bool PageRenderer::can_go_next() const
{
    if (!document_)
        return false;

    int total = total_pages();
    if (current_physical_page_ >= total)
        return false;

    if (PerformanceMode::get() == PerformanceMode::Mode::Normal)
        return true;

    // Performance mode: check if on last segment of last page
    int seg_count = segment_count(current_physical_page_);
    if (current_physical_page_ == total && current_segment_index_ >= seg_count - 1)
        return false;

    return true;
}


bool PageRenderer::can_go_prev() const
{
    if (!document_)
        return false;

    if (current_physical_page_ <= 1 && current_segment_index_ <= 0)
        return false;

    return true;
}


int PageRenderer::segment_count(int physical_page) const
{
    if (!document_ || PerformanceMode::get() != PerformanceMode::Mode::Performance)
        return 1;

    const auto& breaks = document_->performance_data().get_page_breaks(physical_page);
    // Number of segments = number of breaks + 1
    return static_cast<int>(breaks.size()) + 1;
}


Page PageRenderer::crop_page(const Page& page, int physical_page, int segment_index) const
{
    if (!document_)
        return page;

    const auto& breaks = document_->performance_data().get_page_breaks(physical_page);
    if (breaks.empty())
        return page;

    int height = page.height();
    if (height == 0)
        return page;

    // Calculate crop region based on segment index
    double top_normalized = 0.0;
    double bottom_normalized = 1.0;

    if (segment_index == 0) {
        // First segment: top to first break
        top_normalized = 0.0;
        bottom_normalized = breaks[0];
    } else if (segment_index < static_cast<int>(breaks.size())) {
        // Middle segment: from break[i-1] to break[i]
        top_normalized = breaks[segment_index - 1];
        bottom_normalized = breaks[segment_index];
    } else {
        // Last segment: from last break to bottom
        top_normalized = breaks.back();
        bottom_normalized = 1.0;
    }

    // Convert normalized coordinates to pixel coordinates
    int top_pixel = static_cast<int>(top_normalized * height);
    int bottom_pixel = static_cast<int>(bottom_normalized * height);
    int crop_height = bottom_pixel - top_pixel;

    if (crop_height <= 0)
        return page;

    // Crop the QImage
    QImage cropped = page.img.copy(0, top_pixel, page.width(), crop_height);

    // Create new Page with cropped image
    Page result(cropped, page.page_num, page.double_page);
    return result;
}


std::string PageRenderer::format_page_display(int physical_page, int segment_index) const
{
    int seg_count = segment_count(physical_page);
    if (seg_count == 1)
        return std::to_string(physical_page);

    // Convert segment index to letter: 0='a', 1='b', etc.
    char letter = 'a' + static_cast<char>(segment_index);
    return std::to_string(physical_page) + letter;
}
