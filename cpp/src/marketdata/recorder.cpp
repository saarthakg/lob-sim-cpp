#include "../../include/marketdata/recorder.hpp"
#include <stdexcept>

Recorder::Recorder(const std::string& output_path)
    : file_(output_path, std::ios::out | std::ios::trunc) {
    if (!file_.is_open()) {
        throw std::runtime_error("Recorder: cannot open file: " + output_path);
    }
    write_header();
}

Recorder::~Recorder() {
    flush();
    file_.close();
}

void Recorder::write_header() {
    file_ << "ts,event_type,first_update_id,final_update_id,side,price_ticks,qty\n";
}

void Recorder::record_event(const BinanceDepthEvent& event) {
    // One row per price-level change within the event
    for (const auto& lvl : event.bid_updates) {
        file_ << event.event_time << ",depth,"
              << event.first_update_id << ","
              << event.final_update_id << ",B,"
              << lvl.price << ","
              << lvl.qty << "\n";
    }
    for (const auto& lvl : event.ask_updates) {
        file_ << event.event_time << ",depth,"
              << event.first_update_id << ","
              << event.final_update_id << ",A,"
              << lvl.price << ","
              << lvl.qty << "\n";
    }
}

void Recorder::flush() {
    file_.flush();
}
