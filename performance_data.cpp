#include "performance_data.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>


std::filesystem::path PerformanceData::get_filename(std::filesystem::path path) const
{
    path.replace_extension(".perf");
    return path;
}


bool PerformanceData::load(const std::filesystem::path &pdf_path)
{
    auto fname = get_filename(pdf_path);

    if (!std::filesystem::exists(fname))
        return false;

    std::ifstream file(fname);
    if (!file.is_open())
        return false;

    page_breaks_.clear();

    std::string line;
    bool in_page_breaks_section = false;

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
            in_page_breaks_section = true;
            continue;
        }

        if (line[0] == '[') {
            in_page_breaks_section = false;
            continue;
        }

        // Parse page break entries: "page: N, position: P"
        if (in_page_breaks_section) {
            std::istringstream iss(line);
            std::string token;
            int page_num = -1;
            double position = 0.0;

            // Split by comma, then parse key:value pairs
            while (std::getline(iss, token, ',')) {
                size_t colon = token.find(':');
                if (colon != std::string::npos) {
                    std::string key = token.substr(0, colon);
                    std::string value = token.substr(colon + 1);

                    // Trim whitespace
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);

                    if (key == "page")
                        page_num = std::stoi(value);
                    else if (key == "position")
                        position = std::stod(value);
                }
            }

            // Validate and add the break
            if (page_num >= 0 && position >= 0.0 && position <= 1.0)
                add_page_break(page_num, position);
        }
    }

    return true;
}


bool PerformanceData::save(const std::filesystem::path &pdf_path) const
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
        for (const auto &[page_num, positions] : page_breaks_) {
            for (double pos : positions)
                file << "page: " << page_num << ", position: " << pos << "\n";
        }
    }

    return true;
}


void PerformanceData::add_page_break(int page_num, double normalized_position)
{
    auto &breaks = page_breaks_[page_num];

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

    auto &breaks = it->second;
    // Use epsilon comparison since we're dealing with floating point
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(),
                                [normalized_position](double pos) {
        return std::abs(pos - normalized_position) < 1e-6;
    }), breaks.end());

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


const std::vector<double> &PerformanceData::get_page_breaks(int page_num) const
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
