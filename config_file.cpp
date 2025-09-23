#include "config_file.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <set>

#include "logger.h"
#include "exception_logger.h"


using json = nlohmann::json;


std::filesystem::path path_to_os_convention(const std::filesystem::path &path)
{
    try {
        return std::filesystem::canonical(std::filesystem::absolute(path));
    } catch (const std::filesystem::filesystem_error &e) {
        logger::logger::error("Error canonicalizing path: " + std::string(e.what()));
        return std::filesystem::absolute(path);
    }
}


// Implementation of get_persistent_config_path
std::filesystem::path get_persistent_config_path(const std::string &filename)
{
    return filename;

    /*
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
    */
}

// Implementation of point_to_same_file
bool point_to_same_file(const std::filesystem::path &path1, const std::filesystem::path &path2)
{
    try {
        return std::filesystem::equivalent(path1, path2);
    } catch (const std::filesystem::filesystem_error &e) {
        logger::error("Error comparing files '" + path1.string() + "' and '" + path2.string() + "': " + e.what());
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
    case LogLevel::Normal: return "normal";
    case LogLevel::Diagnostic: return "diagnostic";
    case LogLevel::Trace: return "trace";
    default: return "normal";
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

    } else if (str == "trace") {
        level = LogLevel::Trace;
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

static std::string page_location_to_string(PageLocation location)
{
    switch (location) {
    case PageLocation::Left:
        return "left";
    case PageLocation::Center:
        return "center";
    default:
        return "top";
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
    location = ToolbarLocation::Left;
    return false;
}

static bool string_to_page_location(const std::string &str, PageLocation &location)
{
    if (str == "left") {
        location = PageLocation::Left;
        return true;
    } else if (str == "center") {
        location = PageLocation::Center;
        return true;
    }
    location = PageLocation::Left;
    return false;
}


// Constructor
ConfigFile::ConfigFile(bool reset_on_error)
    : filename_(get_persistent_config_path("MusicReader.config")),
    save_operation_enabled_(true)
{
    set_defaults();
    read(reset_on_error);
}



// Method to read configuration from file with improved error handling
void ConfigFile::read(bool reset_on_error)
{
    bool valid_file = false; // Flag to indicate successful parsing
    json j;

    //std::cout << "Reading config file: " << filename_.string() << std::endl;
    std::ifstream infile(filename_);
    if (!infile.is_open()) {
        logger::error("Config file not found: " + filename_.string());
        goto CLEANUP;
    }

    try {
        infile >> j;
    } catch (const json::parse_error &e) {
        logger::error("JSON parse error: " + std::string(e.what()));
        goto CLEANUP;
    }

    infile.close();

    // Begin parsing each key with specific error checks
    // file_version
    if (j.contains("file_version") && j["file_version"].is_number_integer()) {
        file_version_ = j["file_version"].get<int>();
    } else {
        logger::error("Invalid or missing 'file_version'");
        goto CLEANUP;
    }

    if (j.contains("restore_window_position") && j["restore_window_position"].is_boolean())
        restore_window_position_ = j["restore_window_position"].get<bool>();
    else
        logger::error("Invalid or missing 'restore_window_position'");

    if (j.contains("restore_documents") && j["restore_documents"].is_boolean())
        restore_documents_ = j["restore_documents"].get<bool>();
    else
        logger::error("Invalid or missing 'restore_documents'");

    if (j.contains("zoom_to_content") && j["zoom_to_content"].is_boolean())
        zoom_to_content_ = j["zoom_to_content"].get<bool>();
    else
        logger::error("Invalid or missing 'zoom_to_content'");

    if (j.contains("show_status_bar") && j["show_status_bar"].is_boolean())
        show_status_bar_ = j["show_status_bar"].get<bool>();
    else
        logger::error("Invalid or missing 'show_status_bar'");

    if (j.contains("dev_mode") && j["dev_mode"].is_boolean())
        dev_mode_ = j["dev_mode"].get<bool>();

    if (j.contains("show_toolbar") && j["show_toolbar"].is_boolean())
        show_toolbar_ = j["show_toolbar"].get<bool>();
    else
        logger::error("Invalid or missing 'show_toolbar'");

    if (j.contains("show_menu") && j["show_menu"].is_boolean())
        show_menu_ = j["show_menu"].get<bool>();
    else
        logger::error("Invalid or missing 'show_menu'");

    if (j.contains("horiz_tabs") && j["horiz_tabs"].is_boolean())
        horiz_tabs_ = j["horiz_tabs"].get<bool>();
    else
        logger::error("Invalid or missing 'horiz_tabs'");

    if (j.contains("allow_file_delete") && j["allow_file_delete"].is_boolean())
        allow_file_delete_ = j["allow_file_delete"].get<bool>();
    else
        logger::error("Invalid or missing 'allow_file_delete'");

    if (j.contains("tour_has_run") && j["tour_has_run"].is_boolean())
        tour_has_run_ = j["tour_has_run"].get<bool>();
    else
        logger::error("Invalid or missing 'tour_has_run'");

    if (j.contains("open_documents") && j["open_documents"].is_array()) {
        open_documents_.clear();
        int default_tab_order = 0;
        for (const auto &doc : j["open_documents"]) {
            if (doc.contains("filename") && doc["filename"].is_string() &&
                doc.contains("page") && doc["page"].is_number_integer() &&
                doc.contains("page_count") && doc["page_count"].is_number_integer()) {

                OpenDocument od;
                std::string filename_str = doc["filename"].get<std::string>();
                od.filename = std::filesystem::path(std::u8string(filename_str.begin(), filename_str.end()));
                od.page = doc["page"].get<int>();
                od.page_count = doc["page_count"].get<int>();

                if (doc.contains("access_order") && doc["access_order"].is_number_integer())
                    od.access_order = doc["access_order"].get<int>();
                else
                    od.access_order = -1;  // Mark as needs assignment

                // Handle tab_order for backward compatibility
                if (doc.contains("tab_order") && doc["tab_order"].is_number_integer())
                    od.tab_order = doc["tab_order"].get<int>();
                else
                    od.tab_order = default_tab_order++;  // Use array position if missing

                open_documents_.push_back(od);
            } else {
                logger::error("Invalid entry in 'open_documents'");
                goto CLEANUP;
            }
        }

        // Fix any missing or duplicate access_order values
        std::set<int> used_orders;
        int max_order = 0;

        // First pass: collect valid orders
        for (const auto &doc : open_documents_) {
            if (doc.access_order > 0) {
                used_orders.insert(doc.access_order);
                max_order = std::max(max_order, doc.access_order);
            }
        }

        // Second pass: assign unique orders to missing/invalid ones
        for (auto &doc : open_documents_) {
            if (doc.access_order <= 0 || used_orders.count(doc.access_order) > 1) {
                ++max_order;
                doc.access_order = max_order;
                used_orders.insert(max_order);
            }
        }

        // Fix any missing or duplicate tab_order values
        std::set<int> used_tab_orders;
        int max_tab_order = -1;

        // First pass: collect valid tab orders
        for (const auto &doc : open_documents_) {
            if (doc.tab_order >= 0) {
                used_tab_orders.insert(doc.tab_order);
                max_tab_order = std::max(max_tab_order, doc.tab_order);
            }
        }

        // Second pass: assign unique tab orders to missing/invalid ones
        for (auto &doc : open_documents_) {
            if (doc.tab_order < 0 || used_tab_orders.count(doc.tab_order) > 1) {
                ++max_tab_order;
                doc.tab_order = max_tab_order;
                used_tab_orders.insert(max_tab_order);
            }
        }
    } else {
        logger::error("Invalid or missing 'open_documents'");
        open_documents_.clear();
    }

    if (j.contains("recent_documents") && j["recent_documents"].is_array()) {
        recent_documents_.clear();
        for (const auto &path : j["recent_documents"]) {
            if (path.is_string()) {
                std::string path_str = path.get<std::string>();
                recent_documents_.emplace_back(std::filesystem::path(std::u8string(path_str.begin(), path_str.end())));
            } else {
                logger::error("Invalid entry in 'recent_documents'");
                goto CLEANUP;
            }
        }
    } else
        logger::error("Invalid or missing 'recent_documents'");


    app_size_ = { 0, 0, 1280, 1024 };
    if (j.contains("app_size") && j["app_size"].is_array() && j["app_size"].size() == 4) {
        bool valid = true;
        std::array<int, 4> size = {};
        for (int i = 0; valid && i < 4; ++i) {
            auto x = j["app_size"][i];
            if (x.is_number_integer())
                size[i] = x.get<int>();
            else
                valid = false;
        }
        if (valid)
            app_size_ = size;
        else
            logger::error("Invalid entries in 'app_size'");
    } else
        logger::info("Invalid or missing 'app_size'");

    dev_dialog_size_ = { 0, 0, 0, 0 };
    if (j.contains("dev_dialog_size") && j["dev_dialog_size"].is_array() && j["dev_dialog_size"].size() == 4) {
        bool valid = true;
        std::array<int, 4> size = {};
        for (int i = 0; valid && i < 4; ++i) {
            auto x = j["dev_dialog_size"][i];
            if (x.is_number_integer())
                size[i] = x.get<int>();
            else
                valid = false;
        }
        if (valid)
            dev_dialog_size_ = size;
        else
            logger::error("Invalid entries in 'dev_dialog_size_'");
    } else
        logger::info("Invalid or missing 'dev_dialog_size_'");

    if (j.contains("toolbar_location") && j["toolbar_location"].is_number_integer()) {
        int toolbar_int = j["toolbar_location"].get<int>();
        if (toolbar_int < 1 || toolbar_int > 4) {
            logger::error("Invalid value for 'toolbar_location': " + std::to_string(toolbar_int));
            toolbar_int = (int)ToolbarLocation::Left;
        } else
            toolbar_location_ = (ToolbarLocation)toolbar_int;
    } else {
        logger::error("Invalid or missing 'toolbar_location'");
    }

    if (j.contains("page_location") && j["page_location"].is_number_integer()) {
        int toolbar_int = j["page_location"].get<int>();
        if (toolbar_int < 0 || toolbar_int > 1)
            logger::error("Invalid value for 'page_location': " + std::to_string(toolbar_int));
        else
            page_location_ = (PageLocation)toolbar_int;
    } else
        logger::error("Invalid or missing 'page_location'");

    if (j.contains("page_view_count") && j["page_view_count"].is_number_integer())
        page_view_count_ = j["page_view_count"].get<int>();
    else
        logger::error("Invalid or missing 'page_view_count'");

    if (j.contains("page_step_size") && j["page_step_size"].is_number_integer()) {
        page_step_size_ = j["page_step_size"].get<int>();
        if (page_step_size_ < 1 || page_step_size_ > 2) {
            logger::error("Invalid value for 'page_step_size': {}", page_step_size_);
            page_view_count_ = 2;
        }
    } else
        logger::error("Invalid or missing 'page_step_size'");


    if (j.contains("open_tab") && j["open_tab"].is_number_integer())
        open_tab_ = j["open_tab"].get<int>();
    else
        logger::error("Invalid or missing 'open_tab'");

    if (j.contains("max_recent_documents") && j["max_recent_documents"].is_number_integer())
        max_recent_documents_ = j["max_recent_documents"].get<int>();
    else
        logger::error("Invalid or missing 'max_recent_documents'");


    if (j.contains("save_cadence_secs") && j["save_cadence_secs"].is_number_integer())
        save_cadence_secs_ = j["save_cadence_secs"].get<int>();
    else
        logger::error("Invalid or missing 'save_cadence_secs'");

    if (j.contains("dpi")) {
        if (j["dpi"].is_number_integer())
            dpi_ = j["dpi"].get<int>();
        else
            logger::error("Invalid value for 'dpi', setting to 96");
    } else
        logger::error("Missing 'dpi', setting to 96");

    if (j.contains("allow_oversize") && j["allow_oversize"].is_boolean())
        allow_oversize_ = j["allow_oversize"].get<bool>();
    else
        logger::error("Invalid or missing 'allow_oversize'");

    if (j.contains("theme") && j["theme"].is_string()) {
        std::string theme_str = j["theme"].get<std::string>();
        if (!string_to_theme(theme_str, theme_)) {
            logger::error("Invalid value for 'theme': " + theme_str);
        }
    } else
        logger::error("Invalid or missing 'theme'");


    fast_search_dialog_size_ = { 100, 100, 480, 320 };
    if (j.contains("fast_search_dialog_size") && j["fast_search_dialog_size"].is_array() && j["fast_search_dialog_size"].size() == 4) {
        bool valid = true;
        std::array<int, 4> size = {};
        for (int i = 0; valid && i < 4; ++i) {
            auto x = j["fast_search_dialog_size"][i];
            if (x.is_number_integer())
                size[i] = x.get<int>();
            else
                valid = false;
        }
        if (valid)
            fast_search_dialog_size_ = size;
        else
            logger::error("Invalid entries in 'fast_search_dialog_size'");
    } else
        logger::info("Invalid or missing 'fast_search_dialog_size'");

    if (j.contains("border_margin") && j["border_margin"].is_number_integer())
        border_margin_ = j["border_margin"].get<int>();
    else
        logger::error("Invalid or missing 'border_margin'");


    if (j.contains("music_directory") && j["music_directory"].is_string())
        music_directory_ = std::filesystem::path(j["music_directory"].get<std::string>());
    else
        logger::error("Invalid or missing 'music_directory'");

    if (j.contains("log_level") && j["log_level"].is_string()) {
        std::string log_level_str = j["log_level"].get<std::string>();
        if (!string_to_log_level(log_level_str, log_level_))
            logger::error("Invalid value for 'log_level': " + log_level_str);
        else
            logger::error("Invalid or missing 'log_level'");
    }

    // sanity check
    if (open_tab_ < 0 || open_tab_ >= static_cast<int>(open_documents_.size()))
        open_tab_ = 0;

    // If all parsing succeeded
    valid_file = true;

CLEANUP:
    if (!valid_file && reset_on_error) {
        set_defaults();
        save();
    }
}


json ConfigFile::to_json() const
{
    json j;
    j["file_version"] = file_version_;
    j["restore_window_position"] = restore_window_position_;
    j["restore_documents"] = restore_documents_;
    j["zoom_to_content"] = zoom_to_content_;
    j["show_status_bar"] = show_status_bar_;
    j["dev_mode"] = dev_mode_;
    j["show_toolbar"] = show_toolbar_;
    j["show_menu"] = show_menu_;
    j["horiz_tabs"] = horiz_tabs_;
    j["allow_file_delete"] = allow_file_delete_;

    j["open_documents"] = json::array();
    for (const auto &doc : open_documents_) {
        json doc_json;
        doc_json["filename"] = std::string(reinterpret_cast<const char *>(doc.filename.u8string().c_str()));
        doc_json["page"] = doc.page;
        doc_json["page_count"] = doc.page_count;
        doc_json["access_order"] = doc.access_order;
        doc_json["tab_order"] = doc.tab_order;
        j["open_documents"].push_back(doc_json);
    }

    j["recent_documents"] = json::array();
    for (const auto &path : recent_documents_)
        j["recent_documents"].push_back(std::string(reinterpret_cast<const char *>(path.u8string().c_str())));

    j["app_size"] = app_size_;
    j["dev_dialog_size"] = dev_dialog_size_;
    j["toolbar_location"] = static_cast<int>(toolbar_location_);
    j["page_location"] = static_cast<int>(page_location_);
    j["page_view_count"] = page_view_count_;
    j["page_step_size"] = page_step_size_;
    j["open_tab"] = open_tab_;
    j["max_recent_documents"] = max_recent_documents_;
    j["save_cadence_secs"] = save_cadence_secs_;
    j["dpi"] = dpi_;
    j["allow_oversize"] = allow_oversize_;
    j["theme"] = theme_to_string(theme_);
    j["fast_search_dialog_size"] = fast_search_dialog_size_;
    j["border_margin"] = border_margin_;
    j["music_directory"] = std::string(reinterpret_cast<const char *>(music_directory_.u8string().c_str()));
    j["log_level"] = log_level_to_string(log_level_);
    j["tour_has_run"] = tour_has_run_;
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

    std::ofstream outfile(filename_, std::ios::out | std::ios::binary);
    if (!outfile.is_open()) {
        logger::error("Failed to open config file for writing: " + std::string(reinterpret_cast<const char *>(filename_.u8string().c_str())));
        return;
    }

    // Write UTF-8 BOM (optional but helps some parsers)
    outfile << "\xEF\xBB\xBF";
    outfile << j.dump(4); // Pretty print with 4 spaces indentation
    outfile.close();
}


// Validation method
bool ConfigFile::validate() const
{
    if (file_version_ <= 0) return false;

    if (page_view_count_ != 1 && page_view_count_ != 2) return false;
    if (page_step_size_ != 1 && page_step_size_ != 2) return false;
    if (open_tab_ < -1) return false;
    if (max_recent_documents_ < 0) return false;
    if (save_cadence_secs_ < 0) return false;
    if (dpi_ < 1) return false;
    if (!(theme_ == Theme::Dark || theme_ == Theme::Light)) return false;
    if (!(log_level_ == LogLevel::Normal || log_level_ == LogLevel::Diagnostic || log_level_ != LogLevel::Trace)) return false;

    if (!(toolbar_location_ == ToolbarLocation::Top ||
          toolbar_location_ == ToolbarLocation::Bottom ||
          toolbar_location_ == ToolbarLocation::Left ||
          toolbar_location_ == ToolbarLocation::Right)) return false;

    if (!(page_location_ == PageLocation::Left || page_location_ == PageLocation::Center)) return false;
    if (!valid_window_rect(app_size_)) return false;
    if (!valid_window_rect(fast_search_dialog_size_)) return false;

    // Validate tab_order values are unique and start from 0
    std::set<int> tab_orders;
    for (const auto &doc : open_documents_) {
        if (doc.tab_order < 0) return false;
        if (tab_orders.count(doc.tab_order)) return false; // Duplicate
        tab_orders.insert(doc.tab_order);
    }

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
    recent_documents_.erase(std::remove(recent_documents_.begin(), recent_documents_.end(), path), recent_documents_.end());
    // Add to the end
    recent_documents_.push_back(path);
    // Trim to max_recent_documents
    if (recent_documents_.size() > static_cast<size_t>(max_recent_documents_)) {
        recent_documents_.erase(recent_documents_.begin(), recent_documents_.begin() + (recent_documents_.size() - max_recent_documents_));
    }
    save();
}

void ConfigFile::remove_recent_document(const std::filesystem::path &path)
{
    recent_documents_.erase(std::remove(recent_documents_.begin(), recent_documents_.end(), path), recent_documents_.end());
    save();
}

void ConfigFile::remove_recent_documents(const std::vector<std::filesystem::path> &paths)
{
    for (const auto &path : paths) {
        remove_recent_document(path);
    }
}

void ConfigFile::update_document_access(const std::filesystem::path &filepath)
{
    auto it = std::find_if(open_documents_.begin(), open_documents_.end(),
        [&filepath](const OpenDocument &doc) {
        return doc.filename == filepath;
    });

    if (it != open_documents_.end()) {
        // Remove the accessed document temporarily
        OpenDocument accessed_doc = *it;
        open_documents_.erase(it);

        // Renumber remaining documents
        std::sort(open_documents_.begin(), open_documents_.end(),
                  [](const OpenDocument &a, const OpenDocument &b) {
            return a.access_order < b.access_order;
        });

        for (size_t i = 0; i < open_documents_.size(); ++i) {
            open_documents_[i].access_order = static_cast<int>(i + 2);
        }

        // Add accessed document back as most recent (1)
        accessed_doc.access_order = 1;
        open_documents_.insert(open_documents_.begin(), accessed_doc);

        save();
    }
}


void ConfigFile::set_open_documents(const std::vector<OpenDocument> &value)
{
    open_documents_ = value;

    // Sort by access_order and renumber to be consecutive
    std::sort(open_documents_.begin(), open_documents_.end(),
              [](const OpenDocument &a, const OpenDocument &b) {
        return a.access_order < b.access_order;
    });

    for (size_t i = 0; i < open_documents_.size(); ++i) {
        open_documents_[i].access_order = static_cast<int>(i + 1);
    }

    // Ensure tab_order values are valid and consecutive
    std::sort(open_documents_.begin(), open_documents_.end(),
              [](const OpenDocument &a, const OpenDocument &b) {
        return a.tab_order < b.tab_order;
    });

    for (size_t i = 0; i < open_documents_.size(); ++i) {
        open_documents_[i].tab_order = static_cast<int>(i);
    }

    save();
}


void ConfigFile::add_new_document(const std::filesystem::path &filepath, int page, int page_count)
{
    // Increment all existing documents' access_order
    for (auto &doc : open_documents_)
        ++doc.access_order;

    // Find the highest tab_order
    int max_tab_order = -1;
    for (const auto &doc : open_documents_) {
        max_tab_order = std::max(max_tab_order, doc.tab_order);
    }

    // Add new document with access_order = 1 and tab_order at the end
    open_documents_.push_back({ filepath, page, page_count, 1, max_tab_order + 1 });

    save();
}


// Utility methods
void ConfigFile::set_defaults()
{
    file_version_ = 1;
    restore_window_position_ = true;
    restore_documents_ = true;
    zoom_to_content_ = true;
    show_status_bar_ = true;
    show_toolbar_ = true;
    show_menu_ = true;
    horiz_tabs_ = true;
    allow_file_delete_ = false;
    open_documents_.clear();
    recent_documents_.clear();
    app_size_ = { 10, 10, 1280, 1024 };
    dev_dialog_size_ = { 0, 0, 0, 0 };

    toolbar_location_ = ToolbarLocation::Left;
    page_location_ = PageLocation::Center;
    page_view_count_ = 2;
    page_step_size_ = 2;
    open_tab_ = -1;
    max_recent_documents_ = 50;
    save_cadence_secs_ = 15;
    dpi_ = 360;
    allow_oversize_ = false;
    theme_ = Theme::Dark;
    fast_search_dialog_size_ = { -1, -1, 640, 800 };
    border_margin_ = 10;
    music_directory_ = "";
    log_level_ = LogLevel::Normal;
    dev_mode_ = false;
    tour_has_run_ = false;
}

std::string ConfigFile::repr() const
{
    json j;
    j["file_version"] = file_version_;
    j["restore_window_position"] = restore_window_position_;
    j["restore_documents"] = restore_documents_;
    j["zoom_to_content"] = zoom_to_content_;
    j["show_status_bar"] = show_status_bar_;
    j["dev_mode"] = dev_mode_;
    j["show_toolbar"] = show_toolbar_;
    j["show_menu"] = show_menu_;
    j["horiz_tabs"] = horiz_tabs_;
    j["allow_file_delete"] = allow_file_delete_;

    // Serialize open_documents
    j["open_documents"] = json::array();
    for (const auto &doc : open_documents_) {
        json doc_json;
        doc_json["filename"] = doc.filename.string();
        doc_json["page"] = doc.page;
        doc_json["page_count"] = doc.page_count;
        doc_json["access_order"] = doc.access_order;
        doc_json["tab_order"] = doc.tab_order;
        j["open_documents"].push_back(doc_json);
    }

    // Serialize recent_documents
    j["recent_documents"] = json::array();
    for (const auto &path : recent_documents_) {
        j["recent_documents"].push_back(path.string());
    }

    j["app_size"] = app_size_;
    j["dev_dialog_size"] = dev_dialog_size_;
    j["toolbar_location"] = (int)(toolbar_location_);
    j["page_location"] = (int)(page_location_);
    j["page_view_count"] = page_view_count_;
    j["page_step_size"] = page_step_size_;
    j["open_tab"] = open_tab_;
    j["max_recent_documents"] = max_recent_documents_;
    j["save_cadence_secs"] = save_cadence_secs_;
    j["dpi"] = dpi_;
    j["allow_oversize"] = allow_oversize_;
    j["theme"] = theme_to_string(theme_);
    j["fast_search_dialog_size"] = fast_search_dialog_size_;
    j["border_margin"] = border_margin_;
    j["music_directory"] = music_directory_.string();
    j["log_level"] = log_level_to_string(log_level_);

    return j.dump(4); // Pretty print with 4 spaces indentation
}

void ConfigFile::filenames_to_os_convention()
{
    for (auto &doc : open_documents_) {
        doc.filename = path_to_os_convention(doc.filename);
    }
    for (auto &path : recent_documents_) {
        path = path_to_os_convention(path);
    }
    music_directory_ = path_to_os_convention(music_directory_);
}

// Helper functions for validation
bool ConfigFile::valid_window_rect(const std::array<int, 4> &vec) const
{
    // First two can be -1 or >=0
    if (vec[0] < -1 || vec[1] < -1) return false;
    // Last two must be >0
    if (vec[2] <= 0 || vec[3] <= 0) return false;
    return true;
}

void ConfigFile::remove_duplicate_documents()
{
    // Remove duplicates in open_documents
    open_documents_.erase(std::unique(open_documents_.begin(), open_documents_.end(),
                                      [&](const OpenDocument &a, const OpenDocument &b) -> bool {
        return point_to_same_file(a.filename, b.filename);
    }), open_documents_.end());

    // Remove duplicates in recent_documents
    recent_documents_.erase(std::unique(recent_documents_.begin(), recent_documents_.end(),
                                        [&](const std::filesystem::path &a, const std::filesystem::path &b) -> bool {
        return point_to_same_file(a, b);
    }), recent_documents_.end());
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
            logger::info("Removing duplicate document from config: " + doc.string());
        }
    }
    return unique;
}

bool ConfigFile::remove_missing_documents()
{
    bool all_exist = true;

    // Check open_documents
    std::vector<OpenDocument> valid_open_docs;
    for (const auto &doc : open_documents_) {
        if (std::filesystem::exists(doc.filename)) {
            valid_open_docs.push_back(doc);
        } else {
            logger::error("Missing open document: " + doc.filename.string());
            all_exist = false;
        }
    }
    open_documents_ = valid_open_docs;

    // Check recent_documents
    std::vector<std::filesystem::path> valid_recent_docs;
    for (const auto &path : recent_documents_) {
        if (std::filesystem::exists(path)) {
            valid_recent_docs.push_back(path);
        } else {
            logger::error("Missing recent document: " + path.string());
            all_exist = false;
        }
    }
    recent_documents_ = valid_recent_docs;

    return all_exist;
}

void ConfigFile::remove_recent_in_open_documents()
{
    std::vector<std::filesystem::path> filtered_recent;
    for (const auto &recent : recent_documents_) {
        bool is_open = false;
        for (const auto &open_doc : open_documents_) {
            if (point_to_same_file(recent, open_doc.filename)) {
                is_open = true;
                break;
            }
        }
        if (!is_open) {
            filtered_recent.push_back(recent);
        }
    }
    recent_documents_ = filtered_recent;
}
