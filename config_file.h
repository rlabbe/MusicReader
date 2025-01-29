// ConfigFile.hpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include "json.hpp"


// Enum for Theme
enum class Theme
{
    Dark,
    Light
};

// Enum for LogLevel
enum class LogLevel
{
    Normal,
    Diagnostic
};

// Enum for ToolbarLocation
enum class ToolbarLocation
{
    Left = 0x1,   // matches Qt enum, though we don't assume that.
    Right = 0x2,
    Top = 0x4,
    Bottom = 0x8,
};

// Struct to represent each open document
struct OpenDocument
{
    std::filesystem::path filename;
    int page;
    int page_count;
};

// ConfigFile Class Definition
class ConfigFile
{
public:
    // Public member variables corresponding to JSON fields
    int file_version;
    bool restore_window_position;
    bool restore_documents;
    bool zoom_to_content;
    std::vector<OpenDocument> open_documents;
    std::vector<std::filesystem::path> recent_documents;
    std::vector<int> app_size; // [x, y, sx, sy]
    ToolbarLocation toolbar_location; // Top, Bottom, Left, Right
    int page_view_count; // 1 or 2
    int open_tab; // -1 or >=0
    int max_recent_documents;
    int dpi;
    bool allow_oversize;
    Theme theme; // Dark or Light
    std::vector<int> fast_search_dialog_size; // [x, y, sx, sy]
    int border_margin;
    std::filesystem::path music_directory;
    LogLevel log_level; // Normal or Diagnostic

    // Constructor
    explicit ConfigFile(bool reset_on_error = true);

    // Method to read configuration from file
    void read(bool reset_on_error = true);

    // Method to save configuration to file
    void save() const;

    // Validation methods
    bool validate() const;
    bool fix();

    // Document management methods
    void add_recent_document(const std::filesystem::path &path);
    void remove_recent_document(const std::filesystem::path &path);
    void remove_recent_documents(const std::vector<std::filesystem::path> &paths);

    // Utility methods
    void set_defaults();
    std::string repr() const;
    void filenames_to_os_convention();

private:
    nlohmann::json to_json() const;

    // Private member variables
    std::filesystem::path filename_;
    bool save_operation_enabled_;

    // Helper functions for validation
    bool valid_window_rect(const std::vector<int> &vec) const;
    void remove_duplicate_documents();
    std::vector<std::filesystem::path> remove_duplicates(const std::vector<std::filesystem::path> &docs) const;
    bool remove_missing_documents();
    void remove_recent_in_open_documents();
    std::pair<std::vector<std::filesystem::path>, std::vector<std::filesystem::path>> verify_documents_exist(const std::vector<std::filesystem::path> &docs) const;
};
