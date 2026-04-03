#include <catch2/catch_test_macros.hpp>
#include "../include/marketdata/book_sync.hpp"
#include "../include/core/order_book.hpp"

static BinanceDepthEvent make_event(UpdateId first, UpdateId last,
                                    Price bid_px = 6500000, Qty bid_qty = 100) {
    BinanceDepthEvent ev{};
    ev.event_time      = 1000000;
    ev.first_update_id = first;
    ev.final_update_id = last;
    ev.bid_updates     = {{bid_px, bid_qty}};
    return ev;
}

static BookSnapshot make_snapshot(UpdateId last_update_id) {
    BookSnapshot snap{};
    snap.last_update_id = last_update_id;
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 500}};
    return snap;
}

TEST_CASE("BookSync: initial state is WaitingForSnapshot", "[booksync]") {
    BookSync sync;
    REQUIRE(sync.state() == SyncState::WaitingForSnapshot);
}

TEST_CASE("BookSync: stale buffered events are discarded", "[booksync]") {
    BookSync sync;
    OrderBook book;

    // Buffer events with final_update_id <= snapshot.last_update_id
    sync.buffer_event(make_event(98, 99));  // stale
    sync.buffer_event(make_event(99, 100)); // stale (== snapshot id)

    BookSnapshot snap = make_snapshot(100);
    bool ok = sync.initialize_from_snapshot(snap, book);

    REQUIRE(ok);
    REQUIRE(sync.state() == SyncState::Initialized);
}

TEST_CASE("BookSync: valid first event passes U/u check", "[booksync]") {
    BookSync sync;
    OrderBook book;

    // first_update_id <= 101 AND final_update_id >= 101 (snapshot.last_update_id + 1 = 101)
    sync.buffer_event(make_event(100, 102));  // valid: U=100<=101 and u=102>=101

    BookSnapshot snap = make_snapshot(100);
    bool ok = sync.initialize_from_snapshot(snap, book);

    REQUIRE(ok);
    REQUIRE(sync.state() == SyncState::Initialized);
}

TEST_CASE("BookSync: gap between snapshot and first event triggers Error", "[booksync]") {
    BookSync sync;
    OrderBook book;

    // first_update_id=103 > snapshot.last_update_id+1=101 → gap
    sync.buffer_event(make_event(103, 105));

    BookSnapshot snap = make_snapshot(100);
    bool ok = sync.initialize_from_snapshot(snap, book);

    REQUIRE(!ok);
    REQUIRE(sync.state() == SyncState::Error);
}

TEST_CASE("BookSync: sequential events apply correctly", "[booksync]") {
    BookSync sync;
    OrderBook book;

    BookSnapshot snap = make_snapshot(100);
    sync.initialize_from_snapshot(snap, book);

    // Apply events 101→102, 103→104
    bool r1 = sync.apply_event(make_event(101, 102), book);
    bool r2 = sync.apply_event(make_event(103, 104), book);

    REQUIRE(r1);
    REQUIRE(r2);
    REQUIRE(sync.state() == SyncState::Initialized);
}

TEST_CASE("BookSync: gap in sequential events triggers Error", "[booksync]") {
    BookSync sync;
    OrderBook book;

    BookSnapshot snap = make_snapshot(100);
    sync.initialize_from_snapshot(snap, book);

    sync.apply_event(make_event(101, 102), book);
    // Skip 103→104, jump to 105→106 → gap
    bool r = sync.apply_event(make_event(105, 106), book);

    REQUIRE(!r);
    REQUIRE(sync.state() == SyncState::Error);
}

TEST_CASE("BookSync: reset clears all state", "[booksync]") {
    BookSync sync;
    OrderBook book;

    BookSnapshot snap = make_snapshot(100);
    sync.initialize_from_snapshot(snap, book);
    REQUIRE(sync.state() == SyncState::Initialized);

    sync.reset();
    REQUIRE(sync.state() == SyncState::WaitingForSnapshot);
}

TEST_CASE("BookSync: apply_event before initialization returns false", "[booksync]") {
    BookSync sync;
    OrderBook book;

    bool r = sync.apply_event(make_event(101, 102), book);
    REQUIRE(!r);
}
