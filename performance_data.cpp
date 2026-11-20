#include "performance_data.h"
#include <algorithm>
#include <cmath>


void PerformanceData::add_page_break(int page_num, double normalized_position)
{
    auto& breaks = page_breaks_[page_num];

    if (std::find(breaks.begin(), breaks.end(), normalized_position) == breaks.end()) {
        breaks.push_back(normalized_position);
        std::sort(breaks.begin(), breaks.end());
    }
}


void PerformanceData::remove_page_break(int page_num, double normalized_position)
{
    auto it = page_breaks_.find(page_num);
    if (it == page_breaks_.end())
        return;

    auto& breaks = it->second;
    // Use epsilon comparison since we're dealing with floating point
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(),
                                [normalized_position](double pos) {
                                    return std::abs(pos - normalized_position) < 1e-6;
                                }),
                 breaks.end());

    // Remove the page entry entirely if no breaks remain
    if (breaks.empty())
        page_breaks_.erase(it);
}


void PerformanceData::remove_page_breaks(int page_num)
{
    page_breaks_.erase(page_num);
}


void PerformanceData::clear()
{
    page_breaks_.clear();
}


const std::vector<double>& PerformanceData::get_page_breaks(int page_num) const
{
    static const std::vector<double> empty;
    auto it = page_breaks_.find(page_num);
    return (it != page_breaks_.end()) ? it->second : empty;
}


bool PerformanceData::has_breaks(int page_num) const
{
    auto it = page_breaks_.find(page_num);
    return it != page_breaks_.end() && !it->second.empty();
}


void PerformanceData::set_paper_crop_top(int page_num, double normalized_position)
{
    paper_crops_[page_num].top = normalized_position;
}


void PerformanceData::set_paper_crop_bottom(int page_num, double normalized_position)
{
    paper_crops_[page_num].bottom = normalized_position;
}


void PerformanceData::clear_paper_crop_top(int page_num)
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return;

    it->second.top.reset();
    if (it->second.empty())
        paper_crops_.erase(it);
}


void PerformanceData::clear_paper_crop_bottom(int page_num)
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return;

    it->second.bottom.reset();
    if (it->second.empty())
        paper_crops_.erase(it);
}


void PerformanceData::set_paper_crop_left(int page_num, double normalized_position)
{
    paper_crops_[page_num].left = normalized_position;
}


void PerformanceData::set_paper_crop_right(int page_num, double normalized_position)
{
    paper_crops_[page_num].right = normalized_position;
}


void PerformanceData::clear_paper_crop_left(int page_num)
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return;

    it->second.left.reset();
    if (it->second.empty())
        paper_crops_.erase(it);
}


void PerformanceData::clear_paper_crop_right(int page_num)
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return;

    it->second.right.reset();
    if (it->second.empty())
        paper_crops_.erase(it);
}


const PaperCrop* PerformanceData::get_paper_crop(int page_num) const
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return nullptr;
    return &it->second;
}
