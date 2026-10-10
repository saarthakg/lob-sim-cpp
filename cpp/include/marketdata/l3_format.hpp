#pragma once
#include "l3_record.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Compact binary order-level (L3) event file — the replay format for the L3
// engine. Fixed 48-byte little-endian records after a small header, so a
// replay is a sequential fread into a reusable buffer with no parsing.
// Record semantics: see l3_record.hpp.
#pragma pack(push, 1)
struct L3FileHeader {
    char     magic[8];    // "LOBSIML3"
    uint32_t version;     // 1
    char     symbol[20];
    double   tick_size;
    double   lot_size;
    uint64_t count;       // number of records
};
#pragma pack(pop)

class L3FileWriter {
public:
    L3FileWriter(const std::string& path, const std::string& symbol, double tick, double lot);
    ~L3FileWriter();
    void write(const L3Record& r);
    uint64_t count() const { return count_; }

private:
    FILE* f_ = nullptr;
    L3FileHeader hdr_{};
    uint64_t count_ = 0;
    std::vector<L3Record> buf_;
    void flush();
};

class L3FileReader {
public:
    explicit L3FileReader(const std::string& path);
    ~L3FileReader();
    const L3FileHeader& header() const { return hdr_; }
    // Returns a pointer to the next record, or nullptr at end of file.
    const L3Record* next();

private:
    FILE* f_ = nullptr;
    L3FileHeader hdr_{};
    std::vector<L3Record> buf_;
    size_t pos_ = 0, len_ = 0;
};

// Converts a Bitstamp order-level capture (python/data/capture_bitstamp_l3.py:
// a REST snapshot line, then live_orders/live_trades messages) to an L3 file.
// Tick and lot come from the decimals the venue prints in the snapshot.
// Returns the number of records written.
uint64_t convert_bitstamp_l3(const std::string& jsonl_path, const std::string& out_path);
