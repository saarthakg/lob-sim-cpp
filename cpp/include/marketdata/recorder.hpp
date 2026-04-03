#pragma once
#include "binance_depth_event.hpp"
#include <fstream>
#include <string>

class Recorder {
public:
    explicit Recorder(const std::string& output_path);
    ~Recorder();

    void record_event(const BinanceDepthEvent& event);
    void flush();

private:
    std::ofstream file_;
    void write_header();
};
