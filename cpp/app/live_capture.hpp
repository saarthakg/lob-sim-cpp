#pragma once
// Live capture of L2 depth + trade prints to the v2 normalized CSV format.
//
// Bybit (default; reachable from the US):
//   wss://stream.bybit.com/v5/public/linear, topics orderbook.200.<SYM> and
//   publicTrade.<SYM>. Every `snapshot` message (the first one, and any
//   service-restart resnapshot) is recorded as S rows so a replay starts from
//   — and resets to — the true book; deltas as D rows; trades as T rows.
//   Book rows are stamped with `cts` (matching-engine time), trades with `T`.
//
// Binance spot (geo-blocked in the US, HTTP 451):
//   <sym>@depth@100ms + <sym>@aggTrade on one combined stream, REST snapshot,
//   BookSync for the U/u sequence handshake. The REST snapshot is recorded as
//   S rows.
//
// The previous version recorded only deltas (never the initial snapshot) and
// no trades, so replays began from an empty, partially-populated book and the
// fill model had no execution evidence at all.

#include "../include/marketdata/book_sync.hpp"
#include "../include/marketdata/parser.hpp"
#include "../include/marketdata/recorder.hpp"
#include "../include/util/json.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

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

static std::vector<Recorder::StrLevel> str_levels(const json& arr) {
    std::vector<Recorder::StrLevel> out;
    for (const auto& e : arr) out.emplace_back(e[0].get<std::string>(), e[1].get<std::string>());
    return out;
}

static void wait_for_duration(int duration_minutes, std::mutex& mtx, const size_t& book_msgs,
                              const size_t& trade_msgs, const char* tag) {
    std::signal(SIGINT, capture_signal_handler);
    auto end_time   = std::chrono::steady_clock::now() + std::chrono::minutes(duration_minutes);
    auto last_print = std::chrono::steady_clock::now();
    while (!g_stop_capture.load() && std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto now = std::chrono::steady_clock::now();
        if (now - last_print >= std::chrono::seconds(10)) {
            std::lock_guard<std::mutex> lock(mtx);
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(end_time - now).count();
            std::cout << "[" << tag << "] book msgs=" << book_msgs << " trades=" << trade_msgs
                      << " | " << std::max<long long>(remaining, 0) << "s remaining\n";
            last_print = now;
        }
    }
}

// ── Bybit ────────────────────────────────────────────────────────────────────

static void run_bybit_capture(const std::string& symbol, int duration_minutes,
                              const std::string& output_path) {
    const std::string sym = str_upper(symbol);
    Recorder recorder(output_path);
    std::mutex mtx;
    bool   initialized = false;
    size_t book_msgs = 0, trade_msgs = 0;

    ix::WebSocket ws;
    ws.setUrl("wss://stream.bybit.com/v5/public/linear");
    ws.setHandshakeTimeout(15);
    ws.setPingInterval(20);

    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            json sub = {{"op", "subscribe"},
                        {"args", {"orderbook.200." + sym, "publicTrade." + sym}}};
            ws.sendText(sub.dump());
            std::cout << "[bybit-capture] Connected; subscribed to orderbook.200 + publicTrade.\n";
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Error) {
            std::cerr << "[bybit-capture] Error: " << msg->errorInfo.reason << "\n";
            return;
        }
        if (msg->type != ix::WebSocketMessageType::Message) return;

        json j;
        try { j = json::parse(msg->str); } catch (...) { return; }
        if (!j.contains("topic") || !j.contains("data")) return;
        const std::string topic = j["topic"].get<std::string>();

        std::lock_guard<std::mutex> lock(mtx);
        if (topic.rfind("orderbook.", 0) == 0) {
            const std::string type = j.value("type", "");
            const auto& d = j["data"];
            auto bids = str_levels(d["b"]);
            auto asks = str_levels(d["a"]);
            Timestamp ts = (j.contains("cts") ? j["cts"].get<int64_t>() : j.value("ts", int64_t(0))) * 1000;
            if (type == "snapshot") {
                if (!recorder.header_written()) {
                    auto [tick, lot] = detect_tick_lot(bids, asks);
                    recorder.write_instrument(sym, tick, lot);
                    std::cout << "[bybit-capture] Snapshot: " << bids.size() + asks.size()
                              << " levels, tick=" << tick << " lot=" << lot << "\n";
                }
                recorder.record_book(ts, true, bids, asks);
                initialized = true;
            } else if (type == "delta" && initialized) {
                recorder.record_book(ts, false, bids, asks);
            }
            ++book_msgs;
        } else if (topic.rfind("publicTrade.", 0) == 0 && initialized) {
            for (const auto& t : j["data"]) {
                // RPI fills match hidden retail-only orders, not the visible book.
                if (t.contains("RPI") && t["RPI"].is_boolean() && t["RPI"].get<bool>()) continue;
                Side aggr = (t.value("S", "Buy") == "Buy") ? Side::Bid : Side::Ask;
                recorder.record_trade(t.value("T", int64_t(0)) * 1000, aggr,
                                      t["p"].get<std::string>(), t["v"].get<std::string>());
                ++trade_msgs;
            }
        }
    });

    ws.start();
    std::cout << "[bybit-capture] Waiting for initial snapshot...\n";
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::lock_guard<std::mutex> lock(mtx);
        if (initialized) break;
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!initialized) {
            std::cerr << "[bybit-capture] Timed out waiting for snapshot.\n";
            ws.stop();
            return;
        }
    }
    wait_for_duration(duration_minutes, mtx, book_msgs, trade_msgs, "bybit-capture");
    ws.stop();
    recorder.flush();
    std::cout << "[bybit-capture] Done: " << book_msgs << " book msgs, " << trade_msgs
              << " trades → " << output_path << "\n";
}

