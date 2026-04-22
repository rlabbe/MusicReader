#pragma once

#include <string>
#include <format>
#include <iostream>
#include <atomic>
#include <chrono>
#include <optional>


class ConfigFile;

struct logger {

    // Initializes the logger with the given file path and size limit.
    //
    // Parameters:
    // log_to_console - Whether to also log to the console.
    // max_size_kb - The maximum log file size in kilobytes.
    static void initialize(bool log_to_console, ConfigFile& config_file, size_t max_size_kb);
    static void shutdown();

    static void flush();

    static std::string log_file_path();

    static std::string get_log_content();

    // Returns true if an error has been logged since startup
    static bool logged_error();

    static void enable_debug_logging(bool enable);
    static void enable_trace_logging(bool enable);
    static void enable_debug_and_trace_logging(bool enable);

    static bool trace_enabled() { return trace_enabled_; }
    static bool debug_enabled() { return debug_enabled_; }

    // Logs an INFO level message.
    static void info(const std::string& message);
    static void info(const std::u8string& message);

    // Logs a WARNING level message.
    static void warning(const std::string& message);
    static void warning(const std::u8string& message);

    // Logs an ERROR level message.
    static void error(const std::string& message);
    static void error(const std::u8string& message);

    // Logs a DEBUG level message.
    static void debug(const std::string& message);
    static void debug(const std::u8string& message);

    static void trace(const std::string& message);
    static void trace(const std::u8string& message);

    // format versions
    template<typename... Args> static void debug(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args> static void info(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args> static void warning(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args> static void error(std::format_string<Args...> fmt, Args&&... args);

    template<typename... Args> static void trace(std::format_string<Args...> fmt, Args&&... args);

private:
    static void configure_logger(const std::string& filename, size_t max_size_kb, bool log_to_console);

    static inline bool logged_error_;
    static inline ConfigFile* config_file_ = nullptr;
    static inline std::string log_file_path_;
    static inline bool trace_enabled_ = false;
    static inline bool debug_enabled_ = false;
};

template<typename... Args> inline static void logger::debug(std::format_string<Args...> fmt, Args&&... args)
{
    debug(std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args> inline static void logger::info(std::format_string<Args...> fmt, Args&&... args)
{
    info(std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args> inline static void logger::warning(std::format_string<Args...> fmt, Args&&... args)
{
    warning(std::format(fmt, std::forward<Args>(args)...));
}


template<typename... Args> inline static void logger::error(std::format_string<Args...> fmt, Args&&... args)
{
    error(std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args> inline static void logger::trace(std::format_string<Args...> fmt, Args&&... args)
{
    trace(std::format(fmt, std::forward<Args>(args)...));
}


// __FUNCSIG__ generates something like:
//     auto __cdecl MusicReader::setup_mouse_hiding::<lambda_1>::operator ()(void) const
// and we just want the name, however, we don't use __FUNC__ because
// if inside a lambda it doesn't give you the outer function name.
std::string strip_extra_call_info(const std::string& funcsig, bool log = false);
void test_strip_extra_call_info();

int indent_level();
void increase_indent();
void decrease_indent();

struct time_logger {
    const char* msg;
    std::chrono::high_resolution_clock::time_point start_time_;

    __forceinline time_logger(const char* msg)
        : msg(msg)
        , start_time_(std::chrono::high_resolution_clock::now())
    {
    }

    __forceinline ~time_logger()
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_).count();
        logger::trace("{}: ({} us)", msg, duration_us);
    }
};


struct function_tracer {
    const char* file_name;
    const char* func_name;
    int line_num;
    bool enter_exit;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool active = false; // true if trace was enabled at construction
    std::optional<std::string> message;

    template<typename... Args>
    __forceinline function_tracer(const char* file,
                                  const char* name,
                                  int line,
                                  bool enter_exit,
                                  std::format_string<Args...> fmt = "",
                                  Args&&... args)
        : file_name(file)
        , func_name(name)
        , line_num(line)
        , enter_exit(enter_exit)
        , start_time_(std::chrono::high_resolution_clock::now())
    {
        if (!logger::trace_enabled())
            return;

        active = true;

        // remove path and drive letter if they exist
        if (auto p = strrchr(file, '\\'))
            file_name = p + 1;
        else if (auto q = strrchr(file, '/'))
            file_name = q + 1;

        if constexpr (sizeof...(Args) > 0)
            message.emplace(std::format(fmt, std::forward<Args>(args)...));

        if (enter_exit) {
            std::string indent(indent_level(), ' ');
            logger::trace("{}Enter {}:{} {}{}", indent, file_name, line_num, strip_extra_call_info(func_name),
                          message ? " " + *message : "");
            increase_indent();
        }
    }

    __forceinline double duration() const
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_).count());
    }

    __forceinline ~function_tracer()
    {
        if (!active)
            return;

        if (enter_exit) {
            decrease_indent();
            std::string indent(indent_level(), ' ');
            logger::trace("{}Exit {}:{} {} ({} us)", indent, file_name, line_num, strip_extra_call_info(func_name),
                          duration());
        } else { // call only
            std::string indent(indent_level(), ' ');
            logger::trace("{}Called {}:{} {}{} ({} us)", indent, file_name, line_num, strip_extra_call_info(func_name),
                          message ? " " + *message : "", duration());
        }
    }
};


// Trace functions in logger when in trace mode
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

#define TRACE_FUNCTION function_tracer _trace_guard_##__LINE__(__FILE__, __FUNCSIG__, __LINE__, true)
#define TRACE_FUNCTION_MSG(...)                                                                                        \
    function_tracer _trace_guard_##__LINE__(__FILE__, __FUNCSIG__, __LINE__, true, __VA_ARGS__)


// Similar to TRACE_FUNCTION but only traces the fact of the call, not entry/exit
// results in 1 line in the log, useful when the function doesn't call any other
// function which is being traced.
#define TRACE_CALL function_tracer _trace_guard_##__LINE__(__FILE__, __FUNCSIG__, __LINE__, false)
#define TRACE_CALL_MSG(...) function_tracer _trace_guard_##__LINE__(__FILE__, __FUNCSIG__, __LINE__, false, __VA_ARGS__)