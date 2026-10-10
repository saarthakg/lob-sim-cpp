#include "../../include/marketdata/event_source.hpp"
#include "../../include/core/l3_book.hpp"
#include "../../include/marketdata/l3_format.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <stdexcept>

// ── LineReader ───────────────────────────────────────────────────────────────

static bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

LineReader::LineReader(const std::string& path) {
    if (ends_with(path, ".gz")) {
        fp_   = ::popen(("gzip -dc " + shell_quote(path)).c_str(), "r");
        pipe_ = true;
    } else if (ends_with(path, ".zip")) {
        fp_   = ::popen(("unzip -p " + shell_quote(path)).c_str(), "r");
        pipe_ = true;
    } else {
        fp_ = std::fopen(path.c_str(), "r");
    }
    if (!fp_) throw std::runtime_error("LineReader: cannot open " + path);
    // Probe that the file exists even when going through a pipe (popen
    // succeeds regardless; the decompressor fails later).
    if (pipe_) {
        FILE* probe = std::fopen(path.c_str(), "r");
        if (!probe) {
            ::pclose(fp_);
            fp_ = nullptr;
            throw std::runtime_error("LineReader: cannot open " + path);
        }
        std::fclose(probe);
    }
}

LineReader::~LineReader() {
    if (!fp_) return;
    if (pipe_) ::pclose(fp_);
    else std::fclose(fp_);
}

bool LineReader::getline(std::string& line) {
    line.clear();
    char buf[1 << 16];
    while (std::fgets(buf, sizeof(buf), fp_)) {
        size_t n = std::strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') {
            line.append(buf, n - 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line.append(buf, n);
    }
    return !line.empty();
}

// ── decimal ──────────────────────────────────────────────────────────────────

int64_t decimal::parse_scaled(const char*& p) {
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    int64_t ip = 0;
    while (*p >= '0' && *p <= '9') ip = ip * 10 + (*p++ - '0');
    int64_t frac = 0;
    int64_t mul  = SCALE;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (mul > 1) {
                mul /= 10;
                frac += (*p - '0') * mul;
            }
            ++p;
        }
    }
    // Scientific notation (rare: Bybit grossValue column, which we skip).
    if (*p == 'e' || *p == 'E') {
        ++p;
        bool eneg = false;
        if (*p == '-') { eneg = true; ++p; } else if (*p == '+') ++p;
        int e = 0;
        while (*p >= '0' && *p <= '9') e = e * 10 + (*p++ - '0');
        double v = (static_cast<double>(ip) + static_cast<double>(frac) / SCALE);
        for (int i = 0; i < e; ++i) v = eneg ? v / 10.0 : v * 10.0;
        int64_t r = static_cast<int64_t>(v * SCALE);
        return neg ? -r : r;
    }
    int64_t r = ip * SCALE + frac;
    return neg ? -r : r;
}

int64_t decimal::parse_scaled(const std::string& s) {
    const char* p = s.c_str();
    return parse_scaled(p);
}

// ── BybitHistoricalSource ────────────────────────────────────────────────────

BybitHistoricalSource::BybitHistoricalSource(Config cfg) : cfg_(std::move(cfg)) {
    book_ = std::make_unique<LineReader>(cfg_.book_path);
    if (!cfg_.trades_path.empty()) trades_ = std::make_unique<LineReader>(cfg_.trades_path);

    // Prime the book stream; the first message must be a snapshot.
    have_book_ = read_book(pend_book_);
    while (have_book_ && !pend_book_.snapshot) have_book_ = read_book(pend_book_);
    if (!have_book_) throw std::runtime_error("Bybit source: no snapshot in " + cfg_.book_path);
    grid_ = bybit::detect_grid(pend_book_, cfg_.tick_size, cfg_.lot_size);
    inst_.symbol    = cfg_.symbol.empty() ? "BYBIT" : cfg_.symbol;
    inst_.tick_size = static_cast<double>(grid_.tick_scaled) / decimal::SCALE;
    inst_.lot_size  = static_cast<double>(grid_.lot_scaled) / decimal::SCALE;

    if (trades_) {
        // Skip header row.
        if (trades_->getline(line_) && !line_.empty() && line_[0] >= '0' && line_[0] <= '9') {
            throw std::runtime_error("Bybit trades: expected a header row");
        }
        have_trade_ = read_trade(pend_trade_);
    }
}

bool BybitHistoricalSource::read_book(bybit::BookMsg& out) {
    while (book_->getline(line_)) {
        if (!bybit::parse_book(line_.c_str(), out)) continue;
        ++book_msgs_;
        return true;
    }
    return false;
}

