#pragma once
#include "../core/side.hpp"
#include "../core/types.hpp"
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// Writes the v2 normalized capture format read by NormalizedCsvSource
// (see event_source.hpp): snapshot rows, delta rows and trade rows, with
// prices/quantities kept as the venue's exact decimal strings.
class Recorder {
public:
    using StrLevel = std::pair<std::string, std::string>;  // (price, qty)

    explicit Recorder(const std::string& output_path);
    ~Recorder();

    // Must be called once before any rows (once tick/lot are known).
    void write_instrument(const std::string& symbol, double tick_size, double lot_size);
    void record_book(Timestamp ts, bool snapshot, const std::vector<StrLevel>& bids,
                     const std::vector<StrLevel>& asks);
    void record_trade(Timestamp ts, Side aggressor, const std::string& px, const std::string& qty);
    void flush();
    bool header_written() const { return header_written_; }

private:
    std::ofstream file_;
    bool header_written_ = false;
};

// Venue tick/lot from the decimal grids seen in a snapshot (GCD of price
// differences and of quantities) — exchanges' minimum increments are often
// coarser than the number of printed decimals suggests.
std::pair<double, double> detect_tick_lot(const std::vector<Recorder::StrLevel>& bids,
                                          const std::vector<Recorder::StrLevel>& asks);
