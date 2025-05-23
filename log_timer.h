#pragma once
#include <chrono>
#include <format>
#include <string>
#include <iomanip>
#include "logger.h"


#ifdef LOG_TIMER_ENABLED


class LogTimer {
public:
    explicit LogTimer(std::string label)
        : label_(std::move(label)), start_time_(std::chrono::steady_clock::now()), stopped_(false)
    {
    }



    ~LogTimer()
    {
        if (!stopped_) stop();
    }

    void stop()
    {
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = end_time - start_time_;
        logger::debug("{} {}", label_, format_duration(elapsed));
        logger::flush();
        stopped_ = true;
    }

    void start(const std::string &label="")
    {
        stopped_ = false;
        start_time_ = std::chrono::steady_clock::now();
        if (label.size() > 0) 
            label_ = label;
    }


private:
    std::string label_;
    std::chrono::steady_clock::time_point start_time_;
    bool stopped_;

    static std::string format_duration(std::chrono::nanoseconds ns)
    {
        using namespace std::chrono;

        if (ns < 1us)
            return std::to_string(ns.count()) + " ns";
        else if (ns < 1ms)
            return std::to_string(duration_cast<microseconds>(ns).count()) + " us";
        else if (ns < 1s)
            return std::to_string(duration_cast<milliseconds>(ns).count()) + " ms";
        else 
            return std::format("{:.2f} s", duration_cast<duration<double>>(ns).count());;
    }
};


#define CONCATENATE_DETAIL(x, y) x ## y
#define CONCATENATE(x, y) CONCATENATE_DETAIL(x, y)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define LOG_TIMER(...) LogTimer CONCATENATE(log_timer_, __LINE__)(CONCATENATE_OPTIONAL(__VA_ARGS__) __FILE__ ":" TOSTRING(__LINE__) ":" __FUNCTION__)

#define CONCATENATE_OPTIONAL(...) CONCATENATE_OPTIONAL_IMPL(__VA_ARGS__, "", "")
#define CONCATENATE_OPTIONAL_IMPL(x, ...) x


#else

class LogTimer {
public:
    explicit LogTimer([[maybe_unused]] const std::string &label) {}
    void stop() {}
    void start([[maybe_unused]] const std::string &label = "") {}

};

#define LOG_TIMER(...) 
#endif