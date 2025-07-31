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
    static void initialize(bool log_to_console, ConfigFile &config_file, bool append, size_t max_size_kb);
    static void shutdown();

    static void flush();

    static std::string log_file_path();

    static std::string get_log_content();

    // Returns true if an error has been logged since startup 
    static bool logged_error();

    static void enable_debug_logging(bool enable);
    static void enable_trace_logging(bool enable);

    static bool trace_enabled() { return trace_enabled_; }
    static bool debug_enabled() { return debug_enabled_; }

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

    static void trace(const std::string &message);
    static void trace(const std::u8string &message);

    // format versions
    template<typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void warning(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    static void trace(std::format_string<Args...> fmt, Args&&... args);

private:
    static void configure_logger(const std::string &filename, size_t max_size_kb, bool log_to_console);

    static inline bool logged_error_;
    static inline ConfigFile *config_file_ = nullptr;
    static inline std::string log_file_path_;
    static inline bool trace_enabled_ = false;
    static inline bool debug_enabled_ = false;
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

template<typename... Args>
inline static void logger::trace(std::format_string<Args...> fmt, Args&&... args)
{
    trace(std::format(fmt, std::forward<Args>(args)...));
}



struct function_tracer {
    const char *file_name;
    const char *func_name;
    int line_num;
    __forceinline function_tracer(const char *file, const char *name, int line)
        : file_name(file), func_name(name), line_num(line)
    {
        if (logger::trace_enabled())
            logger::trace("Enter {} {}:{}", file_name, func_name, line_num);
    }
    __forceinline ~function_tracer()
    {
        if (logger::trace_enabled())
            logger::trace("Exit {} {}:{}", file_name, func_name, line_num);
    }
};

struct function_tracer_msg {
    const char *file_name;
    const char *func_name;
    int line_num;
    std::string message;
    bool enabled;

    template<typename... Args>
    __forceinline function_tracer_msg(const char *file, const char *name, int line, std::format_string<Args...> fmt, Args&&... args)
        : file_name(file), func_name(name), line_num(line), enabled(logger::trace_enabled())
    {
        if (enabled) {
            message = std::format(fmt, std::forward<Args>(args)...);
            logger::trace("Enter {}:{} {} {}", file_name, line_num, func_name, message);
        }
    }

    __forceinline ~function_tracer_msg()
    {
        if (enabled)
            logger::trace("Exit {}:{} {} {}", file_name, line_num, func_name, message);
    }
};

// Trace functions in logger when in diagnostic mode
// 
// use:
// Put at top of function, or wherever you want to trace, optionally adding a message
// 
//     void foo() {
//         TRACE_FUNCTION;
//         ...
//
//      void voo() {
//          TRACE_FUNCTION_MSG("Requesting page {} of {}", page_num, filename_.string());
  
#define TRACE_FUNCTION function_tracer _trace_guard_##__LINE__(__FILE__, __func__, __LINE__)
#define TRACE_FUNCTION_MSG(...) function_tracer_msg _trace_guard_##__LINE__(__FILE__, __func__, __LINE__, __VA_ARGS__)