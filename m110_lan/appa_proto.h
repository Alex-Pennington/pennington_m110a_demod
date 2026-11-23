// MIL-STD-188-110D Appendix A (LAN interface): packet formats, CRC and the UDP data FEC.
//
// Header-only so the server (m110_lan/main.cpp) and the unit test
// (test/lan/test_appa_proto.cpp) share one implementation. Every constant cites the
// paragraph, table or figure of Appendix A it comes from; docs/110d-appa-lan.md has the
// field-by-field table.
#ifndef M110_LAN_APPA_PROTO_H
#define M110_LAN_APPA_PROTO_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace appa {

// ---- A.5.3 CRC: x16+x15+x12+x11+x8+x6+x3+1, LSB of each byte first, initial 0 ----------------
// The spec's C listing, with the `return nCrc;` its listing omits.
inline uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++)
        for (unsigned j = 0x01; j & 0xFF; j <<= 1) {
            unsigned bit = (crc & 1u) ^ ((p[i] & j) ? 1u : 0u);
            crc >>= 1;
            if (bit) crc ^= 0x9299;
        }
    return crc;
}
inline uint16_t crc16(const std::vector<uint8_t>& v) { return crc16(v.data(), v.size()); }

// ---- A.4.4 network byte order ----------------------------------------------------------------
inline void put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
inline void put24(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v >> 16)); put16(b, v & 0xFFFF); }
inline void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }
inline uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
inline uint32_t get24(const uint8_t* p) { return uint32_t(p[0]) << 16 | get16(p + 1); }
inline uint32_t get32(const uint8_t* p) { return get16(p) << 16 | get16(p + 2); }

