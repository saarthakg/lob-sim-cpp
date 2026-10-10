#include "../../include/live/live_bybit_source.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {

int64_t mono_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void cpu_relax() {
#if defined(__aarch64__)
    asm volatile("yield");
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

}  // namespace

struct LiveBybitSource::Impl {
    ix::WebSocket ws;
};

LiveBybitSource::LiveBybitSource(Config cfg)
    : impl_(std::make_unique<Impl>()), cfg_(std::move(cfg)), ring_(cfg_.ring_capacity) {
    ix::initNetSystem();
    parse_ns_.reserve(1 << 20);
    const std::string sym = cfg_.symbol;
    const std::string sub = std::string(R"({"op":"subscribe","args":["orderbook.)") + std::to_string(cfg_.depth) +
                            "." + sym + R"(","publicTrade.)" + sym + R"("]})";
    auto& ws = impl_->ws;
    ws.setUrl(cfg_.url);
    ws.setHandshakeTimeout(15);
    ws.setPingInterval(20);
    ws.setOnMessageCallback([this, sub](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            ++opens_;
            impl_->ws.sendText(sub);
        } else if (msg->type == ix::WebSocketMessageType::Message) {
            on_frame(msg->str);
        }
    });
    ws.start();

    // Block until the first snapshot has fixed the instrument grid.
    const int64_t give_up = mono_ns() + 20'000'000'000LL;
    while (!ready_.load(std::memory_order_acquire)) {
        if (mono_ns() > give_up) {
            ws.stop();
            throw std::runtime_error("LiveBybitSource: no order-book snapshot within 20 s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    deadline_ns_ = mono_ns() + static_cast<int64_t>(cfg_.duration_s * 1e9);
}

LiveBybitSource::~LiveBybitSource() {
    impl_->ws.stop();
    ix::uninitNetSystem();
}

void LiveBybitSource::push(MarketEvent&& ev) {
    // Backpressure: if the engine falls behind by a full ring, wait rather
    // than drop (a dropped delta would corrupt the book).
    while (!ring_.try_push(std::move(ev))) {
        ++overruns_;
        if (stop_.load(std::memory_order_relaxed)) return;
        cpu_relax();
    }
}

void LiveBybitSource::on_frame(const std::string& text) {
    const int64_t recv = mono_ns();
    ++frames_;
    const char* s = text.c_str();
    const char* topic = std::strstr(s, "\"topic\":\"");
    if (!topic) return;
    topic += 9;

    if (std::strncmp(topic, "orderbook", 9) == 0) {
        if (!bybit::parse_book(s, book_msg_)) return;
        if (!ready_.load(std::memory_order_relaxed)) {
            if (!book_msg_.snapshot) return;
            grid_ = bybit::detect_grid(book_msg_, cfg_.tick_size, cfg_.lot_size);
            inst_.symbol    = cfg_.symbol;
            inst_.tick_size = static_cast<double>(grid_.tick_scaled) / decimal::SCALE;
            inst_.lot_size  = static_cast<double>(grid_.lot_scaled) / decimal::SCALE;
        }
        MarketEvent ev;
        ev.ts   = last_ts_ = std::max(book_msg_.ts, last_ts_);
        ev.type = book_msg_.snapshot ? EventType::Snapshot : EventType::Delta;
        ev.bids.reserve(book_msg_.bids.size());
        ev.asks.reserve(book_msg_.asks.size());
        for (const auto& l : book_msg_.bids) ev.bids.push_back({grid_.px(l.px), grid_.qty(l.qty)});
        for (const auto& l : book_msg_.asks) ev.asks.push_back({grid_.px(l.px), grid_.qty(l.qty)});
        ev.local_ns = recv;
        parse_ns_.push_back(mono_ns() - recv);
        ++book_n_;
        push(std::move(ev));
        ready_.store(true, std::memory_order_release);
    } else if (std::strncmp(topic, "publicTrade", 11) == 0) {
        if (!ready_.load(std::memory_order_relaxed)) return;
        trade_msgs_.clear();
        bybit::parse_trades(s, trade_msgs_);
        parse_ns_.push_back(mono_ns() - recv);
        for (const auto& t : trade_msgs_) {
            if (cfg_.skip_rpi && t.rpi) continue;
            MarketEvent ev;
            ev.ts       = last_ts_ = std::max(t.ts, last_ts_);
            ev.type     = EventType::Trade;
            ev.trade    = TradePrint{t.aggressor, grid_.px(t.px), grid_.qty(t.qty)};
            ev.local_ns = recv;
            ++trade_n_;
            push(std::move(ev));
        }
    }
}

bool LiveBybitSource::next(MarketEvent& ev) {
    uint32_t spins = 0;
    while (true) {
        if (ring_.try_pop(ev)) return true;
        if (stop_.load(std::memory_order_relaxed) || mono_ns() > deadline_ns_) return false;
        if (++spins < 4096) cpu_relax();
        else { std::this_thread::yield(); spins = 0; }
    }
}

LiveBybitSource::Stats LiveBybitSource::shutdown_and_stats() {
    stop();
    impl_->ws.stop();   // joins the WebSocket thread; parse_ns_ is ours from here
    Stats s;
    s.frames   = frames_.load();
    s.book_msgs = book_n_.load();
    s.trades   = trade_n_.load();
    s.overruns = overruns_.load();
    s.opens    = opens_.load();
    if (!parse_ns_.empty()) {
        std::vector<int64_t>& v = parse_ns_;
        std::sort(v.begin(), v.end());
        s.parse_p50_ns = static_cast<double>(v[v.size() / 2]);
        s.parse_p99_ns = static_cast<double>(v[static_cast<size_t>(0.99 * static_cast<double>(v.size() - 1))]);
    }
    return s;
}
