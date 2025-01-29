#include "config_file.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include "logger.h"
#include "exception_logger.h"

using namespace logger;
// For convenience
using json = nlohmann::json;


std::filesystem::path path_to_os_convention(const std::filesystem::path &path)
{
    try {
        return std::filesystem::canonical(std::filesystem::absolute(path));
    } catch (const std::filesystem::filesystem_error &e) {
        log_error("Error canonicalizing path: " + std::string(e.what()));
        return std::filesystem::absolute(path);
    }
}


// Implementation of get_persistent_config_path
std::filesystem::path get_persistent_config_path(const std::string &filename)
{
#ifdef _WIN32
    // Retrieve the APPDATA environment variable on Windows
    wchar_t *wappdata = nullptr;
    size_t len = 0;
    _wdupenv_s(&wappdata, &len, L"APPDATA");
    if (wappdata) {
        // Construct the path: %APPDATA%\MusicReader\filename
        std::filesystem::path path = std::filesystem::path(wappdata) / L"MusicReader" / std::filesystem::path(filename);
        free(wappdata);
        return path;
    }
#else
    // Retrieve the HOME environment variable on Unix-like systems
    const char *home = std::getenv("HOME");
    if (home) {
        // Construct the path: $HOME/.config/MusicReader/filename
        return std::filesystem::path(home) / ".config" / "MusicReader" / filename;
    }
#endif
    // Fallback to the current working directory if environment variables are not set
    return std::filesystem::current_path() / filename;
}

// Implementation of point_to_same_file
bool point_to_same_file(const std::filesystem::path &path1, const std::filesystem::path &path2)
{
    try {
        return std::filesystem::equivalent(path1, path2);
    } catch (const std::filesystem::filesystem_error &e) {
        log_error("Error comparing files '" + path1.string() + "' and '" + path2.string() + "': " + e.what());
        return false;
    }
}

// Helper functions to convert enums to strings
static std::string theme_to_string(Theme theme)
{
    switch (theme) {
    case Theme::Dark:
        return "dark";
    case Theme::Light:
        return "light";
    default:
        return "dark";
    }
}

static bool string_to_theme(const std::string &str, Theme &theme)
{
    if (str == "dark") {
        theme = Theme::Dark;
        return true;
    } else if (str == "light") {
        theme = Theme::Light;
        return true;
    }
    return false;
}

static std::string log_level_to_string(LogLevel level)
{
    switch (level) {
    case LogLevel::Normal:
        return "normal";
    case LogLevel::Diagnostic:
        return "diagnostic";
    default:
        return "normal";
    }
}

static bool string_to_log_level(const std::string &str, LogLevel &level)
{
    if (str == "normal") {
        level = LogLevel::Normal;
        return true;
    } else if (str == "diagnostic") {
        level = LogLevel::Diagnostic;
        return true;
    }
    return false;
}

static std::string toolbar_location_to_string(ToolbarLocation location)
{
    switch (location) {
    case ToolbarLocation::Top:
        return "top";
    case ToolbarLocation::Bottom:
        return "bottom";
    case ToolbarLocation::Left:
        return "left";
    case ToolbarLocation::Right:
        return "right";
    default:
        return "left";
    }
}

static bool string_to_toolbar_location(const std::string &str, ToolbarLocation &location)
{
    if (str == "top") {
        location = ToolbarLocation::Top;
        return true;
    } else if (str == "bottom") {
        location = ToolbarLocation::Bottom;
        return true;
    } else if (str == "left") {
        location = ToolbarLocation::Left;
        return true;
    } else if (str == "right") {
        location = ToolbarLocation::Right;
        return true;
    }
    return false;
}

// Constructor
ConfigFile::ConfigFile(bool reset_on_error)
    : filename_(get_persistent_config_path("MusicReader.config")),
    save_operation_enabled_(true)
{
    read(reset_on_error);
}



