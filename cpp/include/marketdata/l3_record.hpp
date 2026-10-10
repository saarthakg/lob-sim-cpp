#pragma once
#include "../core/side.hpp"
#include "../core/types.hpp"
#include <cstdint>

// One order-level (L3) event. Book mutations are Add / Delete / Modify
// (absolute new size; a decrease keeps priority, an increase re-queues).
// Trades are *prints*: they identify the maker order and size for fill
// attribution but do not mutate the book, because venues such as Bitstamp
// report the resulting size change separately as an order event — applying
// both would double-count every partial fill.
enum class L3Type : uint8_t { Add = 1, Delete = 2, Modify = 3, Trade = 4 };

#pragma pack(push, 1)
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
#pragma pack(pop)
static_assert(sizeof(L3Record) == 48, "L3Record must stay 48 bytes");

inline Side l3_side(uint8_t s) { return s == 0 ? Side::Bid : Side::Ask; }