bool BybitHistoricalSource::read_trade(RawTrade& out) {
    while (trades_->getline(line_)) {
        // timestamp,symbol,side,size,price,tickDirection,trdMatchID,grossValue,
        // homeNotional,foreignNotional[,RPI]
        const char* p = line_.c_str();
        // timestamp: seconds with fractional part → us (exact)
        int64_t ts_scaled = decimal::parse_scaled(p);  // seconds × 1e9
        if (*p != ',') continue;
        ++p;
        const char* sym_end = std::strchr(p, ',');
        if (!sym_end) continue;
        p = sym_end + 1;
        Side aggr = (*p == 'B') ? Side::Bid : Side::Ask;
        p = std::strchr(p, ',');
        if (!p) continue;
        ++p;
        int64_t size = decimal::parse_scaled(p);
        if (*p != ',') continue;
        ++p;
        int64_t price = decimal::parse_scaled(p);

        if (cfg_.skip_rpi_trades) {
            // RPI is the 11th column when present; "1"/"true" marks an RPI fill.
            const char* last = std::strrchr(line_.c_str(), ',');
            int commas = 0;
            for (const char* c = line_.c_str(); *c; ++c) commas += (*c == ',');
            if (commas >= 10 && last && (last[1] == '1' || last[1] == 't')) continue;
        }

        out.ts        = ts_scaled / 1000;  // 1e9 → 1e6 (us)
        out.aggressor = aggr;
        out.px        = price;
        out.qty       = size;
        ++trade_msgs_;
        return true;
    }
    return false;
}

bool BybitHistoricalSource::next(MarketEvent& ev) {
    if (!have_book_ && !have_trade_) return false;

    bool emit_trade = have_trade_ && (!have_book_ || pend_trade_.ts <= pend_book_.ts);
    if (emit_trade) {
        ev.ts   = pend_trade_.ts;
        ev.type = EventType::Trade;
        ev.bids.clear();
        ev.asks.clear();
        ev.trade.aggressor = pend_trade_.aggressor;
        ev.trade.price     = grid_.px(pend_trade_.px);
        ev.trade.qty       = grid_.qty(pend_trade_.qty);
        have_trade_ = read_trade(pend_trade_);
        return true;
    }

    ev.ts   = pend_book_.ts;
    ev.type = pend_book_.snapshot ? EventType::Snapshot : EventType::Delta;
    ev.bids.clear();
    ev.asks.clear();
    for (auto& l : pend_book_.bids) ev.bids.push_back({grid_.px(l.px), grid_.qty(l.qty)});
    for (auto& l : pend_book_.asks) ev.asks.push_back({grid_.px(l.px), grid_.qty(l.qty)});
    have_book_ = read_book(pend_book_);
    return true;
}

// ── NormalizedCsvSource ──────────────────────────────────────────────────────

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    while (true) {
        size_t c = line.find(',', start);
        if (c == std::string::npos) { f.push_back(line.substr(start)); break; }
        f.push_back(line.substr(start, c - start));
        start = c + 1;
    }
    return f;
}

NormalizedCsvSource::NormalizedCsvSource(const std::string& path) {
    reader_ = std::make_unique<LineReader>(path);
    inst_   = Instrument::legacy();
    std::string line;
    while (reader_->getline(line)) {
        if (line.rfind("#instrument", 0) == 0) {
            auto f = split_csv(line);
            if (f.size() >= 4) {
                inst_.symbol    = f[1];
                inst_.tick_size = std::stod(f[2]);
                inst_.lot_size  = std::stod(f[3]);
            }
            continue;
        }
        if (line.rfind("ts,event_type", 0) == 0) { legacy_ = true; break; }
        if (line.rfind("ts,type", 0) == 0) { legacy_ = false; break; }
        throw std::runtime_error("NormalizedCsvSource: unrecognised header in " + path);
    }
}

bool NormalizedCsvSource::parse_v2_row(const std::string& line, Timestamp& ts, char& type,
                                       Side& side, Price& px, Qty& qty) const {
    auto f = split_csv(line);
    if (f.size() < 5 || f[1].empty() || f[2].empty()) return false;
    ts   = std::stoll(f[0]);
    type = f[1][0];
    side = (f[2][0] == 'B') ? Side::Bid : Side::Ask;
    px   = inst_.to_ticks(std::stod(f[3]));
    qty  = inst_.to_lots(std::stod(f[4]));
    return true;
}

