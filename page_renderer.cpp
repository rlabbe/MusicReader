#include "page_renderer.h"
#include "document.h"
#include "performance_mode.h"
#include <algorithm>

PageRenderer::PageRenderer(Document* document)
    : document_(document)
    , current_index_(1)
{
}


int PageRenderer::page_count() const
{
    if (!document_)
        return 0;

    int count = 0;
    int total_physical = physical_page_count();

    for (int page = 1; page <= total_physical; ++page)
        count += segment_count(page);

    logger::debug("document({}) has {} pages", document_->id, count);
    return count;
}


int PageRenderer::current_index() const
{
    return current_index_;
}


std::string PageRenderer::current_page_display() const
{
    Position pos = index_to_position(current_index_);
    return format_display(pos.physical_page, pos.segment_index);
}


std::vector<std::string> PageRenderer::get_all_page_displays() const
{
    std::vector<std::string> displays;

    if (!document_)
        return displays;

    int total = page_count();
    for (int i = 1; i <= total; ++i) {
        Position pos = index_to_position(i);
        displays.push_back(format_display(pos.physical_page, pos.segment_index));
    }

    return displays;
}


void PageRenderer::next()
{
    if (can_go_next())
        current_index_++;
}


void PageRenderer::prev()
{
    if (can_go_prev())
        current_index_--;
}


bool PageRenderer::can_go_next() const
{
    return current_index_ < page_count();
}


bool PageRenderer::can_go_prev() const
{
    return current_index_ > 1;
}


void PageRenderer::goto_index(int index)
{
    int count = page_count();
    if (index >= 1 && index <= count)
        current_index_ = index;
}


void PageRenderer::goto_physical_page(int physical_page)
{
    int total = physical_page_count();
    if (physical_page < 1 || physical_page > total)
        return;

    current_index_ = position_to_index(physical_page, 0);
}


Page PageRenderer::get_current_page() const
{
    if (!document_)
        return Page(1);

    Position pos = index_to_position(current_index_);
    Page page = document_->get_page(pos.physical_page, true);

    if (PerformanceMode::is_performance()) {
        int seg_count = segment_count(pos.physical_page);
        if (seg_count > 1)
            page = crop_page(page, pos.physical_page, pos.segment_index);
    }

    return page;
}


PageRenderer::Position PageRenderer::index_to_position(int index) const
{
    Position pos {1, 0};

    if (!document_)
        return pos;

    // Convert 1-based index to 0-based for calculation
    int zero_based = index - 1;
    if (zero_based < 0)
        return pos;

    int accumulated = 0;
    int total_physical = physical_page_count();

    for (int page = 1; page <= total_physical; ++page) {
        int seg_count = segment_count(page);

        if (accumulated + seg_count > zero_based) {
            pos.physical_page = page;
            pos.segment_index = zero_based - accumulated;
            return pos;
        }

        accumulated += seg_count;
    }

    return pos;
}


int PageRenderer::position_to_index(int physical_page, int segment_index) const
{
    if (!document_)
        return 1;

    int index = 0;
    int total_physical = physical_page_count();

    for (int page = 1; page < physical_page && page <= total_physical; ++page)
        index += segment_count(page);

    index += segment_index;

    return index + 1; // Convert to 1-based
}


int PageRenderer::segment_count(int physical_page) const
{
    if (!document_ || !PerformanceMode::is_performance())
        return 1;

    const auto& breaks = document_->performance_data().get_page_breaks(physical_page);
    return static_cast<int>(breaks.size()) + 1;
}


int PageRenderer::physical_page_count() const
{
    return document_ ? document_->page_count() : 0;
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

    double top_normalized = 0.0;
    double bottom_normalized = 1.0;

    if (segment_index == 0) {
        top_normalized = 0.0;
        bottom_normalized = breaks[0];
    } else if (segment_index < static_cast<int>(breaks.size())) {
        top_normalized = breaks[segment_index - 1];
        bottom_normalized = breaks[segment_index];
    } else {
        top_normalized = breaks.back();
        bottom_normalized = 1.0;
    }

    int top_pixel = static_cast<int>(top_normalized * height);
    int bottom_pixel = static_cast<int>(bottom_normalized * height);
    int crop_height = bottom_pixel - top_pixel;
    if (crop_height <= 0)
        return page;

    QImage cropped = page.img.copy(0, top_pixel, page.width(), crop_height);

    Page result(cropped, page.page_num, page.double_page);
    return result;
}


std::string PageRenderer::format_display(int physical_page, int segment_index) const
{
    int seg_count = segment_count(physical_page);
    if (seg_count == 1)
        return std::to_string(physical_page);

    char letter = 'a' + static_cast<char>(segment_index);
    return std::to_string(physical_page) + letter;
}


Page PageRenderer::get_page_at_index(int index) const
{
    if (!document_)
        return Page(1);

    int count = page_count();
    if (index < 1 || index > count)
        return Page(1);

    Position pos = index_to_position(index);
    Page page = document_->get_page(pos.physical_page, false);

    if (PerformanceMode::is_performance()) {
        int seg_count = segment_count(pos.physical_page);
        if (seg_count > 1)
            page = crop_page(page, pos.physical_page, pos.segment_index);
    }

    return page;
}