// Method to read configuration from file with improved error handling
void ConfigFile::read(bool reset_on_error)
{
    bool valid = false; // Flag to indicate successful parsing
    json j;

    std::ifstream infile(filename_);
    if (!infile.is_open()) {
        log_error("Config file not found: " + filename_.string());
        goto CLEANUP;
    }

    try {
        infile >> j;
    } catch (const json::parse_error &e) {
        log_error("JSON parse error: " + std::string(e.what()));
        goto CLEANUP;
    }

    infile.close();

    // Begin parsing each key with specific error checks
    // file_version
    if (j.contains("file_version") && j["file_version"].is_number_integer()) {
        file_version = j["file_version"].get<int>();
    } else {
        log_error("Invalid or missing 'file_version'");
        goto CLEANUP;
    }

    // restore_window_position
    if (j.contains("restore_window_position") && j["restore_window_position"].is_boolean()) {
        restore_window_position = j["restore_window_position"].get<bool>();
    } else {
        log_error("Invalid or missing 'restore_window_position'");
        goto CLEANUP;
    }

    // restore_documents
    if (j.contains("restore_documents") && j["restore_documents"].is_boolean()) {
        restore_documents = j["restore_documents"].get<bool>();
    } else {
        log_error("Invalid or missing 'restore_documents'");
        goto CLEANUP;
    }

    // zoom_to_content
    if (j.contains("zoom_to_content") && j["zoom_to_content"].is_boolean()) {
        zoom_to_content = j["zoom_to_content"].get<bool>();
    } else {
        log_error("Invalid or missing 'zoom_to_content'");
        goto CLEANUP;
    }

    // open_documents
    if (j.contains("open_documents") && j["open_documents"].is_array()) {
        open_documents.clear();
        for (const auto &doc : j["open_documents"]) {
            if (doc.contains("filename") && doc["filename"].is_string() &&
                doc.contains("page") && doc["page"].is_number_integer() &&
                doc.contains("page_count") && doc["page_count"].is_number_integer()) {

                OpenDocument od;
                od.filename = std::filesystem::path(doc["filename"].get<std::string>());
                od.page = doc["page"].get<int>();
                od.page_count = doc["page_count"].get<int>();
                open_documents.push_back(od);
            } else {
                log_error("Invalid entry in 'open_documents'");
                goto CLEANUP;
            }
        }
    } else {
        log_error("Invalid or missing 'open_documents'");
        goto CLEANUP;
    }

    // recent_documents
    if (j.contains("recent_documents") && j["recent_documents"].is_array()) {
        recent_documents.clear();
        for (const auto &path : j["recent_documents"]) {
            if (path.is_string()) {
                recent_documents.emplace_back(std::filesystem::path(path.get<std::string>()));
            } else {
                log_error("Invalid entry in 'recent_documents'");
                goto CLEANUP;
            }
        }
    } else {
        log_error("Invalid or missing 'recent_documents'");
        goto CLEANUP;
    }

    // app_size
    if (j.contains("app_size") && j["app_size"].is_array() && j["app_size"].size() == 4) {
        bool app_size_valid = true;
        app_size.clear();
        for (const auto &size : j["app_size"]) {
            if (size.is_number_integer()) {
                app_size.push_back(size.get<int>());
            } else {
                app_size_valid = false;
                break;
            }
        }
        if (!app_size_valid) {
            log_error("Invalid entries in 'app_size'");
            goto CLEANUP;
        }
    } else {
        log_error("Invalid or missing 'app_size'");
        goto CLEANUP;
    }

    // toolbar_location
    if (j.contains("toolbar_location") && j["toolbar_location"].is_number_integer()) {
        int toolbar_int = j["toolbar_location"].get<int>();
        if (toolbar_int < 1 || toolbar_int > 4) {
            log_error("Invalid value for 'toolbar_location': " + std::to_string(toolbar_int));
            goto CLEANUP;
        }
        toolbar_location = (ToolbarLocation)toolbar_int;
    } else {
        log_error("Invalid or missing 'toolbar_location'");
        goto CLEANUP;
    }

    // page_view_count
    if (j.contains("page_view_count") && j["page_view_count"].is_number_integer()) {
        page_view_count = j["page_view_count"].get<int>();
    } else {
        log_error("Invalid or missing 'page_view_count'");
        goto CLEANUP;
    }

    // open_tab
    if (j.contains("open_tab") && j["open_tab"].is_number_integer()) {
        open_tab = j["open_tab"].get<int>();
    } else {
        log_error("Invalid or missing 'open_tab'");
        goto CLEANUP;
    }

    // max_recent_documents
    if (j.contains("max_recent_documents") && j["max_recent_documents"].is_number_integer()) {
        max_recent_documents = j["max_recent_documents"].get<int>();
    } else {
        log_error("Invalid or missing 'max_recent_documents'");
        goto CLEANUP;
    }

    // dpi
    dpi = 96; // Default value
    if (j.contains("dpi")) {
        if (j["dpi"].is_number_integer())
            dpi = j["dpi"].get<int>();
        else
            log_error("Invalid value for 'dpi', setting to 96");
    } else
        log_error("Missing 'dpi', setting to 96");

    // allow_oversize
    if (j.contains("allow_oversize") && j["allow_oversize"].is_boolean()) {
        allow_oversize = j["allow_oversize"].get<bool>();
    } else {
        log_error("Invalid or missing 'allow_oversize'");
        goto CLEANUP;
    }

    // theme
    if (j.contains("theme") && j["theme"].is_string()) {
        std::string theme_str = j["theme"].get<std::string>();
        if (!string_to_theme(theme_str, theme)) {
            log_error("Invalid value for 'theme': " + theme_str);
            goto CLEANUP;
        }
    } else {
        log_error("Invalid or missing 'theme'");
        goto CLEANUP;
    }

    // fast_search_dialog_size
    if (j.contains("fast_search_dialog_size") && j["fast_search_dialog_size"].is_array() && j["fast_search_dialog_size"].size() == 4) {
        bool fs_size_valid = true;
        fast_search_dialog_size.clear();
        for (const auto &size : j["fast_search_dialog_size"]) {
            if (size.is_number_integer()) {
                fast_search_dialog_size.push_back(size.get<int>());
            } else {
                fs_size_valid = false;
                break;
            }
        }
        if (!fs_size_valid) {
            log_error("Invalid entries in 'fast_search_dialog_size'");
            goto CLEANUP;
        }
    } else {
        log_error("Invalid or missing 'fast_search_dialog_size'");
        goto CLEANUP;
    }

    // border_margin
    if (j.contains("border_margin") && j["border_margin"].is_number_integer()) {
        border_margin = j["border_margin"].get<int>();
    } else {
        log_error("Invalid or missing 'border_margin'");
        goto CLEANUP;
    }

    // music_directory
    if (j.contains("music_directory") && j["music_directory"].is_string()) {
        music_directory = std::filesystem::path(j["music_directory"].get<std::string>());
    } else {
        log_error("Invalid or missing 'music_directory'");
        goto CLEANUP;
    }

    // log_level
    if (j.contains("log_level") && j["log_level"].is_string()) {
        std::string log_level_str = j["log_level"].get<std::string>();
        if (!string_to_log_level(log_level_str, log_level)) {
            log_error("Invalid value for 'log_level': " + log_level_str);
            goto CLEANUP;
        }
    } else {
        log_error("Invalid or missing 'log_level'");
        goto CLEANUP;
    }

    // If all parsing succeeded
    valid = true;

CLEANUP:
    if (!valid && reset_on_error) {
        set_defaults();
        save();
    }
}

