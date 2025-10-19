#pragma once

#include <filesystem>
#include <vector>
#include <map>

// Manages playback-related data for sheet music performance mode.
//
// Stores page breaks that split physical PDF pages into multiple virtual pages
// for performance use. Break positions are stored as normalized coordinates (0.0-1.0)
// relative to page height, making them DPI-independent.
//
// Data is persisted in a .pbk (playback) file alongside the PDF, using a simple
// text format that allows for future extensions (repeats, jumps, etc.).
//
// Example .pbk file format:
//   # Playback file for score.pdf
//   version: 1
//
//   [page_breaks]
//   page: 3, position: 0.65
//   page: 3, position: 0.85
//   page: 7, position: 0.42
class PerformanceData {
public:
    PerformanceData() = default;

    // Load playback data from the .pbk file associated with the given PDF path.
    // Returns true if file exists and was loaded successfully, false otherwise.
    // If no .pbk file exists, this is not an error - just means no playback data.
    bool load(const std::filesystem::path& pdf_path);

    // Save playback data to the .pbk file associated with the given PDF path.
    // If data is empty, still returns true (but creates no file).
    // Returns false only on write errors.
    bool save(const std::filesystem::path& pdf_path) const;

    // Add a page break at the specified normalized position (0.0-1.0) on the page.
    // Breaks are automatically kept sorted. Duplicate positions are ignored.
    void add_page_break(int page_num, double normalized_position);

    // Remove a specific page break at the given position (uses epsilon comparison).
    void remove_page_break(int page_num, double normalized_position);

    // Remove all page breaks for the specified page.
    void remove_page_breaks(int page_num);

    // Clear all playback data.
    void clear();

    // Get all page breaks for a specific page, sorted in ascending order.
    // Returns empty vector if no breaks exist for this page.
    const std::vector<double>& get_page_breaks(int page_num) const;

    // Check if a specific page has any breaks.
    bool has_breaks(int page_num) const;

    // Check if there is any playback data at all.
    bool empty() const { return page_breaks_.empty(); }

private:
    // Constructs the .pbk filename from the PDF path (same name, .pbk extension).
    std::filesystem::path get_playback_file_path(const std::filesystem::path& pdf_path) const;

    // Map from page number to list of break positions (normalized 0.0-1.0).
    // Each vector is kept sorted.
    std::map<int, std::vector<double>> page_breaks_;

    // File format version for future compatibility.
    int version_ = 1;
};