// ── Binance ──────────────────────────────────────────────────────────────────

static void run_binance_capture(const std::string& symbol, int duration_minutes,
                                const std::string& output_path) {
    const std::string lo = str_lower(symbol), up = str_upper(symbol);
    const std::string ws_url   = "wss://stream.binance.com:9443/stream?streams=" + lo +
                                 "@depth@100ms/" + lo + "@aggTrade";
    const std::string rest_url = "https://api.binance.com/api/v3/depth?symbol=" + up + "&limit=1000";

    BookSync  sync;
    OrderBook book;
    Recorder  recorder(output_path);
    std::mutex mtx;
    bool   initialized = false;
    size_t book_msgs = 0, trade_msgs = 0;
    std::vector<std::string> buffered_raw;  // raw depth payloads before snapshot

    auto record_depth = [&](const json& d) {
        recorder.record_book(d.value("E", int64_t(0)) * 1000, false, str_levels(d["b"]), str_levels(d["a"]));
        ++book_msgs;
    };

    ix::WebSocket ws;
    ws.setUrl(ws_url);
    ws.setHandshakeTimeout(10);
    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type != ix::WebSocketMessageType::Message) {
            if (msg->type == ix::WebSocketMessageType::Error)
                std::cerr << "[binance-capture] Error: " << msg->errorInfo.reason << "\n";
            return;
        }
        json j;
        try { j = json::parse(msg->str); } catch (...) { return; }
        if (!j.contains("data")) return;
        const json& d = j["data"];
        const std::string ev = d.value("e", "");
        std::lock_guard<std::mutex> lock(mtx);
        if (ev == "depthUpdate") {
            BinanceDepthEvent be = Parser::parse_depth_event(d.dump());
            if (!initialized) {
                sync.buffer_event(be);
                buffered_raw.push_back(d.dump());
            } else if (sync.apply_event(be, book)) {
                record_depth(d);
            } else {
                std::cerr << "[binance-capture] Sequence gap — stopping.\n";
                g_stop_capture = true;
            }
        } else if (ev == "aggTrade" && initialized) {
            // m = buyer is maker → the aggressor sold.
            Side aggr = d.value("m", false) ? Side::Ask : Side::Bid;
            recorder.record_trade(d.value("T", int64_t(0)) * 1000, aggr, d["p"].get<std::string>(),
                                  d["q"].get<std::string>());
            ++trade_msgs;
        }
    });

    ws.start();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ix::HttpClient http;
    auto args = http.createRequest(rest_url, "GET");
    args->connectTimeout  = 10;
    args->transferTimeout = 30;
    auto response = http.request(rest_url, "GET", "", args);
    if (response->statusCode != 200) {
        std::cerr << "[binance-capture] Snapshot fetch failed: HTTP " << response->statusCode
                  << (response->statusCode == 451 ? " (geo-blocked; use --exchange bybit)" : "") << "\n";
        ws.stop();
        return;
    }
    json snap_j = json::parse(response->body);
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto snap = Parser::parse_snapshot(response->body);
        if (!sync.initialize_from_snapshot(snap, book)) {
            std::cerr << "[binance-capture] BookSync init failed (gap). Retry.\n";
            ws.stop();
            return;
        }
        auto bids = str_levels(snap_j["bids"]);
        auto asks = str_levels(snap_j["asks"]);
        auto [tick, lot] = detect_tick_lot(bids, asks);
        recorder.write_instrument(up, tick, lot);
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();
        recorder.record_book(now_us, true, bids, asks);
        // Buffered events already applied by BookSync after the snapshot.
        for (const auto& raw : buffered_raw) {
            json d = json::parse(raw);
            if (d["u"].get<uint64_t>() > snap.last_update_id) record_depth(d);
        }
        buffered_raw.clear();
        initialized = true;
    }
    wait_for_duration(duration_minutes, mtx, book_msgs, trade_msgs, "binance-capture");
    ws.stop();
    recorder.flush();
    std::cout << "[binance-capture] Done: " << book_msgs << " book msgs, " << trade_msgs << " trades.\n";
}

static void run_live_capture(const std::string& symbol, int duration_minutes,
                             const std::string& output_path, const std::string& exchange = "bybit") {
    ix::initNetSystem();
    g_stop_capture.store(false);
    if (exchange == "binance") run_binance_capture(symbol, duration_minutes, output_path);
    else                       run_bybit_capture(symbol, duration_minutes, output_path);
    ix::uninitNetSystem();
}