// =============================================================================================
// TCP socket interface (A.5.1)
// =============================================================================================
namespace tcp {

constexpr uint8_t kPreamble[3] = {0x49, 0x50, 0x55};  // A.5.1.1.1
constexpr size_t kHeaderLen = 8;                      // A.5.1.1, Figure A-2
constexpr size_t kMaxPayload = 4086;                  // A.5.1.1, Figure A-1
constexpr size_t kMaxData = 4072;                     // A.5.1.1.5 (4086 - 14)
constexpr uint8_t kVersion = 12;                      // A.5.1.1.2

// Table A-I
enum Type : uint8_t { DATA = 0x00, CONNECT = 0x01, CONNECT_ACK = 0x02, ERROR = 0xFF };
// Table A-II
enum Cmd : uint8_t {
    DATA_TRANSFER = 0x00, TRANSMIT_ARM = 0x01, TRANSMIT_START = 0x02, REQUEST_TX_STATUS = 0x03,
    TX_DATA_NAK = 0x04, TX_STATUS = 0x05, ABORT_RECEPTION = 0x06, CARRIER_DETECT = 0x08,
    TRANSMIT_SETUP = 0x09, INITIAL_SETUP = 0x0A, CONNECTION_PROBE = 0x0B
};
// Table A-III
enum Order : uint8_t { FIRST_ONLY = 1, FIRST_AND_LAST = 2, CONTINUATION = 3, LAST = 4 };
// Table A-IV
enum Nack : uint8_t { NOT_ARMED = 0, UNDERRUN = 1, MISSING_FIRST = 2, MULTIPLE_FIRST = 3 };
// Table A-V
enum TxState : uint8_t {
    FLUSHED = 1, ARMED_NOT_READY = 2, ARMED_READY = 3, STARTED = 4, DRAINING_OK = 5, DRAINING_FORCED = 6
};
// Table A-VI
enum Carrier : uint8_t { NO_CARRIER = 0, CARRIER_DETECTED = 1 };
// Tables A-VII, A-VIII, A-IX
enum SyncFlag : uint8_t { ASYNCHRONOUS = 0, SYNCHRONOUS = 1 };
enum DataBits : uint8_t { BITS5 = 0, BITS6 = 1, BITS7 = 2, BITS8 = 3 };
enum StopBits : uint8_t { STOP1 = 0, STOP2 = 1 };
enum Parity : uint8_t { NO_PARITY = 0, EVEN = 1, ODD = 2 };
enum AsyncMode : uint8_t { STANDARD = 0, DATA_ONLY = 1 };

using PacketId = std::array<uint8_t, 12>;  // A.5.1.1.5.2

// One packet: Figure A-1. A payload of zero bytes carries no payload CRC (A.5.1.1.3).
inline std::vector<uint8_t> packet(uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b(kPreamble, kPreamble + 3);
    b.push_back(type);
    put16(b, uint32_t(payload.size()));
    put16(b, crc16(b.data(), 6));  // header CRC over the 6 preceding bytes (A.5.1.1.1)
    if (!payload.empty()) {
        b.insert(b.end(), payload.begin(), payload.end());
        put16(b, crc16(payload));  // payload CRC over the payload only (A.5.1.1.3)
    }
    return b;
}

inline std::vector<uint8_t> connect(uint8_t type = CONNECT) { return packet(type, {kVersion}); }
inline std::vector<uint8_t> keepalive() { return packet(DATA, {}); }  // A.5.1.2.2
inline std::vector<uint8_t> command(uint8_t cmd) { return packet(DATA, {cmd}); }

inline std::vector<uint8_t> data_transfer(uint8_t order, const PacketId& id, const uint8_t* d, size_t n) {
    std::vector<uint8_t> p{DATA_TRANSFER, order};  // Figure A-3
    p.insert(p.end(), id.begin(), id.end());
    p.insert(p.end(), d, d + n);
    return packet(DATA, p);
}
inline std::vector<uint8_t> nack(uint8_t cause, const PacketId& id) {  // Figure A-4
    std::vector<uint8_t> p{TX_DATA_NAK, cause};
    p.insert(p.end(), id.begin(), id.end());
    return packet(DATA, p);
}
inline std::vector<uint8_t> tx_status(uint8_t state, uint32_t space, uint32_t fill, uint32_t crit_ms,
                                      uint32_t crit_bytes) {  // Figure A-5
    std::vector<uint8_t> p{TX_STATUS, state};
    put32(p, space); put32(p, fill); put32(p, crit_ms); put32(p, crit_bytes);
    return packet(DATA, p);
}
inline std::vector<uint8_t> carrier_detect(uint8_t state, uint32_t rate, uint32_t bf) {  // Figure A-6
    std::vector<uint8_t> p{CARRIER_DETECT, state};
    put32(p, rate); put32(p, bf);
    return packet(DATA, p);
}
inline std::vector<uint8_t> transmit_setup(uint32_t rate, uint32_t bf) {  // Figure A-7
    std::vector<uint8_t> p{TRANSMIT_SETUP};
    put32(p, rate); put32(p, bf);
    return packet(DATA, p);
}
struct InitialSetup {  // Figure A-8
    uint32_t rtt_ms = 0, min_latency_ms = 0, max_latency_ms = 0;
    uint8_t sync = ASYNCHRONOUS, data_bits = BITS8, stop_bits = STOP1, parity = NO_PARITY, mode = DATA_ONLY;
};
inline std::vector<uint8_t> initial_setup(const InitialSetup& s) {
    std::vector<uint8_t> p{INITIAL_SETUP};
    put32(p, s.rtt_ms); put32(p, s.min_latency_ms); put32(p, s.max_latency_ms);
    p.push_back(s.sync);
    // Table A-VII: SYNCHRONOUS sets the four following bytes to 0
    if (s.sync == SYNCHRONOUS) { p.insert(p.end(), 4, 0); }
    else { p.push_back(s.data_bits); p.push_back(s.stop_bits); p.push_back(s.parity); p.push_back(s.mode); }
    return packet(DATA, p);
}

// Stream parser. Feed bytes; take() returns each complete packet with good CRCs. A packet
// that fails either CRC is dropped silently (A.5.1.2.3): the parser drops one byte and
// searches for the next preamble.
struct Packet {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
};
class Parser {
public:
    void feed(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    size_t crc_drops() const { return crc_drops_; }
    std::optional<Packet> take() {
        for (;;) {
            // resync: discard bytes before the next preamble, keeping a tail that may be its start
            size_t i = 0;
            while (i + 3 <= buf_.size() && std::memcmp(&buf_[i], kPreamble, 3) != 0) i++;
            if (i + 3 > buf_.size()) i = buf_.size() > 2 ? buf_.size() - 2 : 0;
            buf_.erase(buf_.begin(), buf_.begin() + i);
            if (buf_.size() < kHeaderLen) return std::nullopt;
            if (crc16(buf_.data(), 6) != get16(&buf_[6])) { drop1(); continue; }
            size_t len = get16(&buf_[4]);
            if (len > kMaxPayload) { drop1(); continue; }
            size_t total = kHeaderLen + (len ? len + 2 : 0);
            if (buf_.size() < total) return std::nullopt;
            Packet pk;
            pk.type = buf_[3];
            if (len) {
                if (crc16(&buf_[kHeaderLen], len) != get16(&buf_[kHeaderLen + len])) { drop1(); continue; }
                pk.payload.assign(buf_.begin() + kHeaderLen, buf_.begin() + kHeaderLen + len);
            }
            buf_.erase(buf_.begin(), buf_.begin() + total);
            return pk;
        }
    }

private:
    void drop1() { crc_drops_++; buf_.erase(buf_.begin()); }
    std::vector<uint8_t> buf_;
    size_t crc_drops_ = 0;
};

}  // namespace tcp

// =============================================================================================
// UDP socket interface (A.5.2)
// =============================================================================================
namespace udp {

constexpr uint8_t kVersion = 1;          // A.5.2.3
constexpr size_t kMaxPacket = 1226;      // A.5.2.2
constexpr size_t kMaxData = 1200;        // A.5.2.4.2
// Table A-X
enum Type : uint8_t {
    PING_REQUEST = 1, PING_REPLY = 2, STATUS_REQUEST = 3, STATUS_REPLY = 4, ENCODED_DATA = 5,
    ENHANCED_ENCODED_DATA = 6, MODEM_COMMAND_REQUEST = 7, MODEM_COMMAND_ACK = 8
};
constexpr uint8_t kCmdAbortReception = 1;  // Table A-XVI
constexpr uint8_t kFecTypeRsErasures = 1;  // A.5.2.4.2 FEC Type
constexpr uint8_t kInterleaverStd = 1;     // A.5.2.4.2 Interleaver Type
// Figure A-17 / Table A-XV
constexpr uint8_t kCvFirstSet = 0x01, kCvLastSet = 0x02, kCvHoldoff = 0x04;
// Figure A-14 / Table A-XI
constexpr uint8_t kErrTxOverrun = 0x01, kErrTxUnderrun = 0x02, kErrRxOverrun = 0x04, kErrRxUnderrun = 0x08,
                  kErrNotInControl = 0x10;
// Table A-XIII and A-XIV (status vector fields)
enum RxSv : uint8_t { RX_FLUSHED = 0, RX_STARTED = 1 };
enum TxSv : uint8_t { TX_FLUSHED = 0, TX_QUEUEING = 1, TX_STARTED = 2, TX_DRAINING = 3 };
// Figure A-15: reserved b7, receiver state b6-5, carrier detect b4, transmitter state b3-1, in control b0
inline uint8_t status_vector(uint8_t rx, bool cd, uint8_t tx, bool in_control) {
    return uint8_t((rx & 3) << 5 | (cd ? 1 : 0) << 4 | (tx & 7) << 1 | (in_control ? 1 : 0));
}

// The packet length payload per type (Table A-X); 0 for the data types, whose length follows
// from the Data Length field.
inline int fixed_payload(uint8_t type) {
    switch (type) {
        case PING_REQUEST: case PING_REPLY: case STATUS_REQUEST: return 0;
        case STATUS_REPLY: return 32;
        case MODEM_COMMAND_REQUEST: case MODEM_COMMAND_ACK: return 4;
        default: return -1;
    }
}

// Figure A-11 / A-12: header (version 4 bits, type 4 bits, session 24 bits), payload, CRC over
// header and payload. The version is read as the high nibble of the first byte (it is drawn
// first, and A.4.4 sends most-significant first).
inline std::vector<uint8_t> packet(uint8_t type, uint32_t session, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b;
    b.push_back(uint8_t(kVersion << 4 | (type & 0x0F)));
    put24(b, session & 0xFFFFFF);
    b.insert(b.end(), payload.begin(), payload.end());
    put16(b, crc16(b));
    return b;
}

struct Packet {
    uint8_t type = 0;
    uint32_t session = 0;
    std::vector<uint8_t> payload;
};
// A.5.2.6.3: bad CRC, wrong version (A.5.2.3), unknown type or wrong length are ignored.
inline std::optional<Packet> parse(const uint8_t* p, size_t n) {
    if (n < 6 || n > kMaxPacket) return std::nullopt;
    if (crc16(p, n - 2) != get16(p + n - 2)) return std::nullopt;
    if ((p[0] >> 4) != kVersion) return std::nullopt;
    Packet pk;
    pk.type = p[0] & 0x0F;
    pk.session = get24(p + 1);
    pk.payload.assign(p + 4, p + n - 2);
    int fixed = fixed_payload(pk.type);
    if (fixed >= 0) {
        if (pk.payload.size() != size_t(fixed)) return std::nullopt;
    } else if (pk.type == ENCODED_DATA || pk.type == ENHANCED_ENCODED_DATA) {
        size_t hdr = pk.type == ENCODED_DATA ? 16 : 20;
        if (pk.payload.size() < hdr || pk.payload.size() - hdr != get16(&pk.payload[8])) return std::nullopt;
    } else {
        return std::nullopt;
    }
    return pk;
}

struct StatusReply {  // Figure A-13
    uint16_t seq = 0, transfer_id = 0;
    uint8_t error = 0, status = 0;
    uint16_t dropped = 0, jitter = 0;
    uint32_t tx_fill = 0, tx_space = 0, rx_rate = 0, rx_bf = 0, tx_rate = 0, tx_bf = 0;
};
inline std::vector<uint8_t> status_payload(const StatusReply& s) {
    std::vector<uint8_t> p;
    put16(p, s.seq); put16(p, s.transfer_id); put16(p, 0);  // reserved
    p.push_back(s.error); p.push_back(s.status);
    put16(p, s.dropped); put16(p, s.jitter);
    put32(p, s.tx_fill); put32(p, s.tx_space);
    put24(p, s.rx_rate); put24(p, s.rx_bf); put24(p, s.tx_rate); put24(p, s.tx_bf);
    return p;
}

// Figures A-16 / A-18
struct DataHeader {
    uint16_t transfer_id = 0, seq = 0;
    uint8_t control = 0, fec_type = kFecTypeRsErasures, il_type = kInterleaverStd, n = 0, k = 0;
    uint32_t rate = 0, bf = 0, timestamp = 0;  // rate and bf only in ENHANCED_ENCODED_DATA
};
inline std::vector<uint8_t> data_payload(bool enhanced, const DataHeader& h, const uint8_t* d, size_t len) {
    std::vector<uint8_t> p;
    put16(p, h.transfer_id); put16(p, h.seq);
    p.push_back(h.control);
    p.push_back(uint8_t((h.fec_type & 0xF) << 4 | (h.il_type & 0xF)));
    p.push_back(h.n); p.push_back(h.k);
    put16(p, uint32_t(len));
    if (enhanced) { put24(p, h.rate); put24(p, h.bf); }
    else put16(p, 0);  // reserved
    put32(p, h.timestamp);
    p.insert(p.end(), d, d + len);
    return p;
}
inline DataHeader parse_data_header(const Packet& pk) {
    const uint8_t* p = pk.payload.data();
    DataHeader h;
    h.transfer_id = uint16_t(get16(p)); h.seq = uint16_t(get16(p + 2));
    h.control = p[4]; h.fec_type = p[5] >> 4; h.il_type = p[5] & 0xF; h.n = p[6]; h.k = p[7];
    if (pk.type == ENHANCED_ENCODED_DATA) { h.rate = get24(p + 10); h.bf = get24(p + 13); h.timestamp = get32(p + 16); }
    else h.timestamp = get32(p + 12);
    return h;
}
inline const uint8_t* data_bytes(const Packet& pk) { return pk.payload.data() + (pk.type == ENCODED_DATA ? 16 : 20); }
inline size_t data_len(const Packet& pk) { return get16(&pk.payload[8]); }

}  // namespace udp

// =============================================================================================
// A.5.2.5 Reed-Solomon with erasures, 4-bit symbols, GF(16) mod x^4+x+1
// =============================================================================================
namespace rs {

struct Gf16 {
    uint8_t exp[30], log[16];
    Gf16() {
        unsigned x = 1;
        for (int i = 0; i < 15; i++) {
            exp[i] = exp[i + 15] = uint8_t(x);
            log[x] = uint8_t(i);
            x <<= 1;
            if (x & 0x10) x ^= 0x13;  // x^4 + x + 1
        }
        log[0] = 0;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t inv(uint8_t a) const { return exp[(15 - log[a]) % 15]; }
    uint8_t pow_a(int e) const { return exp[((e % 15) + 15) % 15]; }
};
inline const Gf16& gf() { static Gf16 g; return g; }

// Generator with roots a^1 .. a^(n-k). For n-k = 4 this is the A.5.2.5 polynomial
// g(x) = x^4 + a^13 x^3 + a^6 x^2 + a^3 x + a^10 (test_appa_proto checks the coefficients).
// For n-k = 3 (the 3200 bps and faster rows of Table A-XVII) Appendix A gives no generator;
// roots a^1 .. a^3 is this implementation's reading (docs/110d-appa-lan.md, ambiguities).
inline std::vector<uint8_t> generator(int nk) {
    const Gf16& g = gf();
    std::vector<uint8_t> p{1};  // p[i] = coefficient of x^i
    for (int r = 1; r <= nk; r++) {
        std::vector<uint8_t> q(p.size() + 1, 0);
        uint8_t root = g.pow_a(r);
        for (size_t i = 0; i < p.size(); i++) { q[i + 1] ^= p[i]; q[i] ^= g.mul(p[i], root); }
        p = q;
    }
    return p;
}

// Systematic encode. A codeword is listed position 0 .. n-1 as the coefficient of x^0 .. x^(n-1),
// so the n-k parity symbols come first and the k data symbols after them, as in Table A-XVIII
// ({XXXABCD}).
inline std::vector<uint8_t> encode(const std::vector<uint8_t>& data, int n) {
    const Gf16& g = gf();
    int k = int(data.size()), nk = n - k;
    std::vector<uint8_t> gen = generator(nk);
    std::vector<uint8_t> c(n, 0);
    for (int i = 0; i < k; i++) c[nk + i] = data[i];
    // remainder of data(x) * x^nk mod gen(x)
    std::vector<uint8_t> rem(c);
    for (int i = n - 1; i >= nk; i--) {
        uint8_t f = rem[i];
        if (!f) continue;
        for (int j = 0; j <= nk; j++) rem[i - nk + j] ^= g.mul(f, gen[j]);
    }
    for (int i = 0; i < nk; i++) c[i] = rem[i];
    return c;
}

// Erasure-only decode of an (n, k) codeword: solve c(a^r) = 0, r = 1 .. e, for the e erased
// positions. Returns false when more than n-k positions are erased (A.5.2.5: "N-K packet
// losses may be recovered per interleaver").
inline bool decode(std::vector<uint8_t>& c, int k, const std::vector<int>& erased) {
    const Gf16& g = gf();
    int n = int(c.size()), nk = n - k, e = int(erased.size());
    if (e == 0) return true;
    if (e > nk) return false;
    for (int pos : erased) c[pos] = 0;
    // rows r = 1..e: sum_j x_j a^(r*pos_j) = S_r, where S_r = c(a^r) with erasures zeroed
    std::vector<std::vector<uint8_t>> m(e, std::vector<uint8_t>(e + 1, 0));
    for (int r = 0; r < e; r++) {
        uint8_t s = 0;
        for (int i = 0; i < n; i++) s ^= g.mul(c[i], g.pow_a((r + 1) * i));
        for (int j = 0; j < e; j++) m[r][j] = g.pow_a((r + 1) * erased[j]);
        m[r][e] = s;
    }
    for (int col = 0; col < e; col++) {
        int piv = col;
        while (piv < e && !m[piv][col]) piv++;
        if (piv == e) return false;
        std::swap(m[piv], m[col]);
        uint8_t iv = g.inv(m[col][col]);
        for (int j = col; j <= e; j++) m[col][j] = g.mul(m[col][j], iv);
        for (int r = 0; r < e; r++) {
            if (r == col || !m[r][col]) continue;
            uint8_t f = m[r][col];
            for (int j = col; j <= e; j++) m[r][j] ^= g.mul(f, m[col][j]);
        }
    }
    for (int j = 0; j < e; j++) c[erased[j]] = m[j][e];
    return true;
}

// ---- A.5.2.5 interleaver ----------------------------------------------------------------------
// Figure A-22: L = min(ceil(B / (K * 8)), 1200) * N bytes per interleaver set of N packets,
// L / N bytes per packet.
inline size_t bytes_per_packet(uint32_t bf_bits, int k) {
    size_t per = (bf_bits + size_t(k) * 8 - 1) / (size_t(k) * 8);
    if (per == 0) per = 1;
    return per < 1200 ? per : 1200;
}
// User bytes carried by one interleaver set: each packet byte holds two codeword rows, so
// a set has 2 * (L/N) codewords of K nibbles = (L/N) * K bytes.
inline size_t user_bytes_per_set(size_t per_packet, int k) { return per_packet * size_t(k); }

// Table A-XVIII / A-XIX: user byte b of the set (0-based) gives its high nibble to codeword
// 2*(b / K) and its low nibble to codeword 2*(b / K) + 1, data position b % K. Packet j of the
// set carries column j: its byte i is codeword 2i (high nibble) and codeword 2i+1 (low).
inline std::vector<std::vector<uint8_t>> encode_set(const uint8_t* user, size_t per_packet, int n, int k) {
    std::vector<std::vector<uint8_t>> pk(n, std::vector<uint8_t>(per_packet, 0));
    for (size_t row = 0; row < per_packet; row++) {
        std::vector<uint8_t> hi(k), lo(k);
        for (int j = 0; j < k; j++) {
            uint8_t b = user[row * k + j];
            hi[j] = b >> 4; lo[j] = b & 0x0F;
        }
        std::vector<uint8_t> ch = encode(hi, n), cl = encode(lo, n);
        for (int j = 0; j < n; j++) pk[j][row] = uint8_t(ch[j] << 4 | cl[j]);
    }
    return pk;
}
// present[j] false marks packet j as an erasure. Returns false when the set cannot be decoded.
inline bool decode_set(std::vector<std::vector<uint8_t>>& pk, const std::vector<bool>& present, size_t per_packet,
                       int n, int k, std::vector<uint8_t>& user) {
    std::vector<int> erased;
    for (int j = 0; j < n; j++) if (!present[j]) erased.push_back(j);
    if (int(erased.size()) > n - k) return false;
    user.assign(per_packet * k, 0);
    for (size_t row = 0; row < per_packet; row++) {
        std::vector<uint8_t> ch(n), cl(n);
        for (int j = 0; j < n; j++) {
            uint8_t b = present[j] ? pk[j][row] : 0;
            ch[j] = b >> 4; cl[j] = b & 0x0F;
        }
        if (!decode(ch, k, erased) || !decode(cl, k, erased)) return false;
        for (int j = 0; j < k; j++) user[row * k + j] = uint8_t(ch[n - k + j] << 4 | cl[n - k + j]);
    }
    return true;
}

// A.5.2.5.2 end of message
constexpr uint8_t kEom[8] = {0x4B, 0x65, 0xA5, 0xB2, 0x26, 0xD2, 0xD3, 0x69};
// Append EOM and zero padding to a whole number of sets.
inline std::vector<uint8_t> frame_stream(const std::vector<uint8_t>& user, size_t set_bytes) {
    std::vector<uint8_t> s(user);
    s.insert(s.end(), kEom, kEom + 8);
    size_t r = s.size() % set_bytes;
    if (r) s.insert(s.end(), set_bytes - r, 0);
    return s;
}
// Remove the EOM and the padding after it: the last EOM followed only by zero bytes.
inline bool strip_eom(std::vector<uint8_t>& s) {
    size_t end = s.size();
    while (end > 0 && s[end - 1] == 0) end--;  // the padding
    // the EOM ends in 0x69, so it ends exactly where the padding starts
    if (end < 8 || std::memcmp(&s[end - 8], kEom, 8) != 0) return false;
    s.resize(end - 8);
    return true;
}

}  // namespace rs
}  // namespace appa

#endif
