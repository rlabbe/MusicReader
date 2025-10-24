#pragma once

#include <string>
#include <vector>
#include <array>
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
    Diagnostic,
    Trace
};

// Enum for ToolbarLocation
enum class ToolbarLocation {
    Left = 0x1,   // matches Qt enum, though we don't assume that.
    Right = 0x2,
    Top = 0x4,
    Bottom = 0x8,
};

enum class PageLocation {
    Left,
    Center
};


// Struct to represent each open document
struct OpenDocument {
    std::filesystem::path filename;
    int page;
    int page_count;
    int access_order;  // 1 = most recent, higher numbers = older
    int tab_order;     // 0 = leftmost tab, higher numbers = further right

    // get filename as a string, cant use filename.string() because it's not
    // UTF-8 in Windows.
    /*inline std::string u8filename() const
    {
#ifdef _WIN32
        return wide_to_utf8(filename.wstring());
#else
        return doc.filename.string(); // Linux/macOS paths are already UTF-8
#endif
    }*/
};


// ConfigFile Class Definition
class ConfigFile {
private:
    int file_version_;
    bool restore_window_position_;
    bool restore_documents_;
    bool zoom_to_content_;
    bool show_status_bar_;
    bool show_toolbar_;
    bool show_menu_;
    bool horiz_tabs_;
    bool allow_file_delete_;
    bool tour_has_run_;
    bool append_to_log_;

    bool hide_mouse_cursor_ = false;
    int mouse_hide_delay_secs_ = 3;


    std::vector<OpenDocument> open_documents_;
    std::vector<std::filesystem::path> recent_documents_;
    std::array<int, 4> app_size_;
    std::array<int, 4> dev_dialog_size_;
    ToolbarLocation toolbar_location_;
    PageLocation page_location_;
    int page_view_count_;
    int page_step_size_; // when in 2 page view, this is the number of pages to step on page up/down.
    int open_tab_;
    int max_recent_documents_;
    int save_cadence_secs_;
    int dpi_;
    bool allow_oversize_;
    Theme theme_;
    std::array<int, 4> fast_search_dialog_size_;
    int border_margin_;
    std::filesystem::path music_directory_;
    LogLevel log_level_;

public:

    explicit ConfigFile(bool reset_on_error = true);

    void start_group_changes() { save_operation_enabled_ = false; }
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

    bool show_toolbar() const { return show_toolbar_; }
    void set_show_toolbar(bool value) { show_toolbar_ = value; save(); }

    bool show_menu() const { return show_menu_; }
    void set_show_menu(bool value) { show_menu_ = value; save(); }

    bool horiz_tabs() const { return horiz_tabs_; }
    void set_horiz_tabs(bool value) { horiz_tabs_ = value; save(); }

    bool allow_file_delete() const { return allow_file_delete_; }
    void set_allow_file_delete(bool value) { allow_file_delete_ = value; save(); }

    const std::vector<OpenDocument> &open_documents() const { return open_documents_; }
    void set_open_documents(const std::vector<OpenDocument> &value);
    void add_new_document(const std::filesystem::path &filepath, int page, int page_count);

    bool in_dev_mode() const { return dev_mode_; }
    void set_dev_mode(bool value) { dev_mode_ = value; }

    const std::vector<std::filesystem::path> &recent_documents() const { return recent_documents_; }
    void set_recent_documents(const std::vector<std::filesystem::path> &value) { recent_documents_ = value; save(); }

    // [x, y, sx, sy]
    const std::array<int, 4> &app_size() const { return app_size_; }
    void set_app_size(const std::array<int, 4> &value) { app_size_ = value; save(); }

    // [x, y, sx, sy]
    const std::array<int, 4> &dev_dialog_size() const { return dev_dialog_size_; }
    void set_dev_size(const std::array<int, 4> &value) { dev_dialog_size_ = value; save(); }


    // Top, Bottom, Left, Right
    ToolbarLocation toolbar_location() const { return toolbar_location_; }
    void set_toolbar_location(ToolbarLocation value) { toolbar_location_ = value; save(); }

    PageLocation page_location() const { return page_location_; }
    void set_page_location(PageLocation value) { page_location_ = value; save(); }

    // 1 or 2
    int page_view_count() const { return page_view_count_; }
    void set_page_view_count(int value) { page_view_count_ = value; save(); }

    int page_step_size() const { return page_step_size_; }
    void set_page_step_size(int value) { page_step_size_ = value; save(); }
    void toggle_page_step_size() { page_step_size_ = (page_step_size_ == 1) ? 2 : 1; save(); }

    // -1 or >=0
    int open_tab() const { return open_tab_; }
    void set_open_tab(int value) { open_tab_ = value; save(); }

    int max_recent_documents() const { return max_recent_documents_; }
    void set_max_recent_documents(int value) { max_recent_documents_ = value; save(); }

    int save_cadence_secs() const { return save_cadence_secs_; }
    void set_save_cadence_secs(int value) { save_cadence_secs_ = value; save(); }

    int dpi() const { return dpi_; }
    void set_dpi(int value) { dpi_ = value; save(); }

    bool allow_oversize() const { return allow_oversize_; }
    void set_allow_oversize(bool value) { allow_oversize_ = value; save(); }

    Theme theme() const { return theme_; }
    void set_theme(Theme value) { theme_ = value; save(); }

    // [x, y, sx, sy]
    const std::array<int, 4> &fast_search_dialog_size() const { return fast_search_dialog_size_; }
    void set_fast_search_dialog_size(const std::array<int, 4> &value) { fast_search_dialog_size_ = value; save(); }

    int border_margin() const { return border_margin_; }
    void set_border_margin(int value) { border_margin_ = value; save(); }

    const std::filesystem::path &music_directory() const { return music_directory_; }
    void set_music_directory(const std::filesystem::path &value) { music_directory_ = value; save(); }

    LogLevel log_level() const { return log_level_; }
    void set_log_level(LogLevel value) { log_level_ = value; save(); }

    bool tour_has_run() const { return tour_has_run_; }
    void set_tour_has_run(bool value) { tour_has_run_ = value; save(); }

    bool append_to_log() const { return append_to_log_; }
    void set_append_to_log(bool value) { append_to_log_ = value; save(); }

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
    void update_document_access(const std::filesystem::path &filepath);

    // Utility methods
    void set_defaults();
    std::string repr() const;
    void filenames_to_os_convention();

private:
    nlohmann::json to_json() const;
    bool dev_mode_ = false;

    // Private member variables
    std::filesystem::path filename_;
    bool save_operation_enabled_ = true;

    // Helper functions for validation
    bool valid_window_rect(const std::array<int, 4> &vec) const;
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
