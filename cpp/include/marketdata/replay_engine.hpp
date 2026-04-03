#pragma once
#include "binance_depth_event.hpp"
#include <string>
#include <functional>

using EventCallback = std::function<void(const BinanceDepthEvent&)>;

class ReplayEngine {
public:
    explicit ReplayEngine(const std::string& csv_path);

    // Replay all events, calling callback for each
    void replay(EventCallback callback);

    // Replay up to max_events events
    void replay_n(size_t max_events, EventCallback callback);

    size_t total_events() const;  // number of distinct events replayed/scanned

private:
    std::string csv_path_;
    size_t      total_events_ = 0;

    // Read rows from file and call callback, grouping rows by (first_update_id, final_update_id)
    void replay_impl(size_t max_events, EventCallback callback);
};
