#pragma once

#include <filesystem>
#include <vector>
#include <map>
#include <optional>

// Per-page paper boundary crop. top/bottom are normalized positions
// (0.0-1.0 of full page height); left/right are normalized positions
// (0.0-1.0 of full page width). When set, they override the auto-detected
// border in zoom-to-content rendering. Each field is independent and may
// be unset.
struct PaperCrop {
    std::optional<double> top;
    std::optional<double> bottom;
    std::optional<double> left;
    std::optional<double> right;

    bool empty() const { return !top && !bottom && !left && !right; }
};

// Manages performance-related data for sheet music performance mode.
//
// Stores page breaks that split physical PDF pages into multiple virtual pages
// for performance use. Break positions are stored as normalized coordinates (0.0-1.0)
// relative to page height, making them DPI-independent.
//
// Also stores optional per-page paper crops (top/bottom cutoff lines) used
// to exclude titles and footers when zoom-to-content is on.
//
// This is the in-memory model only. Persistence is handled by the
// document_info module, which stores this data as one section of the
// document's .mrd file.
class PerformanceData {
public:
    PerformanceData() = default;

    // Reads a legacy .perf file. Retained only for document_info's one-time
    // migration; remove together with that migration code.
    bool load(const std::filesystem::path& pdf_path);

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

    // Set the top paper crop line for a page. Normalized 0.0-1.0 of full page height.
    // Replaces any existing top crop for this page.
    void set_paper_crop_top(int page_num, double normalized_position);

    // Set the bottom paper crop line for a page. Normalized 0.0-1.0 of full page height.
    // Replaces any existing bottom crop for this page.
    void set_paper_crop_bottom(int page_num, double normalized_position);

    // Clear the top paper crop for a page (if any). If no crop remains on the
    // page, the page entry is erased entirely.
    void clear_paper_crop_top(int page_num);

    // Clear the bottom paper crop for a page (if any).
    void clear_paper_crop_bottom(int page_num);

    // Set the left paper crop line for a page. Normalized 0.0-1.0 of full page width.
    void set_paper_crop_left(int page_num, double normalized_position);

    // Set the right paper crop line for a page. Normalized 0.0-1.0 of full page width.
    void set_paper_crop_right(int page_num, double normalized_position);

    // Clear the left paper crop for a page (if any).
    void clear_paper_crop_left(int page_num);

    // Clear the right paper crop for a page (if any).
    void clear_paper_crop_right(int page_num);

    // Get the paper crop for a page, or nullptr if none exists.
    const PaperCrop* get_paper_crop(int page_num) const;

    // Full read access to the stored data, for serialization by document_info.
    const std::map<int, std::vector<double>>& all_page_breaks() const { return page_breaks_; }
    const std::map<int, PaperCrop>& all_paper_crops() const { return paper_crops_; }

    // Opaque metronome state JSON (compact single-line). Empty string means
    // "use defaults". Storage only — interpretation lives in the metronome lib.
    const std::string& metronome_state() const { return metronome_state_; }
    void set_metronome_state(const std::string& json) { metronome_state_ = json; }

    // Check if there is any playback data at all.
    bool empty() const { return page_breaks_.empty() && paper_crops_.empty() && metronome_state_.empty(); }

private:
    // Constructs the .perf filename from the PDF path (same name, .perf extension).
    std::filesystem::path get_filename(std::filesystem::path pdf_path) const;

    // Map from page number to list of break positions (normalized 0.0-1.0).
    // Each vector is kept sorted.
    std::map<int, std::vector<double>> page_breaks_;

    // Map from page number to its paper crop. Absent pages have no crop.
    std::map<int, PaperCrop> paper_crops_;

    std::string metronome_state_;

    // File format version for future compatibility.
    int version_ = 1;
};
