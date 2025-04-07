#pragma once
#include <chrono>
#include <iostream>
#include <string>
#include <iomanip>

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
        std::cout << label_ << ": " << format_duration(elapsed) << '\n';
        stopped_ = true;
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
        else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3)
                << duration_cast<duration<double>>(ns).count() << " s";
            return oss.str();
        }
    }
};
