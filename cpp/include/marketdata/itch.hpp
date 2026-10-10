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
// Handlers implement every on_* method used below; unhandled message types
// are skipped by length.
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

// Parses complete messages from [p, p + n); returns bytes consumed (a
// trailing partial message is left for the caller's next buffer).
template <typename H>
size_t parse_itch(const uint8_t* p, size_t n, H& h) {
    size_t off = 0;
    while (off + 2 <= n) {
        const uint16_t len = be16(p + off);
        if (off + 2 + len > n) break;
        const uint8_t* m = p + off + 2;
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
            case 'C':   // Order Executed With Price: ... printable(31) price(32)
                h.on_execute(loc, ts, be64(m + kRef), be32(m + 19));
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
            default:
                break;
        }
        h.on_message(m[0]);
        off += 2 + len;
    }
    return off;
}

}  // namespace itch
