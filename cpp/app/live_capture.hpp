#pragma once
// Live capture of diff-depth WebSocket streams.
// Supports Bybit (default, US-accessible) and Binance (blocked in US).
//
// Bybit sync (simpler than Binance):
//   1. Subscribe to orderbook.200.BTCUSDT on wss://stream.bybit.com/v5/public/linear
//   2. First WebSocket message is type="snapshot" → load directly into OrderBook
//   3. Subsequent messages are type="delta" → apply as incremental updates
//   4. Record all delta events to normalized CSV
//
// Binance sync (see BookSync for full algorithm):
//   1. Start WebSocket stream (buffers events)
//   2. Fetch REST snapshot
//   3. Initialize BookSync, then record

#include "../include/marketdata/book_sync.hpp"
#include "../include/marketdata/parser.hpp"
#include "../include/marketdata/recorder.hpp"
#include "../include/core/order_book.hpp"
#include "../include/util/json.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>

using json = nlohmann::json;

static std::atomic<bool> g_stop_capture{false};
static void capture_signal_handler(int) { g_stop_capture.store(true); }

static std::string str_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}
static std::string str_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ── Bybit live capture ────────────────────────────────────────────────────────
// Bybit sends the initial full book as type="snapshot", then type="delta" updates.
// No separate REST call needed — cleaner than Binance's flow.

