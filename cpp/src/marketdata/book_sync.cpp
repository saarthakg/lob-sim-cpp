#include "../../include/marketdata/book_sync.hpp"
#include <stdexcept>

BookSync::BookSync() : state_(SyncState::WaitingForSnapshot), local_update_id_(0) {}

void BookSync::buffer_event(const BinanceDepthEvent& event) {
    if (state_ == SyncState::WaitingForSnapshot) {
        buffer_.push_back(event);
    }
    // After initialization, events are applied directly via apply_event()
}

bool BookSync::initialize_from_snapshot(const BookSnapshot& snapshot, OrderBook& book) {
    // Step 1: Load snapshot into book
    book.load_snapshot(snapshot);
    local_update_id_ = snapshot.last_update_id;

    // Step 2: Discard stale buffered events (final_update_id <= snapshot.last_update_id)
    size_t valid_start = buffer_.size();
    for (size_t i = 0; i < buffer_.size(); ++i) {
        if (buffer_[i].final_update_id > snapshot.last_update_id) {
            valid_start = i;
            break;
        }
    }

    if (valid_start == buffer_.size()) {
        // All buffered events are stale — we'll need to wait for more events
        // This is valid: just clear buffer and mark initialized
        buffer_.clear();
        state_ = SyncState::Initialized;
        return true;
    }

    // Step 3: Validate the first useful event per Binance docs:
    // first_update_id <= snapshot.last_update_id + 1  AND
    // final_update_id >= snapshot.last_update_id + 1
    const auto& first = buffer_[valid_start];
    bool first_valid = (first.first_update_id <= snapshot.last_update_id + 1) &&
                       (first.final_update_id >= snapshot.last_update_id + 1);
    if (!first_valid) {
        // Gap between snapshot and first useful event
        reset();
        state_ = SyncState::Error;
        return false;
    }

    // Step 4: Apply all valid buffered events in order
    for (size_t i = valid_start; i < buffer_.size(); ++i) {
        const auto& ev = buffer_[i];
        // After the first event, each subsequent one must be contiguous
        if (i > valid_start) {
            if (ev.first_update_id != local_update_id_ + 1) {
                reset();
                state_ = SyncState::Error;
                return false;
            }
        }
        apply_event_to_book(ev, book);
        local_update_id_ = ev.final_update_id;
    }

    buffer_.clear();
    state_ = SyncState::Initialized;
    return true;
}

bool BookSync::apply_event(const BinanceDepthEvent& event, OrderBook& book) {
    if (state_ != SyncState::Initialized) return false;

    // Verify contiguity
    if (event.first_update_id != local_update_id_ + 1) {
        state_ = SyncState::Error;
        return false;
    }

    apply_event_to_book(event, book);
    local_update_id_ = event.final_update_id;
    book.set_update_id(local_update_id_);
    return true;
}

void BookSync::apply_event_to_book(const BinanceDepthEvent& event, OrderBook& book) {
    for (const auto& lvl : event.bid_updates) {
        book.apply_update(Side::Bid, lvl.price, lvl.qty);
    }
    for (const auto& lvl : event.ask_updates) {
        book.apply_update(Side::Ask, lvl.price, lvl.qty);
    }
}

SyncState BookSync::state() const { return state_; }

void BookSync::reset() {
    state_           = SyncState::WaitingForSnapshot;
    local_update_id_ = 0;
    buffer_.clear();
}