json ConfigFile::to_json() const
{
    json j;
    j["file_version"] = file_version;
    j["restore_window_position"] = restore_window_position;
    j["restore_documents"] = restore_documents;
    j["zoom_to_content"] = zoom_to_content;

    j["open_documents"] = json::array();
    for (const auto &doc : open_documents) {
        json doc_json;
        doc_json["filename"] = doc.filename.string();
        doc_json["page"] = doc.page;
        doc_json["page_count"] = doc.page_count;
        j["open_documents"].push_back(doc_json);
    }

    j["recent_documents"] = json::array();
    for (const auto &path : recent_documents) {
        j["recent_documents"].push_back(path.string());
    }

    j["app_size"] = app_size;
    j["toolbar_location"] = static_cast<int>(toolbar_location);
    j["page_view_count"] = page_view_count;
    j["open_tab"] = open_tab;
    j["max_recent_documents"] = max_recent_documents;
    j["dpi"] = dpi;
    j["allow_oversize"] = allow_oversize;
    j["theme"] = theme_to_string(theme);
    j["fast_search_dialog_size"] = fast_search_dialog_size;
    j["border_margin"] = border_margin;
    j["music_directory"] = music_directory.string();
    j["log_level"] = log_level_to_string(log_level);
    return j;
}


// Method to save configuration to file
void ConfigFile::save() const
{
    SAFE_METHOD;

    if (!save_operation_enabled_) {
        return;
    }

    json j = to_json();

    std::ofstream outfile(filename_);
    if (!outfile.is_open()) {
        log_error("Failed to open config file for writing: " + filename_.string());
        return;
    }

    outfile << j.dump(4); // Pretty print with 4 spaces indentation
    outfile.close();
}


