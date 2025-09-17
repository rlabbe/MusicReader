#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <regex>
#include <filesystem>
#include <iostream>
#include "config_file.h"
#include "exception_logger.h"

// put here to avoid including spdlog.h in the header file
static std::shared_ptr<spdlog::logger> logger_;



static std::string get_persistent_config_path(const std::string &file_name, [[maybe_unused]] const std::string &appname = "MusicReader")
{

    return file_name;
    /*
    // Determine the operating system
    std::filesystem::path config_path;

#if defined(_WIN32)
    const char *appdata = std::getenv("APPDATA");
    if (!appdata) throw std::runtime_error("APPDATA environment variable not set");
    config_path = std::filesystem::path(appdata) / appname;
#elif defined(__linux__)
    const char *home = std::getenv("HOME");
    if (!home) throw std::runtime_error("HOME environment variable not set");
    config_path = std::filesystem::path(home) / ".config" / appname;
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    if (!home) throw std::runtime_error("HOME environment variable not set");
    config_path = std::filesystem::path(home) / "Library" / "Application Support" / appname;
#else
    throw std::runtime_error("Unsupported OS");
#endif

    // Ensure the directory exists
    std::filesystem::create_directories(config_path);

    // Return the full path to the config file
    return (config_path / file_name).string();
    */
}

void set_high_precision(std::shared_ptr<spdlog::logger> logger, bool tf)
{
    if (!logger) return;
    if (tf)
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v"); // microseconds!
    else
        logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v"); // seconds
}



void logger::configure_logger(const std::string &filename, size_t max_size_kb, bool log_to_console)
{
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Create a rotating file sink (max 1 backup file)
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, max_size_kb * 1024, 1));

        // Optionally log to console
        if (log_to_console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        logger_ = std::make_shared<spdlog::logger>("logger", sinks.begin(), sinks.end());
        spdlog::register_logger(logger_);
        logger_->set_level(spdlog::level::info);
        set_high_precision(logger_, false);
        logger_->flush_on(spdlog::level::debug);
        spdlog::flush_every(std::chrono::seconds(5));
    } catch (const std::exception &ex) {
        std::cerr << "Failed to initialize logger: " << ex.what() << std::endl;
    }
}

void logger::initialize(bool log_to_console, ConfigFile &cf, bool append, size_t max_size_kb)
{
    SAFE_METHOD;

    log_file_path_ = get_persistent_config_path("MusicReader.log");
    std::cout << "opening log file: " << log_file_path_ << std::endl;

    if(!append)
        std::filesystem::remove(log_file_path_);

    configure_logger(log_file_path_, max_size_kb, log_to_console);
    logged_error_ = false;
    config_file_ = &cf;
}

void logger::shutdown()
{
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        logger_ = nullptr;
        logged_error_ = false;
    }
}

std::string logger::log_file_path()
{
    return log_file_path_;
}

bool logger::logged_error()
{
    return logged_error_;
}

void logger::flush()
{
    if (logger_) logger_->flush();
}

void logger::info(const std::string &message)
{
    if (logger_) logger_->info(message);
}

void logger::info(const std::u8string &message)
{
    std::string s{ reinterpret_cast<const char *>(message.data()), message.size() };
    info(s);
}

void logger::warning(const std::string &message)
{
    if (logger_) logger_->warn(message);
}

void logger::warning(const std::u8string &message)
{
    std::string s{ reinterpret_cast<const char *>(message.data()), message.size() };
    warning(s);
}

void logger::error(const std::string &message)
{
    if (logger_) {
        logged_error_ = true;
        logger_->error(message);
    }
}

void logger::error(const std::u8string &message)
{
    std::string s{ reinterpret_cast<const char *>(message.data()), message.size() };
    error(s);
}

void logger::debug(const std::string &message)
{
    if (logger_ && config_file_->log_level() == LogLevel::Diagnostic)
        logger_->debug(message);
}

void logger::debug(const std::u8string &message)
{
    std::string s{ reinterpret_cast<const char *>(message.data()), message.size() };
    debug(s);
}

