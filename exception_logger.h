#pragma once

#include <exception>
#include <string>
#include "logger.h"

// ExceptionLogger
// RAII-based exception handler that logs any unhandled exceptions.
//
// This class ensures that any exceptions escaping a function are caught and logged.
// It is intended for catching unexpected programming errors without crashing the application.
//
// Usage:
// - Use `SAFE_METHOD;` at the start of a function to log and suppress exceptions.
//
// Example:
// void some_function() {
//     SAFE_METHOD;  // Logs exceptions but prevents crashes
//     risky_operation();
// }
class ExceptionLogger {
public:
    // Constructs the ExceptionLogger for a given function
    // @param func_name The name of the function where this logger is used
    explicit ExceptionLogger(const std::string& func_name)
        : func_name_(func_name)
    {
    }

    // Destructor that checks for unhandled exceptions and logs them.
    // Never throws; safe during stack unwinding.
    ~ExceptionLogger() noexcept
    {
        if (std::uncaught_exceptions() > 0) {
            try {
                logger::error("Exception escaping from " + func_name_);
            } catch (...) {
                // Swallow any exceptions from logging
            }
        }
    }

private:
    std::string func_name_; // Stores the function name for logging
};

// Macro to apply ExceptionLogger to a function, logging and suppressing exceptions
// Usage:
// void my_function() {
//     SAFE_METHOD;  // Logs exceptions and prevents crashes
//     risky_operation();
// }
#define SAFE_METHOD ExceptionLogger _exception_logger(__func__)
