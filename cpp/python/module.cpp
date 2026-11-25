// Python bindings: run a backtest with a built-in strategy or one written in
// Python, read order-level files, and use the order book directly.
#include "../include/core/l3_book.hpp"
#include "../include/marketdata/event_source.hpp"
#include "../include/marketdata/itch_l3_extractor.hpp"
#include "../include/marketdata/l3_format.hpp"
#include "../include/strategy/run_setup.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <filesystem>

namespace py = pybind11;

namespace {

// Flat, NumPy-friendly rows.
struct FillRow {
    int64_t  ts;
    uint64_t order_id;
    int8_t   side;        // +1 buy, -1 sell
    int8_t   maker;       // 1 maker, 0 taker
    int8_t   cause;       // FillCause
    int8_t   pad[5];
    double   price, qty, fee, mid_at_fill, queue_ahead_at_entry, queue_ahead_at_fill, position_after;
    double   mid_100ms, mid_1s, mid_5s, mid_30s, mid_60s, mid_300s;
};

template <typename T>
py::array_t<T> to_array(const std::vector<T>& v) {
    py::array_t<T> a(static_cast<py::ssize_t>(v.size()));
    if (!v.empty()) std::memcpy(a.mutable_data(), v.data(), v.size() * sizeof(T));
    return a;
}

std::vector<FillRow> fill_rows(const std::vector<FillRecord>& fills, const Instrument& inst) {
    std::vector<FillRow> out;
    out.reserve(fills.size());
    const double tick = inst.tick_size;
    for (const FillRecord& f : fills) {
        FillRow r{};
        r.ts = f.ts;
        r.order_id = f.order_id;
        r.side  = f.side == Side::Bid ? 1 : -1;
        r.maker = f.liquidity == Liquidity::Maker ? 1 : 0;
        r.cause = static_cast<int8_t>(f.cause);
        r.price = inst.price(f.price);
        r.qty   = inst.qty(f.qty);
        r.fee   = f.fee;
        r.mid_at_fill = f.mid_at_fill * tick;
        r.queue_ahead_at_entry = f.queue_ahead_at_entry * inst.lot_size;
        r.queue_ahead_at_fill  = f.queue_ahead_at_fill * inst.lot_size;
        r.position_after = inst.qty(f.position_after);
        r.mid_100ms = f.mid_after[0] * tick; r.mid_1s = f.mid_after[1] * tick; r.mid_5s = f.mid_after[2] * tick;
        r.mid_30s = f.mid_after[3] * tick; r.mid_60s = f.mid_after[4] * tick; r.mid_300s = f.mid_after[5] * tick;
        out.push_back(r);
    }
    return out;
}

// Contexts are passed by pointer so pybind11 references them instead of
// copying; they are only valid during the callback.
class PyStrategy : public Strategy {
public:
    using Strategy::Strategy;
    std::string name() const override {
        py::gil_scoped_acquire gil;
        if (py::function f = py::get_override(static_cast<const Strategy*>(this), "name"))
            return f().cast<std::string>();
        return "PythonStrategy";
    }
    void on_start(StrategyContext& c) override { call("on_start", c); }
    void on_book(StrategyContext& c) override { call("on_book", c); }
    void on_timer(StrategyContext& c) override { call("on_timer", c); }
    void on_finish(StrategyContext& c) override { call("on_finish", c); }
    void on_trade(StrategyContext& c, const TradePrint& t) override {
        if (py::function f = py::get_override(static_cast<const Strategy*>(this), "on_trade"))
            f(py::cast(&c, py::return_value_policy::reference), t);
    }
    void on_order_update(StrategyContext& c, const ExecutionReport& r) override {
        if (py::function f = py::get_override(static_cast<const Strategy*>(this), "on_order_update"))
            f(py::cast(&c, py::return_value_policy::reference), r);
    }
    int64_t timer_interval_us() const override {
        if (py::object o = py::cast(this, py::return_value_policy::reference); py::hasattr(o, "timer_us"))
            return o.attr("timer_us").cast<int64_t>();
        return 0;
    }

private:
    void call(const char* fn, StrategyContext& c) {
        if (py::function f = py::get_override(static_cast<const Strategy*>(this), fn))
            f(py::cast(&c, py::return_value_policy::reference));
    }
};

TimeInForce tif_of(const std::string& s) {
    if (s == "post_only") return TimeInForce::PostOnly;
    if (s == "gtc") return TimeInForce::GTC;
    if (s == "ioc") return TimeInForce::IOC;
    throw std::invalid_argument("tif must be post_only, gtc or ioc, not " + s);
}

const char* status_name(OrderStatus s) {
    switch (s) {
        case OrderStatus::New: return "new";
        case OrderStatus::PartiallyFilled: return "partially_filled";
        case OrderStatus::Filled: return "filled";
        case OrderStatus::Cancelled: return "cancelled";
        case OrderStatus::Rejected: return "rejected";
        case OrderStatus::CancelRejected: return "cancel_rejected";
    }
    return "?";
}

Side side_of(const std::string& s) {
    if (s == "buy" || s == "bid") return Side::Bid;
    if (s == "sell" || s == "ask") return Side::Ask;
    throw std::invalid_argument("side must be buy or sell, not " + s);
}

Options to_options(const std::map<std::string, std::string>& opts) {
    Options o;
    for (const auto& [k, v] : opts) o.set(k, v);
    return o;
}

py::tuple run(const std::string& book, const std::string& trades, py::object strategy,
              const std::map<std::string, std::string>& opts, const std::string& output, bool equity) {
    const Options o = to_options(opts);
    auto src = open_event_source(book, trades, o.num("--tick-size", 0.0), o.num("--lot-size", 0.0));
    const Instrument inst = src->instrument();
    const bool builtin = py::isinstance<py::str>(strategy);
    const std::string name = builtin ? strategy.cast<std::string>() : "python";
    RunSetup rs = make_run_setup(o, inst, name, output);
    rs.cfg.quiet = true;
    rs.cfg.record_equity = equity;
    rs.cfg.run_config["strategy"] = name;
    std::unique_ptr<Strategy> owned;
    Strategy* strat = nullptr;
    if (builtin) {
        if (!output.empty()) std::filesystem::create_directories(output);
        owned = make_strategy(name, o, inst, rs.order_qty, rs.max_pos, output);
        strat = owned.get();
    } else {
        strat = strategy.cast<Strategy*>();
        // A Python strategy sizes its own orders: no gateway limit unless asked.
        rs.cfg.max_position = o.has("--max-position") ? inst.to_lots(o.num("--max-position", 0.0)) : 0;
    }
    Backtester bt(*src, *strat, rs.cfg);
    RunSummary s;
    if (builtin) {
        py::gil_scoped_release nogil;   // pure C++: other Python threads may run backtests in parallel
        s = bt.run();
    } else {
        s = bt.run();
    }
    return py::make_tuple(Backtester::summary_json(s, rs.cfg.run_config), to_array(fill_rows(bt.fills(), inst)),
                          to_array(bt.equity()));
}

py::array_t<L3Record> read_l3(const std::string& path) {
    L3FileReader rd(path);
    std::vector<L3Record> v;
    v.reserve(rd.header().count);
    while (const L3Record* r = rd.next()) v.push_back(*r);
    return to_array(v);
}

void write_l3(const std::string& path, const std::string& symbol, double tick, double lot,
              py::array_t<L3Record, py::array::c_style | py::array::forcecast> records) {
    L3FileWriter w(path, symbol, tick, lot);
    const auto r = records.unchecked<1>();
    for (py::ssize_t i = 0; i < r.shape(0); ++i) w.write(r(i));
    w.close();
}

std::vector<std::pair<Price, Qty>> levels(const std::vector<Level>& v) {
    std::vector<std::pair<Price, Qty>> out;
    for (const Level& l : v) out.emplace_back(l.price, l.qty);
    return out;
}

py::object level(const std::optional<Level>& l) {
    if (!l) return py::none();
    return py::make_tuple(l->price, l->qty);
}

}  // namespace

