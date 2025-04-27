#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <filesystem>
#include <iostream>


static std::string get_persistent_config_path(const std::string &file_name, const std::string &appname = "MusicReader")
{
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
}

void logger::configure_logger(const std::string &filename, size_t max_size, bool log_to_console)
{
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Create a rotating file sink (max 1 backup file)
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, max_size * 1024, 1));

        // Optionally log to console
        if (log_to_console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        logger_ = std::make_shared<spdlog::logger>("logger", sinks.begin(), sinks.end());
        spdlog::register_logger(logger_);
        logger_->set_level(spdlog::level::info);
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        logger_->flush_on(spdlog::level::debug);
        spdlog::flush_every(std::chrono::seconds(10));
    } catch (const std::exception &ex) {
        std::cerr << "Failed to initialize logger: " << ex.what() << std::endl;
    }
}



void logger::initialize(bool log_to_console, ConfigFile* cf, size_t max_size_kb)
{
    std::string log_file = get_persistent_config_path("MusicReader.log");
    std::cout << "opening log file: " << log_file << std::endl;
    configure_logger(log_file, max_size_kb, log_to_console);
    logged_error_ = false;
    config_file_ = cf;
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


void logger::info(const std::string &message)
{
    if (logger_) logger_->info(message);
}

void logger::warning(const std::string &message)
{
    if (logger_) logger_->warn(message);
}

void logger::error(const std::string &message)
{
    logged_error_ = true;
    if (logger_) logger_->error(message);
}

void logger::debug(const std::string &message)
{
    if (logger_ && (!config_file_ || config_file_->log_level() == LogLevel::Diagnostic)) logger_->debug(message);
}



void logger::logger::enable_debug_logging(bool enable)
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

