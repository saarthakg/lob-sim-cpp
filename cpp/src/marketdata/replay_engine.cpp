#include "../../include/marketdata/replay_engine.hpp"
#include "../../include/marketdata/parser.hpp"
#include <fstream>
#include <stdexcept>
#include <limits>

ReplayEngine::ReplayEngine(const std::string& csv_path) : csv_path_(csv_path) {}

void ReplayEngine::replay(EventCallback callback) {
    replay_impl(std::numeric_limits<size_t>::max(), callback);
}

void ReplayEngine::replay_n(size_t max_events, EventCallback callback) {
    replay_impl(max_events, callback);
}

size_t ReplayEngine::total_events() const { return total_events_; }

void ReplayEngine::replay_impl(size_t max_events, EventCallback callback) {
    std::ifstream file(csv_path_);
    if (!file.is_open()) {
        throw std::runtime_error("ReplayEngine: cannot open file: " + csv_path_);
    }

    std::string line;
    // Skip header
    if (!std::getline(file, line)) return;

    total_events_ = 0;
    BinanceDepthEvent current_event;
    bool has_current = false;

    auto emit = [&]() {
        if (has_current) {
            callback(current_event);
            ++total_events_;
            has_current = false;
        }
    };

    while (std::getline(file, line) && total_events_ < max_events) {
        if (line.empty()) continue;

        BinanceDepthEvent row_ev;
        try {
            row_ev = Parser::parse_csv_row(line);
        } catch (...) {
            continue;  // skip malformed rows
        }

        // Group rows with the same (first_update_id, final_update_id) into one event
        if (has_current &&
            row_ev.first_update_id == current_event.first_update_id &&
            row_ev.final_update_id == current_event.final_update_id) {
            // Merge into current event
            for (const auto& lvl : row_ev.bid_updates)
                current_event.bid_updates.push_back(lvl);
            for (const auto& lvl : row_ev.ask_updates)
                current_event.ask_updates.push_back(lvl);
        } else {
            emit();
            current_event = row_ev;
            has_current   = true;
        }
    }
    emit();  // flush last event
}
