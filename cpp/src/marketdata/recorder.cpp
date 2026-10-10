#include "../../include/marketdata/recorder.hpp"
#include "../../include/marketdata/event_source.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

Recorder::Recorder(const std::string& output_path)
    : file_(output_path, std::ios::out | std::ios::trunc) {
    if (!file_.is_open()) throw std::runtime_error("Recorder: cannot open file: " + output_path);
}

Recorder::~Recorder() { flush(); }

void Recorder::write_instrument(const std::string& symbol, double tick_size, double lot_size) {
    if (header_written_) return;
    file_.precision(12);
    file_ << "#instrument," << symbol << "," << tick_size << "," << lot_size << "\n";
    file_ << "ts,type,side,price,qty\n";
    header_written_ = true;
}

void Recorder::record_book(Timestamp ts, bool snapshot, const std::vector<StrLevel>& bids,
                           const std::vector<StrLevel>& asks) {
    const char t = snapshot ? 'S' : 'D';
    for (const auto& [p, q] : bids) file_ << ts << ',' << t << ",B," << p << ',' << q << '\n';
    for (const auto& [p, q] : asks) file_ << ts << ',' << t << ",A," << p << ',' << q << '\n';
}

void Recorder::record_trade(Timestamp ts, Side aggressor, const std::string& px, const std::string& qty) {
    file_ << ts << ",T," << (aggressor == Side::Bid ? 'B' : 'A') << ',' << px << ',' << qty << '\n';
}

void Recorder::flush() { file_.flush(); }

std::pair<double, double> detect_tick_lot(const std::vector<Recorder::StrLevel>& bids,
                                          const std::vector<Recorder::StrLevel>& asks) {
    std::vector<int64_t> px;
    int64_t gq = 0;
    for (const auto* side : {&bids, &asks}) {
        for (const auto& [p, q] : *side) {
            px.push_back(decimal::parse_scaled(p));
            gq = std::gcd(gq, decimal::parse_scaled(q));
        }
    }
    std::sort(px.begin(), px.end());
    int64_t gp = 0;
    for (size_t i = 1; i < px.size(); ++i) gp = std::gcd(gp, px[i] - px[i - 1]);
    double tick = gp > 0 ? static_cast<double>(gp) / decimal::SCALE : 0.01;
    double lot  = gq > 0 ? static_cast<double>(gq) / decimal::SCALE : 0.001;
    return {tick, lot};
}
