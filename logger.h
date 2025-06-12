#pragma once

#include <string>
#include <format>

class ConfigFile;

struct logger {

    // Initializes the logger with the given file path and size limit.
    //
    // Parameters:
    // log_to_console - Whether to also log to the console.
    // max_size_kb - The maximum log file size in kilobytes.
    static void initialize(bool log_to_console, ConfigFile *config_file, size_t max_size_kb = 4);
    static void shutdown();

    static void flush();

    static std::string log_file_path();

    static std::string get_log_content();

    // Returns true if an error has been logged since startup 
    static bool logged_error();

    static void enable_debug_logging(bool enable);

    // Logs an INFO level message.
    static void info(const std::string &message);
    static void info(const std::u8string &message);

    // Logs a WARNING level message.
    static void warning(const std::string &message);
    static void warning(const std::u8string &message);

    // Logs an ERROR level message.
    static void error(const std::string &message);
    static void error(const std::u8string &message);

    // Logs a DEBUG level message.
    static void debug(const std::string &message);
    static void debug(const std::u8string &message);

    // format versions
    template<typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void warning(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args);

private:
    static void configure_logger(const std::string &filename, size_t max_size, bool log_to_console);

    static bool logged_error_;
    static ConfigFile *config_file_;
    static std::string log_file_path_;
};

template<typename... Args>
inline static void logger::debug(std::format_string<Args...> fmt, Args&&... args)
{
    debug(std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline static void logger::info(std::format_string<Args...> fmt, Args&&... args)
{
    info(std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline static void logger::warning(std::format_string<Args...> fmt, Args&&... args)
{
    warning(std::format(fmt, std::forward<Args>(args)...));
}


template<typename... Args>
inline static void logger::error(std::format_string<Args...> fmt, Args&&... args)
{
    error(std::format(fmt, std::forward<Args>(args)...));
}
