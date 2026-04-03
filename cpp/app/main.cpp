#include "../include/core/order_book.hpp"
#include "../include/core/types.hpp"
#include "../include/marketdata/parser.hpp"
#include "../include/marketdata/replay_engine.hpp"
#include "../include/marketdata/recorder.hpp"
#include "../include/sim/exchange_sim.hpp"
#include "../include/sim/portfolio.hpp"
#include "../include/strategy/market_maker.hpp"
#include "../include/strategy/imbalance_strategy.hpp"
#include "../include/util/csv_writer.hpp"
#include "../include/util/timer.hpp"
#include "live_capture.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <random>
#include <cmath>

namespace fs = std::filesystem;

// ─── helpers ────────────────────────────────────────────────────────────────

static std::string timestamp_dir() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string("run_") + buf;
}

static void print_help() {
    std::cout << R"(
Usage: lob_sim <mode> [options]

Modes:
  --synthetic-demo
        Generate 1000 synthetic depth events, run book, print top of book.

  --load-snapshot <path>
        Load a Binance JSON snapshot file, print best bid/ask/spread/imbalance.

  --replay <csv_path>
        Replay a normalized depth_events.csv with no strategy.
        Exports book_metrics.csv.

  --strategy mm --replay <csv_path> [--output <dir>]
        Run MarketMaker strategy over replay.
        Exports fills.csv, pnl.csv, book_metrics.csv.

  --strategy imbalance --replay <csv_path> [--output <dir>]
        Run ImbalanceStrategy over replay.
        Exports fills.csv, pnl.csv, book_metrics.csv.

  --live-capture <SYMBOL> --output <csv_path> [--duration-minutes <N>]
        Connect to Binance live diff-depth WebSocket, sync book via BookSync,
        and record events to normalized depth_events.csv for later replay.
        Default: 60 minutes. Press Ctrl+C to stop early.
        Example: --live-capture BTCUSDT --output data/recorded/btc_live.csv

Options:
  --output <path>           Output directory (replay) or CSV file (live-capture)
  --duration-minutes <N>    Duration for live capture (default: 60)
  --help                    Show this help

)";
}

// ─── synthetic demo ──────────────────────────────────────────────────────────

static void run_synthetic_demo() {
    std::cout << "[synthetic-demo] Generating 1000 synthetic depth events...\n";

    OrderBook book;
    std::mt19937_64 rng(42);

    // Seed the book with some initial levels
    BookSnapshot snap{};
    snap.last_update_id = 100;
    Price base_bid = 6500000;  // 65000.00 in ticks (×100)
    Price base_ask = 6500100;  // 65001.00
    for (int i = 0; i < 10; ++i) {
        snap.bids.push_back({base_bid - i * 100, 1000 + (Qty)(rng() % 5000)});
        snap.asks.push_back({base_ask + i * 100, 1000 + (Qty)(rng() % 5000)});
    }
    book.load_snapshot(snap);

    // Apply 1000 random updates
    UpdateId uid = 101;
    for (int i = 0; i < 1000; ++i) {
        // Random bid update
        Price px  = base_bid - (Price)(rng() % 10) * 100;
        Qty   qty = (rng() % 3 == 0) ? 0 : (Qty)(rng() % 8000);
        book.apply_update(Side::Bid, px, qty);

        px  = base_ask + (Price)(rng() % 10) * 100;
        qty = (rng() % 3 == 0) ? 0 : (Qty)(rng() % 8000);
        book.apply_update(Side::Ask, px, qty);
        book.set_update_id(uid++);
    }

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::cout << "  Best bid: " << (bb ? bb->price : 0) / TICK_MULTIPLIER
              << " qty=" << (bb ? bb->qty : 0) << "\n";
    std::cout << "  Best ask: " << (ba ? ba->price : 0) / TICK_MULTIPLIER
              << " qty=" << (ba ? ba->qty : 0) << "\n";
    std::cout << "  Mid:      " << book.mid_price() / TICK_MULTIPLIER << "\n";
    std::cout << "  Spread:   " << book.spread() << " ticks\n";
    std::cout << "  Imbal(5): " << book.imbalance(5) << "\n";

    auto top_bids = book.top_n_bids(3);
    auto top_asks = book.top_n_asks(3);
    std::cout << "\n  Top 3 bids:\n";
    for (const auto& l : top_bids)
        std::cout << "    " << l.price / TICK_MULTIPLIER << " × " << l.qty << "\n";
    std::cout << "  Top 3 asks:\n";
    for (const auto& l : top_asks)
        std::cout << "    " << l.price / TICK_MULTIPLIER << " × " << l.qty << "\n";

    std::cout << "[synthetic-demo] Done.\n";
}