PYBIND11_MODULE(_lobsim, m) {
    m.doc() = "lobsim C++ engine (use the lobsim package)";
    PYBIND11_NUMPY_DTYPE(L3Record, ts, id, price, qty, aux, type, side);
    PYBIND11_NUMPY_DTYPE(FillRow, ts, order_id, side, maker, cause, price, qty, fee, mid_at_fill, queue_ahead_at_entry,
                         queue_ahead_at_fill, position_after, mid_100ms, mid_1s, mid_5s, mid_30s, mid_60s, mid_300s);
    PYBIND11_NUMPY_DTYPE(EquitySample, ts, mid, position, gross_pnl, fees, net_pnl, fills);

    m.def("run", &run, py::arg("book"), py::arg("trades"), py::arg("strategy"), py::arg("options"), py::arg("output"),
          py::arg("equity"));
    m.def("is_flag", &Options::is_flag);
    m.def("read_l3", &read_l3, py::arg("path"), "Read an .l3bin(.gz) file into a structured array.");
    m.def("write_l3", &write_l3, py::arg("path"), py::arg("symbol"), py::arg("tick"), py::arg("lot"), py::arg("records"),
          "Write a structured array (see read_l3) to an .l3bin file.");
    m.def("instrument", [](const std::string& path) {
        auto s = open_event_source(path, "");
        const Instrument& i = s->instrument();
        return py::make_tuple(i.symbol, i.tick_size, i.lot_size);
    }, py::arg("path"), "(symbol, tick size, lot size) of a data file.");

    py::class_<TradePrint>(m, "Trade", "A public trade print.")
        .def_property_readonly("side", [](const TradePrint& t) { return t.aggressor == Side::Bid ? "buy" : "sell"; },
                               "Aggressor side.")
        .def_readonly("price", &TradePrint::price, "Ticks.")
        .def_readonly("qty", &TradePrint::qty, "Lots.");

    py::class_<ExecutionReport>(m, "Report", "An order update from the exchange.")
        .def_readonly("order_id", &ExecutionReport::order_id)
        .def_readonly("ts", &ExecutionReport::ts)
        .def_property_readonly("side", [](const ExecutionReport& r) { return r.side == Side::Bid ? "buy" : "sell"; })
        .def_property_readonly("status", [](const ExecutionReport& r) { return status_name(r.status); })
        .def_readonly("price", &ExecutionReport::price)
        .def_readonly("fill_price", &ExecutionReport::fill_price)
        .def_readonly("fill_qty", &ExecutionReport::fill_qty)
        .def_readonly("cum_qty", &ExecutionReport::cum_qty)
        .def_readonly("leaves_qty", &ExecutionReport::leaves_qty)
        .def_readonly("fee", &ExecutionReport::fee)
        .def_property_readonly("maker", [](const ExecutionReport& r) { return r.liquidity == Liquidity::Maker; });

    py::class_<StrategyContext>(m, "Context",
                                "What a strategy can see and do at this moment. Prices are in ticks and sizes in lots. "
                                "Valid only during the callback it was passed to.")
        .def_property_readonly("now", &StrategyContext::now, "Local time, microseconds.")
        .def_property_readonly("position", &StrategyContext::position, "Lots, as known locally.")
        .def_property_readonly("tick_size", [](const StrategyContext& c) { return c.instrument().tick_size; })
        .def_property_readonly("lot_size", [](const StrategyContext& c) { return c.instrument().lot_size; })
        .def_property_readonly("best_bid", [](const StrategyContext& c) { return level(c.book().best_bid()); },
                               "(price, qty) or None.")
        .def_property_readonly("best_ask", [](const StrategyContext& c) { return level(c.book().best_ask()); },
                               "(price, qty) or None.")
        .def_property_readonly("mid", [](const StrategyContext& c) { return c.book().mid_price(); }, "Ticks.")
        .def_property_readonly("weighted_mid", [](const StrategyContext& c) { return c.book().weighted_mid(); }, "Ticks.")
        .def_property_readonly("spread", [](const StrategyContext& c) { return c.book().spread(); }, "Ticks.")
        .def("imbalance", [](const StrategyContext& c, int n) { return c.book().imbalance(n); }, py::arg("levels") = 1)
        .def("bids", [](const StrategyContext& c, int n) { return levels(c.book().top_n_bids(n)); }, py::arg("n") = 5)
        .def("asks", [](const StrategyContext& c, int n) { return levels(c.book().top_n_asks(n)); }, py::arg("n") = 5)
        .def_property_readonly("maker_fee", &StrategyContext::maker_fee)
        .def_property_readonly("taker_fee", &StrategyContext::taker_fee)
        .def("limit", [](StrategyContext& c, const std::string& side, Price px, Qty qty, const std::string& tif) {
                return c.submit_limit(side_of(side), px, qty, tif_of(tif));
            }, py::arg("side"), py::arg("price"), py::arg("qty"), py::arg("tif") = "post_only",
            "Send a limit order; returns its id, or 0 if the risk gateway refused it.")
        .def("market", [](StrategyContext& c, const std::string& side, Qty qty) { return c.submit_market(side_of(side), qty); },
             py::arg("side"), py::arg("qty"))
        .def("cancel", &StrategyContext::cancel, py::arg("order_id"))
        .def("cancel_all", [](StrategyContext& c) {
            std::vector<OrderId> ids;
            for (const auto& [id, o] : c.orders())
                if (o.type == OrderType::Limit && !o.cancel_sent) ids.push_back(id);
            for (OrderId id : ids) c.cancel(id);
            return ids.size();
        })
        .def_property_readonly("orders", [](const StrategyContext& c) {
            py::dict d;
            for (const auto& [id, o] : c.orders()) {
                py::dict e;
                e["side"] = o.side == Side::Bid ? "buy" : "sell";
                e["price"] = o.price;
                e["qty"] = o.qty;
                e["filled"] = o.filled;
                e["acked"] = o.acked;
                e["cancel_sent"] = o.cancel_sent;
                d[py::int_(id)] = e;
            }
            return d;
        }, "Live orders by id, as known locally.");

    py::class_<Strategy, PyStrategy>(m, "Strategy",
                                     "Subclass and define any of on_start(ctx), on_book(ctx), on_trade(ctx, trade), "
                                     "on_order_update(ctx, report), on_timer(ctx), on_finish(ctx), and name(). Set "
                                     "`timer_us` for a timer.")
        .def(py::init<>());

    py::class_<L3Book>(m, "Book", "Order-level book with price-time priority.")
        .def(py::init<>())
        .def("add", [](L3Book& b, uint64_t id, const std::string& s, Price p, Qty q) { return b.add(id, side_of(s), p, q); },
             py::arg("id"), py::arg("side"), py::arg("price"), py::arg("qty"))
        .def("cancel", &L3Book::cancel, py::arg("id"))
        .def("modify", &L3Book::modify, py::arg("id"), py::arg("qty"),
             "A decrease keeps queue priority, an increase loses it.")
        .def("execute", &L3Book::execute, py::arg("id"), py::arg("qty"))
        .def("match", [](L3Book& b, const std::string& aggressor, Price limit, Qty qty) {
                std::vector<std::tuple<uint64_t, Price, Qty>> fills;
                b.match(side_of(aggressor), limit, qty, [&](uint64_t id, Price p, Qty q) { fills.emplace_back(id, p, q); });
                return fills;
            }, py::arg("aggressor"), py::arg("limit"), py::arg("qty"), "List of (maker id, price, qty).")
        .def("queue_ahead", [](const L3Book& b, uint64_t id) -> py::object {
                if (!b.contains(id)) return py::none();
                return py::int_(b.queue_ahead(id));
            }, py::arg("id"), "Quantity ahead of the order at its price, or None.")
        .def("levels", [](const L3Book& b, const std::string& s, size_t n) {
                std::vector<Level> v;
                b.top_levels(side_of(s), n, v);
                return levels(v);
            }, py::arg("side"), py::arg("n") = 5)
        .def("apply", [](L3Book& b, py::array_t<L3Record, py::array::c_style | py::array::forcecast> recs) {
                const auto r = recs.unchecked<1>();
                for (py::ssize_t i = 0; i < r.shape(0); ++i) {
                    const L3Record& x = r(i);
                    switch (static_cast<L3Type>(x.type)) {
                        case L3Type::Add: b.add(x.id, l3_side(x.side), x.price, x.qty); break;
                        case L3Type::Delete: b.cancel(x.id); break;
                        case L3Type::Modify: b.modify(x.id, x.qty); break;
                        case L3Type::Trade: break;   // prints don't change the book
                    }
                }
            }, py::arg("records"), "Apply records from read_l3 in order.")
        .def("__len__", &L3Book::order_count);
}
