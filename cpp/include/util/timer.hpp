#pragma once
#include <chrono>

class Timer {
public:
    void start() {
        start_ = std::chrono::steady_clock::now();
    }

    void stop() {
        end_ = std::chrono::steady_clock::now();
    }

    int64_t elapsed_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(end_ - start_).count();
    }

    double elapsed_ms() const {
        return static_cast<double>(elapsed_us()) / 1000.0;
    }

private:
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point end_;
};
