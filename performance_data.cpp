#include "performance_data.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>


std::filesystem::path PerformanceData::get_filename(std::filesystem::path path) const
{
    path.replace_extension(".perf");
    return path;
}


bool PerformanceData::load(const std::filesystem::path& pdf_path)
{
    auto fname = get_filename(pdf_path);

    if (!std::filesystem::exists(fname))
        return false;

    std::ifstream file(fname);
    if (!file.is_open())
        return false;

    page_breaks_.clear();
    paper_crops_.clear();

    enum class Section { None, PageBreaks, PaperCrops };
    Section section = Section::None;

    std::string line;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#')
            continue;

        // Parse version line
        if (line.find("version:") == 0) {
            std::istringstream iss(line.substr(8));
            iss >> version_;
            continue;
        }

        // Section headers start with '['
        if (line == "[page_breaks]") {
            section = Section::PageBreaks;
            continue;
        }
        if (line == "[paper_crops]") {
            section = Section::PaperCrops;
            continue;
        }
        if (line[0] == '[') {
            section = Section::None;
            continue;
        }

        // Parse key:value pairs shared by both sections.
        std::istringstream iss(line);
        std::string token;
        int page_num = -1;
        double position = 0.0;
        std::optional<double> crop_top;
        std::optional<double> crop_bottom;

        while (std::getline(iss, token, ',')) {
            size_t colon = token.find(':');
            if (colon == std::string::npos)
                continue;

            std::string key = token.substr(0, colon);
            std::string value = token.substr(colon + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (key == "page")
                page_num = std::stoi(value);
            else if (key == "position")
                position = std::stod(value);
            else if (key == "top")
                crop_top = std::stod(value);
            else if (key == "bottom")
                crop_bottom = std::stod(value);
        }

        if (section == Section::PageBreaks) {
            if (page_num >= 0 && position >= 0.0 && position <= 1.0)
                add_page_break(page_num, position);
        } else if (section == Section::PaperCrops) {
            if (page_num < 0)
                continue;
            // Validate individual bounds, and reject a crop where top >= bottom.
            if (crop_top && (*crop_top < 0.0 || *crop_top > 1.0))
                crop_top.reset();
            if (crop_bottom && (*crop_bottom < 0.0 || *crop_bottom > 1.0))
                crop_bottom.reset();
            if (crop_top && crop_bottom && *crop_top >= *crop_bottom) {
                logger::warning("Paper crop for page {} has top >= bottom; dropping both", page_num);
                continue;
            }
            if (crop_top)
                set_paper_crop_top(page_num, *crop_top);
            if (crop_bottom)
                set_paper_crop_bottom(page_num, *crop_bottom);
        }
    }

    return true;
}


bool PerformanceData::save(const std::filesystem::path& pdf_path) const
{
    if (empty())
        return true;

    auto fname = get_filename(pdf_path);
    std::ofstream file(fname);
    if (!file.is_open())
        return false;

    file << "# Playback file for " << pdf_path.filename().string() << "\n";
    file << "version: " << version_ << "\n\n";

    if (!page_breaks_.empty()) {
        file << "[page_breaks]\n";
        for (const auto& [page_num, positions] : page_breaks_) {
            for (double pos : positions)
                file << "page: " << page_num << ", position: " << pos << "\n";
        }
        file << "\n";
    }

    if (!paper_crops_.empty()) {
        file << "[paper_crops]\n";
        for (const auto& [page_num, crop] : paper_crops_) {
            if (!crop.top && !crop.bottom)
                continue;
            file << "page: " << page_num;
            if (crop.top)
                file << ", top: " << *crop.top;
            if (crop.bottom)
                file << ", bottom: " << *crop.bottom;
            file << "\n";
        }
    }

    return true;
}


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
    if (!it->second.top && !it->second.bottom)
        paper_crops_.erase(it);
}


void PerformanceData::clear_paper_crop_bottom(int page_num)
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return;

    it->second.bottom.reset();
    if (!it->second.top && !it->second.bottom)
        paper_crops_.erase(it);
}


const PaperCrop* PerformanceData::get_paper_crop(int page_num) const
{
    auto it = paper_crops_.find(page_num);
    if (it == paper_crops_.end())
        return nullptr;
    return &it->second;
}
