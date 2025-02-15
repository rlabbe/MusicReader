#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <sstream>
#include <fstream>

namespace {
std::shared_ptr<spdlog::logger> logger_;

void configure_logger(const std::string &filename, size_t max_size, bool log_to_console)
{
    std::vector<spdlog::sink_ptr> sinks;

    // Create a rotating file sink (max 1 backup file)
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, max_size * 1024, 1));

    // Optionally log to console
    if (log_to_console)
    {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    logger_ = std::make_shared<spdlog::logger>("logger", sinks.begin(), sinks.end());
    spdlog::register_logger(logger_);
    logger_->set_level(spdlog::level::info);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
    logger_->flush_on(spdlog::level::err);
    spdlog::flush_every(std::chrono::seconds(10));
}
}

namespace logger {

void initialize(const std::string &filename, size_t max_size_kb, bool log_to_console)
{
    configure_logger(filename, max_size_kb, log_to_console);
}

void shutdown()
{
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        logger_ = nullptr;
    }
}


void log_info(const std::string &message)
{
    if (logger_) logger_->info(message);
}

void log_warning(const std::string &message)
{
    if (logger_) logger_->warn(message);
}

void log_error(const std::string &message)
{
    if (logger_) logger_->error(message);
}

void log_debug(const std::string &message)
{
    if (logger_) logger_->debug(message);
}



void logger::enable_debug_logging(bool enable)
{
    if (logger_)
    {
        logger_->set_level(enable ? spdlog::level::debug : spdlog::level::info);
        for (auto &sink : logger_->sinks())
        {
            sink->set_level(enable ? spdlog::level::debug : spdlog::level::info);
        }
    }
}

std::string get_log_content()
{
    if (!logger_) {
        return "";
    }

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


}  // namespace logger
