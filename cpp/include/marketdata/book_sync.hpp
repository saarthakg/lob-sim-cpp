#pragma once
#include "binance_depth_event.hpp"
#include "../core/book_snapshot.hpp"
#include "../core/order_book.hpp"
#include <vector>

enum class SyncState {
    WaitingForSnapshot,
    Initialized,
    Error       // gap detected, needs restart
};

class BookSync {
public:
    BookSync();

    // Call for every incoming WebSocket event (even before snapshot arrives)
    void buffer_event(const BinanceDepthEvent& event);

    // Call once when the REST snapshot has been fetched.
    // Applies buffered events that pass the sync check to the book.
    // Returns true if initialization succeeded.
    bool initialize_from_snapshot(const BookSnapshot& snapshot, OrderBook& book);

    // Call for each subsequent event after initialization.
    // Returns false if a gap is detected (caller should restart).
    bool apply_event(const BinanceDepthEvent& event, OrderBook& book);

    SyncState state() const;
    void reset();

private:
    SyncState state_;
    UpdateId  local_update_id_;
    std::vector<BinanceDepthEvent> buffer_;

    static void apply_event_to_book(const BinanceDepthEvent& event, OrderBook& book);
};
