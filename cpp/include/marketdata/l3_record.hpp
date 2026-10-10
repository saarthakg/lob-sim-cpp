#pragma once
#include "../core/side.hpp"
#include "../core/types.hpp"
#include <cstddef>
#include <cstdint>

// One order-level (L3) event. Book mutations are Add / Delete / Modify
// (absolute new size; a decrease keeps priority, an increase re-queues).
// Trades are *prints*: they identify the maker order and size for fill
// attribution but do not mutate the book, because venues such as Bitstamp
// report the resulting size change separately as an order event — applying
// both would double-count every partial fill.
enum class L3Type : uint8_t { Add = 1, Delete = 2, Modify = 3, Trade = 4 };

// Natural alignment, no packing: the fields are already laid out without
// padding (48 bytes). A packed record has alignment 1, so embedded in
// another struct its 8-byte fields can sit at odd addresses — binding a
// uint64_t& to one is undefined behaviour (caught by UBSan).
struct L3Record {
    int64_t  ts;      // exchange time, microseconds
    uint64_t id;      // order id (Trade: the maker order's id)
    int64_t  price;   // ticks
    int64_t  qty;     // lots (Modify: new absolute size)
    uint64_t aux;     // Trade: aggressor order id (0 if unknown)
    uint8_t  type;    // L3Type
    uint8_t  side;    // 0 bid / 1 ask (Trade: aggressor side)
    uint8_t  pad[6];
};
static_assert(sizeof(L3Record) == 48, "L3Record must stay 48 bytes (on-disk format)");
static_assert(alignof(L3Record) == 8, "L3Record must be 8-byte aligned");
static_assert(offsetof(L3Record, aux) == 32 && offsetof(L3Record, type) == 40, "L3Record layout is the file format");

inline Side l3_side(uint8_t s) { return s == 0 ? Side::Bid : Side::Ask; }