bool NormalizedCsvSource::next(MarketEvent& ev) {
    ev.bids.clear();
    ev.asks.clear();
    bool started = false;
    Timestamp cur_ts = 0;
    char cur_type = 0;
    std::string cur_key;  // legacy grouping key: first/final update ids

    while (true) {
        std::string line;
        if (have_pending_) {
            line = std::move(pending_);
            have_pending_ = false;
        } else if (!reader_->getline(line)) {
            return started;
        }
        if (line.empty() || line[0] == '#') continue;

        if (legacy_) {
            auto f = split_csv(line);
            if (f.size() < 7) continue;
            std::string key = f[2] + "/" + f[3];
            if (started && key != cur_key) {
                pending_ = std::move(line);
                have_pending_ = true;
                return true;
            }
            if (!started) {
                started = true;
                cur_key = key;
                ev.ts   = std::stoll(f[0]);
                ev.type = EventType::Delta;
            }
            Level l{std::stoll(f[5]), std::stoll(f[6])};
            (f[4][0] == 'B' ? ev.bids : ev.asks).push_back(l);
            continue;
        }

        Timestamp ts;
        char type;
        Side side;
        Price px;
        Qty qty;
        if (!parse_v2_row(line, ts, type, side, px, qty)) continue;

        if (type == 'T') {
            if (started) {  // close the pending book event first
                pending_ = std::move(line);
                have_pending_ = true;
                return true;
            }
            ev.ts   = ts;
            ev.type = EventType::Trade;
            ev.trade = TradePrint{side, px, qty};
            return true;
        }
        if (started && (ts != cur_ts || type != cur_type)) {
            pending_ = std::move(line);
            have_pending_ = true;
            return true;
        }
        if (!started) {
            started  = true;
            cur_ts   = ts;
            cur_type = type;
            ev.ts    = ts;
            ev.type  = (type == 'S') ? EventType::Snapshot : EventType::Delta;
        }
        (side == Side::Bid ? ev.bids : ev.asks).push_back({px, qty});
    }
}

// ── L3EventSource ────────────────────────────────────────────────────────────

L3EventSource::L3EventSource(const std::string& path)
    : reader_(std::make_unique<L3FileReader>(path)), book_(std::make_unique<L3Book>(18, 1 << 16)) {
    const L3FileHeader& h = reader_->header();
    inst_.symbol    = h.symbol;
    inst_.tick_size = h.tick_size;
    inst_.lot_size  = h.lot_size;
}

L3EventSource::~L3EventSource() = default;

bool L3EventSource::next(MarketEvent& ev) {
    const L3Record* r = reader_->next();
    if (!r) return false;
    ev.ts     = r->ts;
    ev.has_l3 = true;
    ev.l3     = *r;
    ev.bids.clear();
    ev.asks.clear();
    const auto type = static_cast<L3Type>(r->type);
    if (type == L3Type::Trade) {
        ev.type  = EventType::Trade;
        ev.trade = TradePrint{l3_side(r->side), r->price, r->qty};
        return true;
    }
    // Order event: apply to the market-only book, then report its level's
    // new aggregate size. Delete/Modify carry the order's price, but use the
    // book's record of it in case the feed's copy differs.
    Side side = l3_side(r->side);
    Price px  = r->price;
    if (const L3Book::Order* o = book_->find(r->id)) {
        side = o->side;
        px   = o->price;
    }
    switch (type) {
        case L3Type::Add:    book_->add(r->id, side, px, r->qty); break;
        case L3Type::Delete: book_->cancel(r->id); break;
        case L3Type::Modify: book_->modify(r->id, r->qty); break;
        default: break;
    }
    ev.type = EventType::Delta;
    (side == Side::Bid ? ev.bids : ev.asks).push_back({px, book_->level_qty(side, px)});
    return true;
}

// ── factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<EventSource> open_event_source(const std::string& path,
                                               const std::string& trades_path,
                                               double tick_size, double lot_size) {
    if (ends_with(path, ".data") || ends_with(path, ".data.zip")) {
        BybitHistoricalSource::Config c;
        c.book_path   = path;
        c.trades_path = trades_path;
        c.tick_size   = tick_size;
        c.lot_size    = lot_size;
        // Symbol from file name: {DATE}_{SYM}_ob200.data(.zip)
        auto slash = path.find_last_of('/');
        std::string base = path.substr(slash == std::string::npos ? 0 : slash + 1);
        auto u1 = base.find('_');
        auto u2 = base.find('_', u1 + 1);
        if (u1 != std::string::npos && u2 != std::string::npos) c.symbol = base.substr(u1 + 1, u2 - u1 - 1);
        return std::make_unique<BybitHistoricalSource>(c);
    }
    if (ends_with(path, ".l3bin")) return std::make_unique<L3EventSource>(path);
    return std::make_unique<NormalizedCsvSource>(path);
}