static void run_bybit_capture(const std::string& symbol,
                               int duration_minutes,
                               const std::string& output_path) {
    std::string sym = str_upper(symbol);
    // Linear USDT perpetuals (most liquid BTC market on Bybit)
    std::string ws_url = "wss://stream.bybit.com/v5/public/linear";

    std::cout << "[bybit-capture] Symbol:   " << sym << "\n";
    std::cout << "[bybit-capture] Duration: " << duration_minutes << " minutes\n";
    std::cout << "[bybit-capture] Output:   " << output_path << "\n";
    std::cout << "[bybit-capture] Endpoint: " << ws_url << "\n";

    OrderBook book;
    Recorder  recorder(output_path);

    std::mutex  mtx;
    bool        initialized    = false;
    size_t      events_recorded = 0;
    UpdateId    last_seq       = 0;

    ix::WebSocket ws;
    ws.setUrl(ws_url);
    ws.setHandshakeTimeout(15);

    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            // Subscribe to 200-level orderbook after connection
            json sub = {
                {"op", "subscribe"},
                {"args", {"orderbook.200." + sym}}
            };
            ws.sendText(sub.dump());
            std::cout << "[bybit-capture] Connected. Subscribed to orderbook.200." << sym << "\n";

        } else if (msg->type == ix::WebSocketMessageType::Message) {
            json j;
            try { j = json::parse(msg->str); }
            catch (...) { return; }

            // Skip non-orderbook messages (pings, subscription confirmations)
            if (!j.contains("type") || !j.contains("data")) return;

            std::string type = j["type"].get<std::string>();
            const auto& data = j["data"];

            // Timestamp: Bybit provides ms in "ts" field at top level
            Timestamp ts_us = j.value("ts", int64_t(0)) * 1000LL;  // ms → us
            UpdateId  seq   = data.value("seq", uint64_t(0));

            std::lock_guard<std::mutex> lock(mtx);

            if (type == "snapshot") {
                // Full book replacement — load directly
                book.clear();
                for (const auto& entry : data["b"]) {
                    Price px  = Parser::to_ticks(entry[0].get<std::string>());
                    Qty   qty = static_cast<Qty>(std::round(
                                    std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
                    if (qty > 0) book.apply_update(Side::Bid, px, qty);
                }
                for (const auto& entry : data["a"]) {
                    Price px  = Parser::to_ticks(entry[0].get<std::string>());
                    Qty   qty = static_cast<Qty>(std::round(
                                    std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
                    if (qty > 0) book.apply_update(Side::Ask, px, qty);
                }
                book.set_update_id(seq);
                last_seq    = seq;
                initialized = true;

                auto bb = book.best_bid();
                auto ba = book.best_ask();
                std::cout << "[bybit-capture] Book snapshot loaded ("
                          << data["b"].size() + data["a"].size() << " levels). "
                          << "best_bid=" << (bb ? bb->price / TICK_MULTIPLIER : 0.0)
                          << " best_ask=" << (ba ? ba->price / TICK_MULTIPLIER : 0.0)
                          << " spread=" << book.spread() << " ticks\n";
                std::cout << "[bybit-capture] Recording started. Press Ctrl+C to stop early.\n";

            } else if (type == "delta" && initialized) {
                // Build a BinanceDepthEvent from the Bybit delta
                BinanceDepthEvent ev{};
                ev.event_time      = ts_us;
                ev.symbol          = sym;
                ev.first_update_id = seq;
                ev.final_update_id = seq;

                for (const auto& entry : data["b"]) {
                    Price px  = Parser::to_ticks(entry[0].get<std::string>());
                    Qty   qty = static_cast<Qty>(std::round(
                                    std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
                    ev.bid_updates.push_back({px, qty});
                    book.apply_update(Side::Bid, px, qty);
                }
                for (const auto& entry : data["a"]) {
                    Price px  = Parser::to_ticks(entry[0].get<std::string>());
                    Qty   qty = static_cast<Qty>(std::round(
                                    std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
                    ev.ask_updates.push_back({px, qty});
                    book.apply_update(Side::Ask, px, qty);
                }
                book.set_update_id(seq);
                last_seq = seq;

                recorder.record_event(ev);
                ++events_recorded;
            }

        } else if (msg->type == ix::WebSocketMessageType::Error) {
            std::cerr << "[bybit-capture] Error: " << msg->errorInfo.reason << "\n";
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            std::cout << "[bybit-capture] WebSocket closed.\n";
        }
    });

    ws.start();

    // Wait for initialization (snapshot arrives within a few seconds of subscribing)
    std::cout << "[bybit-capture] Waiting for initial snapshot...\n";
    for (int i = 0; i < 30; ++i) {  // up to 15 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::lock_guard<std::mutex> lock(mtx);
        if (initialized) break;
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!initialized) {
            std::cerr << "[bybit-capture] Timed out waiting for snapshot. Check connectivity.\n";
            ws.stop();
            return;
        }
    }

    // Run for duration_minutes (or until Ctrl+C)
    std::signal(SIGINT, capture_signal_handler);
    auto end_time  = std::chrono::steady_clock::now() + std::chrono::minutes(duration_minutes);
    auto last_print = std::chrono::steady_clock::now();

    while (!g_stop_capture.load() && std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto now = std::chrono::steady_clock::now();
        if (now - last_print >= std::chrono::seconds(10)) {
            std::lock_guard<std::mutex> lock(mtx);
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(end_time - now).count();
            std::cout << "[bybit-capture] " << events_recorded << " events recorded"
                      << " | mid="     << book.mid_price() / TICK_MULTIPLIER
                      << " | spread="  << book.spread() << " ticks"
                      << " | imbal="   << book.imbalance(5)
                      << " | "         << remaining << "s remaining\n";
            last_print = now;
        }
    }

    ws.stop();
    recorder.flush();

    std::cout << "\n[bybit-capture] Done. Recorded " << events_recorded
              << " events to: " << output_path << "\n";
    std::cout << "[bybit-capture] Replay with:\n";
    std::cout << "  ./build/lob_sim --strategy mm --replay " << output_path << "\n";
    std::cout << "  ./build/lob_sim --strategy imbalance --replay " << output_path << "\n";
}

// ── Binance live capture ──────────────────────────────────────────────────────
// NOTE: Binance is geo-blocked for US users (HTTP 451).
// Use --exchange bybit instead.

static void run_binance_capture(const std::string& symbol,
                                 int duration_minutes,
                                 const std::string& output_path) {
    ix::initNetSystem();

    std::string sym_lower = str_lower(symbol);
    std::string sym_upper = str_upper(symbol);
    std::string ws_url    = "wss://stream.binance.com:9443/ws/" + sym_lower + "@depth@100ms";
    std::string rest_url  = "https://api.binance.com/api/v3/depth?symbol=" + sym_upper + "&limit=1000";

    std::cout << "[binance-capture] Symbol:   " << sym_upper << "\n";
    std::cout << "[binance-capture] Duration: " << duration_minutes << " minutes\n";
    std::cout << "[binance-capture] Output:   " << output_path << "\n";
    std::cout << "[binance-capture] NOTE: Binance is geo-blocked for US users (HTTP 451).\n";
    std::cout << "[binance-capture]       Use --exchange bybit if you get a 451 error.\n";

    BookSync sync;
    OrderBook book;
    Recorder  recorder(output_path);

    std::mutex mtx;
    bool       initialized    = false;
    size_t     events_recorded = 0;

    ix::WebSocket ws;
    ws.setUrl(ws_url);
    ws.setHandshakeTimeout(10);

    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            BinanceDepthEvent ev;
            try { ev = Parser::parse_depth_event(msg->str); }
            catch (...) { return; }

            std::lock_guard<std::mutex> lock(mtx);
            if (!initialized) {
                sync.buffer_event(ev);
            } else {
                bool ok = sync.apply_event(ev, book);
                if (ok) { recorder.record_event(ev); ++events_recorded; }
                else    { std::cerr << "[binance-capture] Gap! Stopping.\n"; g_stop_capture = true; }
            }
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            std::cout << "[binance-capture] WebSocket connected.\n";
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            std::cerr << "[binance-capture] Error: " << msg->errorInfo.reason << "\n";
        }
    });

    ws.start();
    std::cout << "[binance-capture] Buffering events for 2 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Fetch REST snapshot
    ix::HttpClient http;
    auto args = http.createRequest(rest_url, "GET");
    args->connectTimeout = 10;
    args->transferTimeout = 30;
    auto response = http.request(rest_url, "GET", "", args);

    if (response->statusCode != 200) {
        std::cerr << "[binance-capture] Snapshot fetch failed: HTTP " << response->statusCode << "\n";
        if (response->statusCode == 451)
            std::cerr << "[binance-capture] 451 = geo-blocked. Use --exchange bybit instead.\n";
        ws.stop();
        ix::uninitNetSystem();
        return;
    }

    auto snap = Parser::parse_snapshot(response->body);
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!sync.initialize_from_snapshot(snap, book)) {
            std::cerr << "[binance-capture] BookSync init failed. Retry.\n";
            ws.stop();
            ix::uninitNetSystem();
            return;
        }
        initialized = true;
    }

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::cout << "[binance-capture] Book initialized! "
              << "best_bid=" << (bb ? bb->price / TICK_MULTIPLIER : 0.0)
              << " best_ask=" << (ba ? ba->price / TICK_MULTIPLIER : 0.0)
              << " spread=" << book.spread() << " ticks\n";
    std::cout << "[binance-capture] Recording started. Press Ctrl+C to stop early.\n";

    std::signal(SIGINT, capture_signal_handler);
    auto end_time  = std::chrono::steady_clock::now() + std::chrono::minutes(duration_minutes);
    auto last_print = std::chrono::steady_clock::now();

    while (!g_stop_capture.load() && std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto now = std::chrono::steady_clock::now();
        if (now - last_print >= std::chrono::seconds(10)) {
            std::lock_guard<std::mutex> lock(mtx);
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(end_time - now).count();
            std::cout << "[binance-capture] " << events_recorded << " events recorded"
                      << " | mid=" << book.mid_price() / TICK_MULTIPLIER
                      << " | " << remaining << "s remaining\n";
            last_print = now;
        }
    }

    ws.stop();
    recorder.flush();
    std::cout << "\n[binance-capture] Done. Recorded " << events_recorded << " events.\n";
    ix::uninitNetSystem();
}

// ── Dispatcher ────────────────────────────────────────────────────────────────

static void run_live_capture(const std::string& symbol,
                              int duration_minutes,
                              const std::string& output_path,
                              const std::string& exchange = "bybit") {
    ix::initNetSystem();
    g_stop_capture.store(false);

    if (exchange == "binance") {
        run_binance_capture(symbol, duration_minutes, output_path);
    } else {
        run_bybit_capture(symbol, duration_minutes, output_path);
    }

    ix::uninitNetSystem();
}
