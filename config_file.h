#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include "json.hpp"
#include "utils.h"

// Enum for Theme
enum class Theme {
    Dark,
    Light
};

// Enum for LogLevel
enum class LogLevel {
    Normal,
    Diagnostic
};

// Enum for ToolbarLocation
enum class ToolbarLocation {
    Left = 0x1,   // matches Qt enum, though we don't assume that.
    Right = 0x2,
    Top = 0x4,
    Bottom = 0x8,
};

// Struct to represent each open document
struct OpenDocument {
    std::filesystem::path filename;
    int page;
    int page_count;

    // get filename as a string, cant use filename.string() because it's not 
    // UTF-8 in Windows.
    inline std::string u8filename() const
    {
#ifdef _WIN32
        return wide_to_utf8(filename.wstring());
#else
        return doc.filename.string(); // Linux/macOS paths are already UTF-8
#endif    
    }
};


// ConfigFile Class Definition
class ConfigFile {
private:
    int file_version_;
    bool restore_window_position_ = true;
    bool restore_documents_ = true;
    bool zoom_to_content_ = false;
    bool show_status_bar_ = true;
    bool show_menu_ = true;
    bool horiz_tabs_ = false;

    std::vector<OpenDocument> open_documents_;
    std::vector<std::filesystem::path> recent_documents_;
    std::vector<int> app_size_ {10, 10, 640, 480};
    ToolbarLocation toolbar_location_ = ToolbarLocation::Left;
    int page_view_count_ = 2;
    int open_tab_ = -1;
    int max_recent_documents_ = 20;
    int dpi_ = 96;
    bool allow_oversize_ = true;
    Theme theme_ = Theme::Dark;
    std::vector<int> fast_search_dialog_size_ = {100, 100, 480, 320};
    int border_margin_ = 10;
    std::filesystem::path music_directory_ = ".";
    LogLevel log_level_ = LogLevel::Normal;

public:

    void start_group_changes() { save_operation_enabled_ = false;}
    void end_group_changes() { save_operation_enabled_ = true; save(); }

    int file_version() const { return file_version_; }
    void set_file_version(int value) { file_version_ = value; save(); }

    bool restore_window_position() const { return restore_window_position_; }
    void set_restore_window_position(bool value) { restore_window_position_ = value; save(); }

    bool restore_documents() const { return restore_documents_; }
    void set_restore_documents(bool value) { restore_documents_ = value; save(); }

    bool zoom_to_content() const { return zoom_to_content_; }
    void set_zoom_to_content(bool value) { zoom_to_content_ = value; save(); }

    bool show_status_bar() const { return show_status_bar_; }
    void set_show_status_bar(bool value) { show_status_bar_ = value; save(); }

    bool show_menu() const { return show_menu_; }
    void set_show_menu(bool value) { show_menu_ = value; save(); }

    bool horiz_tabs() const { return horiz_tabs_; }
    void set_horiz_tabs(bool value) { horiz_tabs_ = value; save(); }

    const std::vector<OpenDocument> &open_documents() const { return open_documents_; }
    void set_open_documents(const std::vector<OpenDocument> &value) { open_documents_ = value; save(); }

    const std::vector<std::filesystem::path> &recent_documents() const { return recent_documents_; }
    void set_recent_documents(const std::vector<std::filesystem::path> &value) { recent_documents_ = value; save(); }

    // [x, y, sx, sy]
    const std::vector<int> &app_size() const { return app_size_; }
    void set_app_size(const std::vector<int> &value) { app_size_ = value; save(); }

    // Top, Bottom, Left, Right
    ToolbarLocation toolbar_location() const { return toolbar_location_; }
    void set_toolbar_location(ToolbarLocation value) { toolbar_location_ = value; save(); }

    // 1 or 2
    int page_view_count() const { return page_view_count_; }
    void set_page_view_count(int value) { page_view_count_ = value; save(); }

    // -1 or >=0
    int open_tab() const { return open_tab_; }
    void set_open_tab(int value) { open_tab_ = value; save(); }

    int max_recent_documents() const { return max_recent_documents_; }
    void set_max_recent_documents(int value) { max_recent_documents_ = value; save(); }

    int dpi() const { return dpi_; }
    void set_dpi(int value) { dpi_ = value; save(); }

    bool allow_oversize() const { return allow_oversize_; }
    void set_allow_oversize(bool value) { allow_oversize_ = value; save(); }

    Theme theme() const { return theme_; }
    void set_theme(Theme value) { theme_ = value; save(); }

    // [x, y, sx, sy]
    const std::vector<int> &fast_search_dialog_size() const { return fast_search_dialog_size_; }
    void set_fast_search_dialog_size(const std::vector<int> &value) { fast_search_dialog_size_ = value; save(); }

    int border_margin() const { return border_margin_; }
    void set_border_margin(int value) { border_margin_ = value; save(); }

    const std::filesystem::path &music_directory() const { return music_directory_; }
    void set_music_directory(const std::filesystem::path &value) { music_directory_ = value; save(); }

    LogLevel log_level() const { return log_level_; }
    void set_log_level(LogLevel value) { log_level_ = value; save(); }


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
    bool save_operation_enabled_ = true;

    // Helper functions for validation
    bool valid_window_rect(const std::vector<int> &vec) const;
    void remove_duplicate_documents();
    std::vector<std::filesystem::path> remove_duplicates(const std::vector<std::filesystem::path> &docs) const;
    bool remove_missing_documents();
    void remove_recent_in_open_documents();
    std::pair<std::vector<std::filesystem::path>, std::vector<std::filesystem::path>> verify_documents_exist(const std::vector<std::filesystem::path> &docs) const;
};



class ConfigFileGroupSave {
public:
    ConfigFileGroupSave(ConfigFile &config) : config_(config) { config.start_group_changes(); }
    ~ConfigFileGroupSave() { config_.end_group_changes(); }

private:
    ConfigFile &config_;
};
