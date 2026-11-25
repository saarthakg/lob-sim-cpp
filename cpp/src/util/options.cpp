#include "../../include/util/options.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace {

// Every option the program understands. Anything else is an error: a typo
// must not silently run an experiment with the default value.
const std::set<std::string> kFlags = {
    "--no-cross-fill", "--no-fee-in-quotes", "--calib-log", "--passive", "--quiet", "--no-book-metrics",
    "--quote-on-book", "--summary-only", "--consume-prints", "--measure-latency", "--help", "-h"};
const std::set<std::string> kOptions = {
    // modes
    "--replay", "--stats", "--live", "--live-capture", "--load-snapshot", "--convert-bitstamp-l3", "--replay-itch",
    "--extract-itch", "--replay-l3",
    // data
    "--trades", "--symbol", "--symbols", "--date", "--snapshot-time", "--end-time", "--exchange", "--duration-minutes",
    "--minutes", "--depth", "--tick-size", "--lot-size", "--output", "--sample-ms",
    // exchange and latency
    "--maker-fee-bps", "--taker-fee-bps", "--maker-fee-per-unit", "--taker-fee-per-unit", "--session-start",
    "--session-end", "--queue", "--queue-power", "--exchange-model", "--impact", "--impact-ttl-ms", "--latency-us",
    "--feed-latency-us", "--entry-latency-us", "--response-latency-us", "--jitter",
    // strategy
    "--strategy", "--order-size", "--max-position", "--gamma", "--levels", "--half-spread-mult", "--skew-mult",
    "--alpha-micro", "--alpha-obi", "--alpha-scale", "--obi-band-bps", "--min-half-spread-ticks", "--warmup-s",
    "--max-depth-ticks", "--depletion-pull", "--pull-threshold", "--join-threshold", "--inventory-skew", "--max-spread-ticks",
    "--offset-ticks", "--threshold", "--hold-ms", "--algo", "--horizon-s", "--gap-s", "--cross-imbalance", "--side", "--slices", "--interval-s", "--probes"};

size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t up = row[j];
            row[j] = std::min({up + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1])});
            diag = up;
        }
    }
    return row[b.size()];
}

[[noreturn]] void unknown(const std::string& k) {
    std::string best;
    size_t best_d = 4;   // suggest only near misses
    for (const auto* set : {&kFlags, &kOptions})
        for (const auto& o : *set)
            if (size_t d = edit_distance(k, o); d < best_d) { best_d = d; best = o; }
    throw std::runtime_error("unknown option " + k + (best.empty() ? "" : " (did you mean " + best + "?)"));
}

}  // namespace

bool Options::is_flag(const std::string& key) { return kFlags.count(key) != 0; }

void Options::set(const std::string& key, const std::string& value) {
    if (kFlags.count(key)) {
        if (!value.empty()) throw std::runtime_error(key + " takes no value");
        kv_[key] = "true";
    } else if (kOptions.count(key)) {
        if (value.empty()) throw std::runtime_error(key + " needs a value");
        kv_[key] = value;
    } else {
        unknown(key);
    }
}

Options Options::parse(int argc, const char* const* argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (kOptions.count(k)) {
            if (i + 1 >= argc) throw std::runtime_error(k + " needs a value");
            o.set(k, argv[++i]);
        } else {
            o.set(k, "");
        }
    }
    return o;
}

double Options::num(const std::string& k, double d) const {
    auto it = kv_.find(k);
    if (it == kv_.end()) return d;
    size_t used = 0;
    double v = 0;
    try { v = std::stod(it->second, &used); } catch (const std::exception&) { used = 0; }
    if (used == 0 || used != it->second.size()) throw std::runtime_error(k + ": not a number: " + it->second);
    return v;
}

std::vector<std::string> split_list(const std::string& list) {
    std::vector<std::string> out;
    for (size_t st = 0;;) {
        const size_t c = list.find(',', st);
        std::string item = list.substr(st, c == std::string::npos ? std::string::npos : c - st);
        if (!item.empty()) out.push_back(std::move(item));
        if (c == std::string::npos) return out;
        st = c + 1;
    }
}

// HH:MM:SS → nanoseconds since midnight.
uint64_t time_of_day_ns(const std::string& hms) {
    unsigned h = 0, m = 0, s = 0;
    if (std::sscanf(hms.c_str(), "%2u:%2u:%2u", &h, &m, &s) != 3 || h > 24 || m > 59 || s > 59)
        throw std::runtime_error("bad time (want HH:MM:SS): " + hms);
    return (h * 3600ULL + m * 60ULL + s) * 1'000'000'000ULL;
}