// Validation method
bool ConfigFile::validate() const
{
    // Validate file_version
    if (file_version <= 0) return false;

    // Validate page_view_count
    if (page_view_count != 1 && page_view_count != 2) return false;

    // Validate open_tab
    if (open_tab < -1) return false;

    // Validate max_recent_documents
    if (max_recent_documents < 0) return false;

    // Validate dpi
    if (dpi < 1) return false;

    // Validate theme
    if (!(theme == Theme::Dark || theme == Theme::Light)) return false;

    // Validate log_level
    if (!(log_level == LogLevel::Normal || log_level == LogLevel::Diagnostic)) return false;

    // Validate toolbar_location
    if (!(toolbar_location == ToolbarLocation::Top ||
          toolbar_location == ToolbarLocation::Bottom ||
          toolbar_location == ToolbarLocation::Left ||
          toolbar_location == ToolbarLocation::Right)) return false;

    // Validate app_size
    if (!valid_window_rect(app_size)) return false;

    // Validate fast_search_dialog_size
    if (!valid_window_rect(fast_search_dialog_size)) return false;

    return true;
}

// Fix method
bool ConfigFile::fix()
{
    bool was_valid = validate();
    if (was_valid) {
        return true;
    }

    set_defaults();
    save();
    return false;
}

// Document management methods
void ConfigFile::add_recent_document(const std::filesystem::path &path)
{
    // Remove if already exists
    recent_documents.erase(std::remove(recent_documents.begin(), recent_documents.end(), path), recent_documents.end());
    // Add to the end
    recent_documents.push_back(path);
    // Trim to max_recent_documents
    if (recent_documents.size() > static_cast<size_t>(max_recent_documents)) {
        recent_documents.erase(recent_documents.begin(), recent_documents.begin() + (recent_documents.size() - max_recent_documents));
    }
    save();
}

void ConfigFile::remove_recent_document(const std::filesystem::path &path)
{
    recent_documents.erase(std::remove(recent_documents.begin(), recent_documents.end(), path), recent_documents.end());
    save();
}

void ConfigFile::remove_recent_documents(const std::vector<std::filesystem::path> &paths)
{
    for (const auto &path : paths) {
        remove_recent_document(path);
    }
}

// Utility methods
void ConfigFile::set_defaults()
{
    file_version = 1;
    restore_window_position = true;
    restore_documents = true;
    zoom_to_content = true;
    open_documents.clear();
    recent_documents.clear();
    app_size = { 1024, 53, 2416, 1412 };
    toolbar_location = ToolbarLocation::Left;
    page_view_count = 2;
    open_tab = 0;
    max_recent_documents = 20;
    dpi = 111;
    allow_oversize = false;
    theme = Theme::Dark;
    fast_search_dialog_size = { 1489, 349, 951, 930 };
    border_margin = 10;
    music_directory = "C:\\smusic";
    log_level = LogLevel::Normal;
}

