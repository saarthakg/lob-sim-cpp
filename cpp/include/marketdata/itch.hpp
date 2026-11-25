#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Zero-copy decoder for Nasdaq TotalView-ITCH 5.0 (the format of Nasdaq's
// public sample files, e.g. 12302019.NASDAQ_ITCH50.gz once decompressed).
//
// Stream framing: each message is a 2-byte big-endian length followed by that
// many bytes, the first being the message type. All integers are big-endian;
// prices are 4-implied-decimal (1/10,000 USD); timestamps are 6-byte
// nanoseconds since midnight.
//
// parse_itch() walks a buffer and calls the handler's typed callbacks
// directly (no virtual dispatch, no allocation, no copies of the payload).
// Handlers derive from itch::HandlerBase, which supplies a no-op for every
// callback, and redefine the ones they need; unhandled message types are
// skipped by length. A message shorter than its type's specified length is
// reported through on_malformed() and never decoded, so a corrupt or
// truncated file cannot make the decoder read outside the buffer.
namespace itch {

inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
inline uint64_t be64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}
inline uint64_t be48(const uint8_t* p) {
    return (static_cast<uint64_t>(be16(p)) << 32) | be32(p + 2);
}

// Field offsets common to order messages: type(0) locate(1) tracking(3) timestamp(5)
constexpr size_t kLocate = 1, kTs = 5, kRef = 11;

// Specified length of each message type we decode (ITCH 5.0 spec, section 4).
// 0 = type not decoded, any length accepted.
constexpr uint16_t min_length(uint8_t type) {
    switch (type) {
        case 'S': return 12;
        case 'R': return 39;
        case 'H': return 25;
        case 'A': return 36;
        case 'F': return 40;
        case 'E': return 31;
        case 'C': return 36;
        case 'X': return 23;
        case 'D': return 19;
        case 'U': return 35;
        case 'P': return 44;
        case 'Q': return 40;
        default:  return 0;
    }
}

// Statically overridden (shadowed, not virtual) by handlers.
struct HandlerBase {
    void on_add(uint16_t, uint64_t, uint64_t, uint8_t, uint32_t, uint32_t) {}
    void on_execute(uint16_t, uint64_t, uint64_t, uint32_t) {}
    // Execution at a price other than the order's display price. Not
    // printable = also reported in a bulk print (crosses): don't count it as
    // a continuous-market trade.
    void on_execute_with_price(uint16_t, uint64_t, uint64_t, uint32_t, bool /*printable*/, uint32_t /*price*/) {}
    void on_cancel(uint16_t, uint64_t, uint64_t, uint32_t) {}
    void on_delete(uint16_t, uint64_t, uint64_t) {}
    void on_replace(uint16_t, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t) {}
    void on_directory(uint16_t, const char*) {}
    void on_hidden_trade(uint16_t, uint64_t, uint32_t, uint32_t) {}
    void on_cross(uint16_t, uint64_t, uint64_t /*shares*/, uint32_t /*price*/) {}
    void on_trading_action(uint16_t, uint64_t, uint8_t /*state*/) {}
    void on_system_event(uint64_t, uint8_t /*code*/) {}
    void on_message(uint8_t) {}
    void on_malformed(uint8_t /*type*/) {}
};

// Parses complete messages from [p, p + n); returns bytes consumed (a
// trailing partial message is left for the caller's next buffer).
template <typename H>
size_t parse_itch(const uint8_t* p, size_t n, H& h) {
    size_t off = 0;
    while (off + 2 <= n) {
        const uint16_t len = be16(p + off);
        if (off + 2 + len > n) break;
        const uint8_t* m = p + off + 2;
        // Every ITCH message has the 11-byte common header.
        if (len < 11 || len < min_length(m[0])) {
            h.on_malformed(len ? m[0] : 0);
            off += 2 + static_cast<size_t>(len);
            continue;
        }
        const uint16_t loc = be16(m + kLocate);
        const uint64_t ts  = be48(m + kTs);
        switch (m[0]) {
            case 'A':   // Add Order (no MPID): ref(11) side(19) shares(20) stock(24) price(32)
            case 'F':   // Add Order with MPID attribution: same + mpid(36)
                h.on_add(loc, ts, be64(m + kRef), m[19], be32(m + 20), be32(m + 32));
                break;
            case 'E':   // Order Executed: ref(11) shares(19) match(23)
                h.on_execute(loc, ts, be64(m + kRef), be32(m + 19));
                break;
            case 'C':   // Order Executed With Price: ref(11) shares(19) match(23) printable(31) price(32)
                h.on_execute_with_price(loc, ts, be64(m + kRef), be32(m + 19), m[31] == 'Y', be32(m + 32));
                break;
            case 'X':   // Order Cancel (partial): ref(11) cancelled shares(19)
                h.on_cancel(loc, ts, be64(m + kRef), be32(m + 19));
                break;
            case 'D':   // Order Delete: ref(11)
                h.on_delete(loc, ts, be64(m + kRef));
                break;
            case 'U':   // Order Replace: old ref(11) new ref(19) shares(27) price(31)
                h.on_replace(loc, ts, be64(m + kRef), be64(m + 19), be32(m + 27), be32(m + 31));
                break;
            case 'R':   // Stock Directory: stock(11..19)
                h.on_directory(loc, reinterpret_cast<const char*>(m + 11));
                break;
            case 'P':   // Trade (non-cross, hidden liquidity): side(19) shares(20) stock(24) price(32)
                h.on_hidden_trade(loc, ts, be32(m + 20), be32(m + 32));
                break;
            case 'Q':   // Cross Trade: shares(11, 8 bytes) stock(19) price(27)
                h.on_cross(loc, ts, be64(m + 11), be32(m + 27));
                break;
            case 'H':   // Stock Trading Action: stock(11) state(19)
                h.on_trading_action(loc, ts, m[19]);
                break;
            case 'S':   // System Event: code(11)
                h.on_system_event(ts, m[11]);
                break;
            default:
                break;
        }
        h.on_message(m[0]);
        off += 2 + static_cast<size_t>(len);
    }
    return off;
}

}  // namespace itch