void logger::trace(const std::string &message)
{
    if (logger_ && config_file_->log_level() == LogLevel::Trace)
        logger_->debug(message);
}

void logger::trace(const std::u8string &message)
{
    std::string s{ reinterpret_cast<const char *>(message.data()), message.size() };
    trace(s);
}

void logger::enable_debug_logging(bool enable)
{
    if (!logger_)
        return;
    logger_->set_level(enable ? spdlog::level::debug : spdlog::level::info);
    for (auto &sink : logger_->sinks())
        sink->set_level(enable ? spdlog::level::debug : spdlog::level::info);

    trace_enabled_ = false;
    debug_enabled_ = true;
    set_high_precision(logger_, false);

}

void logger::enable_trace_logging(bool enable)
{
    if (!logger_)
        return;
    logger_->set_level(enable ? spdlog::level::debug : spdlog::level::info);
    for (auto &sink : logger_->sinks())
        sink->set_level(enable ? spdlog::level::debug : spdlog::level::info);

    trace_enabled_ = true;
    debug_enabled_ = false;
    set_high_precision(logger_, true);
    if (enable)
        logger_->flush_on(spdlog::level::trace);
    else
        logger_->flush_on(spdlog::level::debug);

}

std::string logger::get_log_content()
{
    if (!logger_)
        return {};

    logger_->flush();

    auto sinks = logger_->sinks();
    for (const auto &sink : sinks) {
        auto file_sink = std::dynamic_pointer_cast<spdlog::sinks::rotating_file_sink_mt>(sink);
        if (file_sink) {
            std::string log_path = file_sink->filename();
            std::ifstream file(log_path);
            if (!file.is_open()) {
                return "";
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    return "";
}

std::string strip_extra_call_info(const std::string &funcsig)
{
    // Handle lambdas specially
    if (funcsig.find("<lambda_") != std::string::npos) {
        size_t lambda_start = funcsig.find("::");
        if (lambda_start != std::string::npos) {
            lambda_start += 2;
            size_t paren_pos = funcsig.find('(', lambda_start);
            if (paren_pos != std::string::npos) {
                std::string lambda_part = funcsig.substr(lambda_start, paren_pos - lambda_start);
                return lambda_part + "()";
            }
        }
    }

    size_t paren_pos = funcsig.find('(');
    if (paren_pos == std::string::npos)
        return funcsig;

    // Find the function name by working backwards from the paren
    // Skip any calling convention (__cdecl, __stdcall, etc.)
    std::string before_paren = funcsig.substr(0, paren_pos);

    // Look for actual class scope (not template parameters)
    // Find all :: occurrences and check if they're inside template brackets
    size_t scope_pos = std::string::npos;
    int bracket_depth = 0;

    for (auto i = before_paren.length() - 1; i >= 1; --i) {
        if (before_paren[i] == '>')
            bracket_depth++;
        else if (before_paren[i] == '<')
            bracket_depth--;
        else if (bracket_depth == 0 && before_paren[i] == ':' && before_paren[i - 1] == ':') {
            scope_pos = i - 1;
            break;
        }
    }

    if (scope_pos != std::string::npos) {
        // Found class scope - extract function name after ::
        size_t func_start = scope_pos + 2;
        std::string func_part = before_paren.substr(func_start);
        size_t space_pos = func_part.rfind(' ');
        if (space_pos != std::string::npos)
            func_part = func_part.substr(space_pos + 1);
        return func_part + "()";
    } else {
        // No class scope - find last space before function name
        size_t last_space = before_paren.rfind(' ');
        if (last_space != std::string::npos) {
            std::string func_part = before_paren.substr(last_space + 1);
            return func_part + "()";
        }
    }

    return funcsig;
}


std::atomic<int> indent_level_;
int indent_level() { return indent_level_; }
void increase_indent() { indent_level_ += 2; }
void decrease_indent() { if (indent_level_ >0) indent_level_ -= 2; }







