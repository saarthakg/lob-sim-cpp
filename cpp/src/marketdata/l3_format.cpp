#include "../../include/marketdata/l3_format.hpp"
#include "../../include/marketdata/event_source.hpp"
#include "../../include/util/json.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

// ── writer ───────────────────────────────────────────────────────────────────

L3FileWriter::L3FileWriter(const std::string& path, const std::string& symbol, double tick, double lot) {
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) throw std::runtime_error("L3FileWriter: cannot open " + path);
    std::memcpy(hdr_.magic, "LOBSIML3", 8);
    hdr_.version = 1;
    std::strncpy(hdr_.symbol, symbol.c_str(), sizeof(hdr_.symbol) - 1);
    hdr_.tick_size = tick;
    hdr_.lot_size  = lot;
    std::fwrite(&hdr_, sizeof(hdr_), 1, f_);   // count patched on close
    buf_.reserve(1 << 14);
}

void L3FileWriter::write(const L3Record& r) {
    buf_.push_back(r);
    ++count_;
    if (buf_.size() == buf_.capacity()) flush();
}

void L3FileWriter::flush() {
    if (!buf_.empty()) std::fwrite(buf_.data(), sizeof(L3Record), buf_.size(), f_);
    buf_.clear();
}

L3FileWriter::~L3FileWriter() {
    if (!f_) return;
    flush();
    hdr_.count = count_;
    std::fseek(f_, 0, SEEK_SET);
    std::fwrite(&hdr_, sizeof(hdr_), 1, f_);
    std::fclose(f_);
}

// ── reader ───────────────────────────────────────────────────────────────────

L3FileReader::L3FileReader(const std::string& path) {
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) throw std::runtime_error("L3FileReader: cannot open " + path);
    if (std::fread(&hdr_, sizeof(hdr_), 1, f_) != 1 || std::memcmp(hdr_.magic, "LOBSIML3", 8) != 0)
        throw std::runtime_error("L3FileReader: not an L3 file: " + path);
    buf_.resize(1 << 15);
}

L3FileReader::~L3FileReader() {
    if (f_) std::fclose(f_);
}

const L3Record* L3FileReader::next() {
    if (pos_ == len_) {
        len_ = std::fread(buf_.data(), sizeof(L3Record), buf_.size(), f_);
        pos_ = 0;
        if (len_ == 0) return nullptr;
    }
    return &buf_[pos_++];
}

// ── Bitstamp converter ───────────────────────────────────────────────────────

namespace {

// Printed precision, trailing zeros included: Bitstamp prints fixed decimals
// ("0.50000000"), so this is the venue's precision and can never truncate a
// later value. Stripping zeros would infer a too-coarse lot from round sizes.
int decimals(const std::string& s) {
    auto dot = s.find('.');
    return dot == std::string::npos ? 0 : static_cast<int>(s.size() - dot - 1);
}

int64_t pow10(int n) {
    int64_t v = 1;
    while (n-- > 0) v *= 10;
    return v;
}

}  // namespace

uint64_t convert_bitstamp_l3(const std::string& jsonl_path, const std::string& out_path) {
    std::ifstream in(jsonl_path);
    if (!in) throw std::runtime_error("cannot open " + jsonl_path);
    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("empty capture");
    json first = json::parse(line);
    if (!first.contains("snapshot")) throw std::runtime_error("capture must start with a snapshot line");
    const json& snap = first["snapshot"];

    // Tick/lot from the decimals the venue prints (BTC/USD: 0.01 / 1e-8).
    int pd = 0, qd = 0;
    for (const char* key : {"bids", "asks"})
        for (const auto& row : snap[key]) {
            pd = std::max(pd, decimals(row[0].get<std::string>()));
            qd = std::max(qd, decimals(row[1].get<std::string>()));
        }
    const int64_t tick_scaled = decimal::SCALE / pow10(pd);
    const int64_t lot_scaled  = decimal::SCALE / pow10(qd);
    auto px = [&](const std::string& s) { return decimal::parse_scaled(s) / tick_scaled; };
    auto qy = [&](const std::string& s) { return decimal::parse_scaled(s) / lot_scaled; };

    std::string symbol = "BITSTAMP";
    {
        auto slash = jsonl_path.find_last_of('/');
        std::string base = jsonl_path.substr(slash == std::string::npos ? 0 : slash + 1);
        auto u1 = base.find('_'), u2 = base.find('_', u1 + 1);
        if (u1 != std::string::npos && u2 != std::string::npos) symbol = base.substr(u1 + 1, u2 - u1 - 1);
    }
    L3FileWriter w(out_path, symbol, 1.0 / static_cast<double>(pow10(pd)), 1.0 / static_cast<double>(pow10(qd)));

    // Snapshot: resting orders in id order (Bitstamp ids increase with time,
    // so this reproduces queue priority within each level).
    const int64_t snap_us = std::stoll(snap["microtimestamp"].get<std::string>());
    for (int s = 0; s < 2; ++s) {
        std::vector<std::tuple<uint64_t, int64_t, int64_t>> rows;
        for (const auto& row : snap[s == 0 ? "bids" : "asks"])
            rows.emplace_back(std::stoull(row[2].get<std::string>()), px(row[0].get<std::string>()),
                              qy(row[1].get<std::string>()));
        std::sort(rows.begin(), rows.end());
        for (auto& [id, p, q] : rows) {
            L3Record r{};
            r.ts = snap_us; r.id = id; r.price = p; r.qty = q;
            r.type = static_cast<uint8_t>(L3Type::Add);
            r.side = static_cast<uint8_t>(s);
            w.write(r);
        }
    }

    while (std::getline(in, line)) {
        json j;
        try { j = json::parse(line); } catch (...) { continue; }
        if (!j.contains("msg")) continue;
        const json& m = j["msg"];
        const std::string ev = m.value("event", "");
        const json& d = m["data"];
        if (!d.contains("microtimestamp")) continue;
        const int64_t ts = std::stoll(d["microtimestamp"].get<std::string>());
        if (ts <= snap_us) continue;
        L3Record r{};
        r.ts = ts;
        if (ev == "order_created" || ev == "order_changed" || ev == "order_deleted") {
            r.id    = d["id"].get<uint64_t>();
            r.side  = static_cast<uint8_t>(d["order_type"].get<int>());
            r.price = px(d["price_str"].get<std::string>());
            r.qty   = qy(d["amount_str"].get<std::string>());
            r.type  = static_cast<uint8_t>(ev == "order_created"   ? L3Type::Add
                                           : ev == "order_changed" ? L3Type::Modify
                                                                   : L3Type::Delete);
        } else if (ev == "trade") {
            const bool buy = d["type"].get<int>() == 0;   // aggressor side
            r.type  = static_cast<uint8_t>(L3Type::Trade);
            r.side  = buy ? 0 : 1;
            r.id    = buy ? d["sell_order_id"].get<uint64_t>() : d["buy_order_id"].get<uint64_t>();
            r.aux   = buy ? d["buy_order_id"].get<uint64_t>() : d["sell_order_id"].get<uint64_t>();
            r.price = px(d["price_str"].get<std::string>());
            r.qty   = qy(d["amount_str"].get<std::string>());
        } else {
            continue;
        }
        w.write(r);
    }
    return w.count();
}
