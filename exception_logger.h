#pragma once

#include <exception>
#include <string>
#include "logger.h"

/**
 * @class ExceptionLogger
 * @brief RAII-based exception handler that logs any unhandled exceptions.
 *
 * This class ensures that any exceptions escaping a function are caught and logged.
 * It is intended for catching unexpected programming errors without crashing the application.
 *
 * Usage:
 * - Use `SAFE_METHOD;` at the start of a function to log and suppress exceptions.
 * - Use `LOG_EXCEPTION;` to log and allow exceptions to propagate.
 *
 * Example:
 * ```cpp
 * void some_function() {
 *     SAFE_METHOD;  // Logs exceptions but prevents crashes
 *     risky_operation();
 * }
 *
 * void another_function() {
 *     LOG_EXCEPTION;  // Logs but allows exceptions to propagate
 *     might_throw();
 * }
 * ```
 */
class ExceptionLogger {
public:
    /**
     * @brief Constructs the ExceptionLogger for a given function.
     * @param func_name The name of the function where this logger is used.
     * @param allow_throw If true, the exception is rethrown after logging.
     */
    ExceptionLogger(const std::string& func_name, bool allow_throw)
        : func_name_(func_name)
        , allow_throw_(allow_throw)
    {
    }

    /**
     * @brief Destructor that checks for unhandled exceptions and logs them.
     *
     * If an exception was thrown but not caught, it is logged along with the function name.
     * If `allow_throw_` is true, the exception is rethrown after logging.
     */
    ~ExceptionLogger() noexcept(false)
    {
        if (std::uncaught_exceptions() > 0) {
            try {
                throw;
            } catch (const std::exception& e) {
                logger::error("Exception in " + func_name_ + ": " + e.what());
                if (allow_throw_)
                    throw;
            } catch (...) {
                logger::error("Unknown exception in " + func_name_);
                if (allow_throw_)
                    throw;
            }
        }
    }

private:
    std::string func_name_; ///< Stores the function name for logging.
    bool allow_throw_;      ///< Determines whether to rethrow the exception.
};

/**
 * @brief Macro to apply ExceptionLogger to a function, logging and suppressing exceptions.
 *
 * This macro creates an instance of ExceptionLogger at the start of a function,
 * ensuring that any unhandled exceptions are logged and prevented from crashing the application.
 *
 * Example Usage:
 * ```cpp
 * void my_function() {
 *     SAFE_METHOD;  // Logs exceptions and prevents crashes
 *     risky_operation();
 * }
 * ```
 */
#define SAFE_METHOD ExceptionLogger _exception_logger(__func__, false)

/**
 * @brief Macro to apply ExceptionLogger to a function, logging exceptions but allowing them to propagate.
 *
 * Use this when you want to log unexpected exceptions but still allow the caller to handle them.
 *
 * Example Usage:
 * ```cpp
 * void my_function() {
 *     LOG_EXCEPTION;  // Logs but allows exceptions to propagate
 *     might_throw();
 * }
 * ```
 */
#define LOG_EXCEPTION ExceptionLogger _exception_logger(__func__, true)