// ─── load snapshot ───────────────────────────────────────────────────────────

static void run_load_snapshot(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open: " << path << "\n";
        return;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    BookSnapshot snap = Parser::parse_snapshot(content);
    OrderBook book;
    book.load_snapshot(snap);

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::cout << "Snapshot loaded. last_update_id=" << snap.last_update_id << "\n";
    std::cout << "  Best bid: " << (bb ? bb->price : 0) / TICK_MULTIPLIER
              << " qty=" << (bb ? bb->qty : 0) << "\n";
    std::cout << "  Best ask: " << (ba ? ba->price : 0) / TICK_MULTIPLIER
              << " qty=" << (ba ? ba->qty : 0) << "\n";
    std::cout << "  Mid:      " << book.mid_price() / TICK_MULTIPLIER << "\n";
    std::cout << "  Spread:   " << book.spread() << " ticks\n";
    std::cout << "  Imbal(5): " << book.imbalance(5) << "\n";
}

// ─── replay (with optional strategy) ────────────────────────────────────────

static void run_replay(const std::string& csv_path,
                       const std::string& strategy_name,
                       const std::string& output_dir) {

    fs::create_directories(output_dir);

    CsvWriter metrics_writer(output_dir + "/book_metrics.csv", {
        "ts","best_bid_ticks","best_bid_qty","best_ask_ticks","best_ask_qty",
        "spread_ticks","mid_ticks","imbalance_5"
    });

    std::unique_ptr<CsvWriter> fills_writer;
    std::unique_ptr<CsvWriter> pnl_writer;
    std::unique_ptr<Strategy>  strategy;
    Portfolio  portfolio;
    ExchangeSim exchange;

    bool has_strategy = !strategy_name.empty();

    if (has_strategy) {
        fills_writer = std::make_unique<CsvWriter>(output_dir + "/fills.csv",
            std::vector<std::string>{"ts","order_id","strategy_name","side","order_type",
                                     "fill_price_ticks","fill_qty","fee","status"});
        pnl_writer = std::make_unique<CsvWriter>(output_dir + "/pnl.csv",
            std::vector<std::string>{"ts","position","cash","realized_pnl",
                                     "unrealized_pnl","mark_mid_ticks","total_pnl"});

        if (strategy_name == "mm") {
            strategy = std::make_unique<MarketMaker>();
        } else if (strategy_name == "imbalance") {
            strategy = std::make_unique<ImbalanceStrategy>();
        } else {
            std::cerr << "Unknown strategy: " << strategy_name << "\n";
            return;
        }
    }

    OrderBook book;
    bool book_initialized = false;
    Timer timer;
    timer.start();
    size_t event_count = 0;

    auto write_fill = [&](const ExecutionReport& rep, const std::string& order_type) {
        if (!fills_writer) return;
        fills_writer->write_row({
            std::to_string(rep.ts),
            std::to_string(rep.order_id),
            rep.strategy_name,
            (rep.side == Side::Bid ? "B" : "A"),
            order_type,
            std::to_string(rep.fill_price),
            std::to_string(rep.fill_qty),
            std::to_string(rep.fee_paid),
            [&]() -> std::string {
                switch (rep.status) {
                    case OrderStatus::Filled:          return "Filled";
                    case OrderStatus::PartiallyFilled: return "PartialFill";
                    case OrderStatus::Cancelled:       return "Cancelled";
                    case OrderStatus::Rejected:        return "Rejected";
                    default:                           return "New";
                }
            }()
        });
    };

    ReplayEngine engine(csv_path);
    engine.replay([&](const BinanceDepthEvent& ev) {
        // First event: use it to bootstrap the book if not yet initialized
        if (!book_initialized) {
            // Build a minimal snapshot from the first event's update IDs
            // (In real usage, a REST snapshot would be used. For replay we just
            // trust the CSV is already in order and initialize from update_id - 1.)
            book.set_update_id(ev.first_update_id > 0 ? ev.first_update_id - 1 : 0);
            book_initialized = true;
        }

        // Apply event to book
        for (const auto& lvl : ev.bid_updates)
            book.apply_update(Side::Bid, lvl.price, lvl.qty);
        for (const auto& lvl : ev.ask_updates)
            book.apply_update(Side::Ask, lvl.price, lvl.qty);
        book.set_update_id(ev.final_update_id);

        // Process passive orders first (queue advancement)
        if (has_strategy) {
            exchange.on_market_event(ev, book, portfolio);
        }

        // Write book metrics
        auto bb = book.best_bid();
        auto ba = book.best_ask();
        if (bb && ba) {
            metrics_writer.write_row({
                std::to_string(ev.event_time),
                std::to_string(bb->price),
                std::to_string(bb->qty),
                std::to_string(ba->price),
                std::to_string(ba->qty),
                std::to_string(static_cast<int64_t>(book.spread())),
                std::to_string(static_cast<int64_t>(book.mid_price())),
                std::to_string(book.imbalance(5))
            });

            // Run strategy
            if (has_strategy && strategy) {
                strategy->on_market_event(ev.event_time, book, exchange, portfolio);

                // Drain all fills (passive + market orders from this event)
                for (const auto& rep : exchange.drain_fills()) {
                    std::string otype = (rep.status == OrderStatus::Cancelled) ? "Limit"
                                      : (rep.fill_price == 0) ? "Limit" : "Market";
                    write_fill(rep, otype);
                }

                // Write PnL snapshot
                double mid = book.mid_price();
                pnl_writer->write_row({
                    std::to_string(ev.event_time),
                    std::to_string(portfolio.position()),
                    std::to_string(portfolio.cash()),
                    std::to_string(portfolio.realized_pnl()),
                    std::to_string(portfolio.unrealized_pnl(mid)),
                    std::to_string(static_cast<int64_t>(mid)),
                    std::to_string(portfolio.total_pnl(mid))
                });
            }
        }

        ++event_count;
    });

    timer.stop();
    double elapsed_ms = timer.elapsed_ms();
    double eps = (elapsed_ms > 0) ? (event_count / elapsed_ms * 1000.0) : 0.0;

    std::cout << "Replayed " << event_count << " events in "
              << elapsed_ms << " ms (" << static_cast<int>(eps) << " events/sec)\n";
    std::cout << "Output written to: " << output_dir << "\n";

    if (has_strategy) {
        auto bb = book.best_bid();
        double mark = bb ? bb->price : 0.0;
        std::cout << "Final PnL: " << portfolio.total_pnl(mark) << "\n";
        std::cout << "Final position: " << portfolio.position() << "\n";
    }
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 0; }

    std::string mode;
    std::string csv_path;
    std::string snapshot_path;
    std::string strategy_name;
    std::string output_dir;
    std::string capture_symbol;
    std::string exchange = "bybit";  // default: bybit (US-accessible)
    int         duration_minutes = 60;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_help(); return 0; }
        else if (arg == "--synthetic-demo") mode = "synthetic";
        else if (arg == "--load-snapshot" && i + 1 < argc) {
            mode = "snapshot"; snapshot_path = argv[++i];
        }
        else if (arg == "--replay" && i + 1 < argc) {
            if (mode.empty()) mode = "replay";
            csv_path = argv[++i];
        }
        else if (arg == "--strategy" && i + 1 < argc) {
            strategy_name = argv[++i];
            mode = "replay";
        }
        else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        }
        else if (arg == "--live-capture" && i + 1 < argc) {
            mode = "live";
            capture_symbol = argv[++i];
        }
        else if (arg == "--duration-minutes" && i + 1 < argc) {
            duration_minutes = std::stoi(argv[++i]);
        }
        else if (arg == "--exchange" && i + 1 < argc) {
            exchange = argv[++i];
        }
    }

    if (mode == "synthetic") {
        run_synthetic_demo();
    } else if (mode == "snapshot") {
        run_load_snapshot(snapshot_path);
    } else if (mode == "replay") {
        if (csv_path.empty()) {
            std::cerr << "Error: --replay requires a CSV path\n";
            return 1;
        }
        if (output_dir.empty()) {
            output_dir = "data/processed/" + timestamp_dir();
        }
        run_replay(csv_path, strategy_name, output_dir);
    } else if (mode == "live") {
        if (capture_symbol.empty()) capture_symbol = "BTCUSDT";
        if (output_dir.empty()) {
            std::cerr << "Error: --live-capture requires --output <csv_path>\n";
            return 1;
        }
        run_live_capture(capture_symbol, duration_minutes, output_dir, exchange);
    } else {
        print_help();
    }

    return 0;
}
