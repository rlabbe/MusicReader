#pragma once

#include <string>



namespace logger {

// Initializes the logger with the given file path and size limit.
//
// Parameters:
// filename - The log file path.
// max_size_kb - The maximum log file size in kilobytes.
// log_to_console - Whether to also log to the console.
void initialize(const std::string &filename, 
                size_t max_size_kb = 4, 
                bool log_to_console = false);

void shutdown();

void enable_debug_logging(bool enable);

// Logs an INFO level message.
void log_info(const std::string &message);

// Logs a WARNING level message.
void log_warning(const std::string &message);

// Logs an ERROR level message.
void log_error(const std::string &message);

// Logs a DEBUG level message.
void log_debug(const std::string &message);

std::string get_log_content();

}  // namespace logger