std::string ConfigFile::repr() const
{
    json j;
    j["file_version"] = file_version;
    j["restore_window_position"] = restore_window_position;
    j["restore_documents"] = restore_documents;
    j["zoom_to_content"] = zoom_to_content;

    // Serialize open_documents
    j["open_documents"] = json::array();
    for (const auto &doc : open_documents) {
        json doc_json;
        doc_json["filename"] = doc.filename.string();
        doc_json["page"] = doc.page;
        doc_json["page_count"] = doc.page_count;
        j["open_documents"].push_back(doc_json);
    }

    // Serialize recent_documents
    j["recent_documents"] = json::array();
    for (const auto &path : recent_documents) {
        j["recent_documents"].push_back(path.string());
    }

    j["app_size"] = app_size;
    j["toolbar_location"] = (int)(toolbar_location);
    j["page_view_count"] = page_view_count;
    j["open_tab"] = open_tab;
    j["max_recent_documents"] = max_recent_documents;
    j["dpi"] = dpi;
    j["allow_oversize"] = allow_oversize;
    j["theme"] = theme_to_string(theme);
    j["fast_search_dialog_size"] = fast_search_dialog_size;
    j["border_margin"] = border_margin;
    j["music_directory"] = music_directory.string();
    j["log_level"] = log_level_to_string(log_level);

    return j.dump(4); // Pretty print with 4 spaces indentation
}

void ConfigFile::filenames_to_os_convention()
{
    for (auto &doc : open_documents) {
        doc.filename = path_to_os_convention(doc.filename);
    }
    for (auto &path : recent_documents) {
        path = path_to_os_convention(path);
    }
    music_directory = path_to_os_convention(music_directory);
}

// Helper functions for validation
bool ConfigFile::valid_window_rect(const std::vector<int> &vec) const
{
    if (vec.size() != 4) return false;
    // First two can be -1 or >=0
    if (vec[0] < -1 || vec[1] < -1) return false;
    // Last two must be >0
    if (vec[2] <= 0 || vec[3] <= 0) return false;
    return true;
}

void ConfigFile::remove_duplicate_documents()
{
    // Remove duplicates in open_documents
    open_documents.erase(std::unique(open_documents.begin(), open_documents.end(),
                                     [&](const OpenDocument &a, const OpenDocument &b) -> bool {
        return point_to_same_file(a.filename, b.filename);
    }), open_documents.end());

    // Remove duplicates in recent_documents
    recent_documents.erase(std::unique(recent_documents.begin(), recent_documents.end(),
                                       [&](const std::filesystem::path &a, const std::filesystem::path &b) -> bool {
        return point_to_same_file(a, b);
    }), recent_documents.end());
}

std::vector<std::filesystem::path> ConfigFile::remove_duplicates(const std::vector<std::filesystem::path> &docs) const
{
    std::vector<std::filesystem::path> unique;
    for (const auto &doc : docs) {
        bool exists = std::any_of(unique.begin(), unique.end(),
                                  [&](const std::filesystem::path &existing) -> bool {
            return point_to_same_file(doc, existing);
        });
        if (!exists) {
            unique.push_back(doc);
        } else {
            log_info("Removing duplicate document from config: " + doc.string());
        }
    }
    return unique;
}

bool ConfigFile::remove_missing_documents()
{
    bool all_exist = true;

    // Check open_documents
    std::vector<OpenDocument> valid_open_docs;
    for (const auto &doc : open_documents) {
        if (std::filesystem::exists(doc.filename)) {
            valid_open_docs.push_back(doc);
        } else {
            log_error("Missing open document: " + doc.filename.string());
            all_exist = false;
        }
    }
    open_documents = valid_open_docs;

    // Check recent_documents
    std::vector<std::filesystem::path> valid_recent_docs;
    for (const auto &path : recent_documents) {
        if (std::filesystem::exists(path)) {
            valid_recent_docs.push_back(path);
        } else {
            log_error("Missing recent document: " + path.string());
            all_exist = false;
        }
    }
    recent_documents = valid_recent_docs;

    return all_exist;
}

void ConfigFile::remove_recent_in_open_documents()
{
    std::vector<std::filesystem::path> filtered_recent;
    for (const auto &recent : recent_documents) {
        bool is_open = false;
        for (const auto &open_doc : open_documents) {
            if (point_to_same_file(recent, open_doc.filename)) {
                is_open = true;
                break;
            }
        }
        if (!is_open) {
            filtered_recent.push_back(recent);
        }
    }
    recent_documents = filtered_recent;
}
