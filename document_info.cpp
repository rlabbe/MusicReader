#include "document_info.h"
#include "json.hpp"
#include "logger.h"
#include <fstream>


namespace {

constexpr int INFO_FILE_VERSION = 1;


// Reads and parses the .mrd file for a PDF. Returns an empty object when the
// file is absent or cannot be parsed, so callers treat "no file" and "no
// data" identically.
nlohmann::json read_info_file(const std::filesystem::path& pdf_path)
{
    auto path = document_info::path_for(pdf_path);
    if (!std::filesystem::exists(path))
        return nlohmann::json::object();

    std::ifstream file(path);
    if (!file.is_open()) {
        logger::error("Failed to open info file: {}", path.string());
        return nlohmann::json::object();
    }

    try {
        nlohmann::json j;
        file >> j;
        if (j.is_object())
            return j;
    } catch (const std::exception& e) {
        logger::error("Failed to parse info file {}: {}", path.string(), e.what());
    }
    return nlohmann::json::object();
}


// Writes the .mrd file. Skips writing when there is no data section and no
// file already exists, so empty info files are never created.
bool write_info_file(const std::filesystem::path& pdf_path, nlohmann::json& j)
{
    auto path = document_info::path_for(pdf_path);

    bool has_data = false;
    for (const char* section : {"performance", "bookmarks", "favorite"}) {
        auto it = j.find(section);
        if (it != j.end() && !it->empty())
            has_data = true;
    }
    if (!has_data && !std::filesystem::exists(path))
        return true;

    j["version"] = INFO_FILE_VERSION;

    std::ofstream file(path);
    if (!file.is_open()) {
        logger::error("Failed to write info file: {}", path.string());
        return false;
    }
    file << j.dump(4) << "\n";
    return true;
}

} // namespace


std::filesystem::path document_info::path_for(const std::filesystem::path& pdf_path)
{
    auto path = pdf_path;
    path.replace_extension(".mrd");
    return path;
}


PerformanceData document_info::read_performance(const std::filesystem::path& pdf_path)
{
    PerformanceData performance;

    nlohmann::json j = read_info_file(pdf_path);
    auto section = j.find("performance");
    if (section == j.end() || !section->is_object())
        return performance;

    try {
        if (auto breaks = section->find("page_breaks"); breaks != section->end() && breaks->is_array()) {
            for (const auto& entry : *breaks) {
                if (entry.contains("page") && entry.contains("position"))
                    performance.add_page_break(entry["page"].get<int>(), entry["position"].get<double>());
            }
        }

        if (auto crops = section->find("paper_crops"); crops != section->end() && crops->is_array()) {
            for (const auto& entry : *crops) {
                if (!entry.contains("page"))
                    continue;
                int page = entry["page"].get<int>();
                if (entry.contains("top"))
                    performance.set_paper_crop_top(page, entry["top"].get<double>());
                if (entry.contains("bottom"))
                    performance.set_paper_crop_bottom(page, entry["bottom"].get<double>());
                if (entry.contains("left"))
                    performance.set_paper_crop_left(page, entry["left"].get<double>());
                if (entry.contains("right"))
                    performance.set_paper_crop_right(page, entry["right"].get<double>());
            }
        }

        if (auto metro = section->find("metronome"); metro != section->end() && metro->is_string())
            performance.set_metronome_state(metro->get<std::string>());
    } catch (const std::exception& e) {
        logger::error("Malformed performance section in {}: {}", path_for(pdf_path).string(), e.what());
    }

    return performance;
}


bool document_info::write_performance(const std::filesystem::path& pdf_path, const PerformanceData& performance)
{
    nlohmann::json j = read_info_file(pdf_path);

    if (performance.empty()) {
        j.erase("performance");
        return write_info_file(pdf_path, j);
    }

    nlohmann::json section = nlohmann::json::object();

    nlohmann::json breaks = nlohmann::json::array();
    for (const auto& [page, positions] : performance.all_page_breaks()) {
        for (double position : positions)
            breaks.push_back({
                {    "page",     page},
                {"position", position}
            });
    }
    if (!breaks.empty())
        section["page_breaks"] = breaks;

    nlohmann::json crops = nlohmann::json::array();
    for (const auto& [page, crop] : performance.all_paper_crops()) {
        if (crop.empty())
            continue;
        nlohmann::json entry = {
            {"page", page}
        };
        if (crop.top)
            entry["top"] = *crop.top;
        if (crop.bottom)
            entry["bottom"] = *crop.bottom;
        if (crop.left)
            entry["left"] = *crop.left;
        if (crop.right)
            entry["right"] = *crop.right;
        crops.push_back(entry);
    }
    if (!crops.empty())
        section["paper_crops"] = crops;

    if (!performance.metronome_state().empty())
        section["metronome"] = performance.metronome_state();

    j["performance"] = section;
    return write_info_file(pdf_path, j);
}


std::vector<Bookmark> document_info::read_bookmarks(const std::filesystem::path& pdf_path)
{
    nlohmann::json j = read_info_file(pdf_path);
    auto section = j.find("bookmarks");
    if (section == j.end() || !section->is_array())
        return {};
    return json_to_bookmark(section->dump());
}


bool document_info::write_bookmarks(const std::filesystem::path& pdf_path, const std::vector<Bookmark>& bookmarks)
{
    nlohmann::json j = read_info_file(pdf_path);

    if (bookmarks.empty()) {
        j.erase("bookmarks");
        return write_info_file(pdf_path, j);
    }

    std::vector<Bookmark> copy = bookmarks; // to_json takes a non-const reference
    try {
        j["bookmarks"] = nlohmann::json::parse(to_json(copy));
    } catch (const std::exception& e) {
        logger::error("Failed to serialize bookmarks for {}: {}", path_for(pdf_path).string(), e.what());
        return false;
    }
    return write_info_file(pdf_path, j);
}


bool document_info::has_bookmarks(const std::filesystem::path& pdf_path)
{
    nlohmann::json j = read_info_file(pdf_path);
    auto section = j.find("bookmarks");
    return section != j.end() && section->is_array() && !section->empty();
}


bool document_info::is_favorite(const std::filesystem::path& pdf_path)
{
    nlohmann::json j = read_info_file(pdf_path);
    auto it = j.find("favorite");
    return it != j.end() && it->is_boolean() && it->get<bool>();
}


bool document_info::set_favorite(const std::filesystem::path& pdf_path, bool favorite)
{
    nlohmann::json j = read_info_file(pdf_path);
    if (favorite)
        j["favorite"] = true;
    else
        j.erase("favorite");
    return write_info_file(pdf_path, j);
}
