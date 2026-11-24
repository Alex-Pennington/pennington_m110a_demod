// m110_lan - MIL-STD-188-110D Appendix A LAN interface in front of the 110A modem.
//
// A DTE connects over TCP (A.5.1, the TCP data socket) or over the UDP data socket (A.5.2),
// which needs a TCP control connection first (A.5.2.1). One DTE at a time (A.4.1).
// Transmit: data from the DTE -> ModemTX (110A) -> audio to a WAV file or raw s16le on stdout.
// Receive: audio from a WAV/raw file or stdin -> bursts found by preamble -> 110A receiver
// (AUTO) -> data to the DTE. Audio is read on its own thread, so a decode never stalls it.
// There is no radio. docs/110d-appa-lan.md lists what is implemented and what is not.

#include "api/modem.h"
#include "dsp/fir_filter.h"
#include "m110_lan/appa_proto.h"
#include "m110a/brain_preamble.h"
#include "sync/preamble_acq.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace m110a::api;
using m110a::PreambleCorrelator;
using m110a::generate_srrc_taps;
using Clock = std::chrono::steady_clock;
namespace tcp = appa::tcp;
namespace udp = appa::udp;

namespace {

// ---------------------------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------------------------
enum class Duplex { FULL, HALF_TX_MASTER, HALF_RX_MASTER };

struct Config {
    std::string bind = "0.0.0.0";
    int tcp_port = 3000;  // A.4.2: configurable, 3000 suggested
    int rci_port = 3001;  // A.5.2.1 control connection for the UDP data socket
    int udp_port = 3002;
    std::string mode_name = "600S";
    Mode mode = Mode::M600_SHORT;
    std::string tx_out;   // file (may hold %d) or "-" for stdout
    std::string tx_format;  // wav | raw
    double tx_lead = 0.5, tx_tail = 1.0;
    std::string rx_in;    // file or "-" for stdin
    double rx_gap = 0.5;  // seconds below the burst's own end level that end a burst
    double rx_max = 120.0;  // longest burst, seconds
    double rx_threshold = 0.45;  // preamble correlation metric that starts a burst (0..1)
    bool rx_low_quality = false;  // deliver bytes the decoder scores below its quality gate
    bool rx_realtime = true;   // pace delivery at the over-the-air rate
    Duplex duplex = Duplex::FULL;
    uint32_t min_latency_ms = 100, max_latency_ms = 5000;  // Figure A-8, modem configuration
    size_t tx_queue = 1 << 20;
    double udp_set_timeout = 2.0;
    bool verbose = false;
};
Config cfg;

void logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "m110_lan: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

// 110A modes with a data path in this repo. 75 bps has none (test/test_table_xi_modes.cpp: "the
// 75 bps data path is not implemented"); 4800L is not a 110A mode (ModemTX maps it to 4800S).
const std::map<std::string, Mode> kModes = {
    {"150S", Mode::M150_SHORT}, {"150L", Mode::M150_LONG}, {"300S", Mode::M300_SHORT},
    {"300L", Mode::M300_LONG}, {"600S", Mode::M600_SHORT}, {"600L", Mode::M600_LONG},
    {"1200S", Mode::M1200_SHORT}, {"1200L", Mode::M1200_LONG}, {"2400S", Mode::M2400_SHORT},
    {"2400L", Mode::M2400_LONG}, {"4800S", Mode::M4800_SHORT}};

// Blocking factor (A.5.1.1.9 / A.5.1.1.8): user bits per 110A interleaver block, the data rate
// times the interleaver span, 0.6 s short and 4.8 s long (MIL-STD-188-110 Table XII).
uint32_t blocking_factor(Mode m) {
    double span = mode_is_long(m) ? 4.8 : 0.6;
    return uint32_t(std::lround(mode_bitrate(m) * span));
}

// Table A-XVII by data rate.
void fec_nk(uint32_t rate, int& n, int& k) {
    if (rate <= 2400) { n = 6; k = 2; }
    else if (rate <= 28800) { n = 7; k = 4; }
    else { n = 8; k = 5; }
}

// A.5.2.4.2 timestamp, as the appendix's code computes it
uint32_t udp_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return uint32_t((tv.tv_sec % 3600) * 1000000u + tv.tv_usec);
}

bool send_all(int fd, const std::vector<uint8_t>& b) {
    size_t off = 0;
    while (off < b.size()) {
        ssize_t n = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// audio sink (transmit) and source (receive)
// ---------------------------------------------------------------------------------------------
int g_audio_out_fd = -1;  // the original stdout; fd 1 is pointed at stderr for library prints
int g_tx_count = 0;

std::vector<int16_t> to_pcm(const std::vector<float>& sig) {
    double e = 0;
    for (float v : sig) e += double(v) * v;
    double rms = std::sqrt(e / std::max<size_t>(1, sig.size()));
    float g = rms > 0 ? float((7000.0 / 32768.0) / rms) : 0.f;  // MS-DMT reference level
    std::vector<int16_t> out(size_t(cfg.tx_lead * 48000), 0);
    for (float v : sig) {
        long s = std::lround(v * g * 32767.0f);
        out.push_back(int16_t(std::max(-32768L, std::min(32767L, s))));
    }
    out.resize(out.size() + size_t(cfg.tx_tail * 48000), 0);
    return out;
}

bool write_audio(const std::vector<int16_t>& pcm) {
    g_tx_count++;
    std::vector<uint8_t> b;
    bool wav = cfg.tx_format == "wav";
    if (wav) {
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); };
        auto u16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
        uint32_t data = uint32_t(pcm.size() * 2);
        b.insert(b.end(), {'R', 'I', 'F', 'F'}); u32(36 + data); b.insert(b.end(), {'W', 'A', 'V', 'E'});
        b.insert(b.end(), {'f', 'm', 't', ' '}); u32(16); u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16);
        b.insert(b.end(), {'d', 'a', 't', 'a'}); u32(data);
    }
    for (int16_t s : pcm) { b.push_back(uint8_t(s)); b.push_back(uint8_t(uint16_t(s) >> 8)); }
    if (cfg.tx_out == "-") {
        size_t off = 0;
        while (off < b.size()) {
            ssize_t n = ::write(g_audio_out_fd, b.data() + off, b.size() - off);
            if (n <= 0) return false;
            off += size_t(n);
        }
        return true;
    }
    std::string path = cfg.tx_out;
    size_t p = path.find("%d");
    if (p != std::string::npos) path.replace(p, 2, std::to_string(g_tx_count));
    std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
    ok = (fclose(f) == 0) && ok;
    // written whole, then renamed, so a reader never sees a partial file
    return ok && rename(tmp.c_str(), path.c_str()) == 0;
}

// ---------------------------------------------------------------------------------------------
// the modem
// ---------------------------------------------------------------------------------------------
enum class TxSt { FLUSHED, ARMED_NOT_READY, ARMED_READY, STARTED, DRAINING_OK, DRAINING_FORCED };
enum class RxSt { NO_CARRIER, CARRIER_DETECTED, RECEIVING };

uint8_t tcp_tx_state(TxSt s) {  // Table A-V
    switch (s) {
        case TxSt::FLUSHED: return tcp::FLUSHED;
        case TxSt::ARMED_NOT_READY: return tcp::ARMED_NOT_READY;
        case TxSt::ARMED_READY: return tcp::ARMED_READY;
        case TxSt::STARTED: return tcp::STARTED;
        case TxSt::DRAINING_OK: return tcp::DRAINING_OK;
        default: return tcp::DRAINING_FORCED;
    }
}
uint8_t udp_tx_state(TxSt s) {  // Table A-XIV
    switch (s) {
        case TxSt::FLUSHED: return udp::TX_FLUSHED;
        case TxSt::ARMED_NOT_READY: case TxSt::ARMED_READY: return udp::TX_QUEUEING;
        case TxSt::STARTED: return udp::TX_STARTED;
        default: return udp::TX_DRAINING;
    }
}

// One burst from the air, from preamble sync until its data is delivered or it is dropped.
struct Reception {
    uint64_t id = 0;  // the scanner's burst number; 0 for none
    Mode mode = Mode::AUTO;
    uint32_t rate = 0, bf = 0;
    Clock::time_point sync_at;  // when the preamble was synchronised
    enum St { DECODING, READY, FAILED } st = DECODING;
    bool data_started = false;
    std::vector<uint8_t> data;
};

struct InSet {  // one interleaver set of an inbound UDP transfer
    std::vector<std::vector<uint8_t>> pk;
    std::vector<bool> have;
    size_t per = 0;
    int n = 0, k = 0;
    bool last = false;
    Clock::time_point first_seen;
};

class Modem {
public:
    std::mutex mu;
    std::condition_variable cv;
    std::atomic<bool> stop{false};

    // ---- DTE ownership (A.4.1: one DTE at a time) ----
    int tcp_fd = -1;
    bool tcp_established = false;
    Clock::time_point tcp_last_sent, tcp_last_rx_data, tcp_last_status;
    uint64_t pkt_id_counter = 0;

    int rci_fd = -1;
    bool udp_have_dte = false;
    sockaddr_in udp_dte{};
    bool in_control = false;
    int udp_fd = -1;
    uint32_t session = 0;
    bool session_adopted = false;
    uint16_t status_seq = 0;
    Clock::time_point udp_last_status, udp_last_ping_sent, udp_last_ping_reply;
    bool udp_ping_timeout = false;
    uint8_t udp_error = 0;
    std::vector<uint8_t> last_status_fields;
    // inbound UDP transfer (DTE -> modem)
    bool in_active = false;
    uint16_t in_tid = 0;
    bool have_done_tid = false;
    uint16_t done_tid = 0;
    std::map<uint32_t, InSet> in_sets;
    uint32_t in_next_set = 0;
    std::vector<uint8_t> in_stream;
    bool in_holdoff = false;
    uint16_t in_dropped = 0;
    std::deque<double> jitter_win;
    bool cmd_seq_seen = false;
    uint16_t last_cmd_seq = 0;
    // outbound UDP transfer (modem -> DTE)
    uint16_t out_tid = 0;

    // ---- transmitter ----
    TxSt tx = TxSt::FLUSHED;
    std::vector<uint8_t> txq;
    bool have_first = false, got_last = false;
    uint16_t tx_tid = 0;  // UDP current transfer id reported in status

    // ---- receiver ----
    RxSt rx = RxSt::NO_CARRIER;
    std::deque<Reception> rx_pending;
    Reception cur;
    size_t deliver_off = 0;
    int deliver_idx = 0;
    Clock::time_point next_deliver;
    // UDP delivery: whole sets precomputed
    std::vector<uint8_t> out_stream;
    size_t out_set_bytes = 0, out_per = 0, out_sets_sent = 0;
    int out_n = 0, out_k = 0;
    uint16_t out_seq = 0;

    bool dte() const { return tcp_established || udp_have_dte; }
    bool dte_busy() const { return tcp_fd >= 0 || rci_fd >= 0; }

    // ======================= TCP =======================
    void tcp_send(const std::vector<uint8_t>& b) {
        if (tcp_fd < 0) return;
        if (send_all(tcp_fd, b)) tcp_last_sent = Clock::now();
        else ::shutdown(tcp_fd, SHUT_RDWR);
    }
    void tcp_status() {
        tcp_send(tcp::tx_status(tcp_tx_state(tx), uint32_t(cfg.tx_queue - txq.size()), uint32_t(txq.size()), 0, 0));
        tcp_last_status = Clock::now();
    }
    tcp::PacketId next_pid() {
        tcp::PacketId id{};
        uint64_t c = ++pkt_id_counter;
        for (int i = 0; i < 8; i++) id[11 - i] = uint8_t(c >> (8 * i));
        return id;
    }
    void carrier_packet() {
        if (!tcp_established) return;
        if (rx == RxSt::NO_CARRIER) tcp_send(tcp::carrier_detect(tcp::NO_CARRIER, 0, 0));
        else tcp_send(tcp::carrier_detect(tcp::CARRIER_DETECTED, cur.rate, cur.bf));
    }

    // ======================= transmitter (both interfaces) =======================
    void status_changed() {
        if (tcp_established) tcp_status();
        if (udp_have_dte) udp_status(false);
    }
    void tx_reset() {
        tx = TxSt::FLUSHED;
        txq.clear();
        have_first = got_last = false;
    }
    void enter_started() {
        tx = TxSt::STARTED;
        // A.5.1.2.7.1: half-duplex transmitter master aborts a reception in progress
        if (cfg.duplex == Duplex::HALF_TX_MASTER && rx != RxSt::NO_CARRIER) abort_reception();
        if (cfg.duplex != Duplex::FULL) rx_pending.clear();
    }
    void enter_draining() {
        tx = TxSt::DRAINING_OK;
        cv.notify_all();  // wakes the encoder
    }
    // Rx master: an armed transmitter waits for the receiver (A.5.1.2.7.2)
    void rx_went_idle() {
        if (cfg.duplex == Duplex::HALF_RX_MASTER && tx == TxSt::ARMED_NOT_READY) {
            tx = TxSt::ARMED_READY;
            status_changed();
        }
        if (cfg.duplex == Duplex::HALF_RX_MASTER) udp_try_start();
    }

    // A.5.1.2.5 step 1-3
    void tcp_arm() {
        if (tx != TxSt::FLUSHED) { tcp_status(); return; }
        bool busy_rx = cfg.duplex == Duplex::HALF_RX_MASTER && rx != RxSt::NO_CARRIER;
        tx = busy_rx ? TxSt::ARMED_NOT_READY : TxSt::ARMED_READY;
        have_first = got_last = false;
        txq.clear();
        tcp_status();
    }
    // A.5.1.2.5 steps 5-6, A.5.1.2.7.2
    void tcp_start() {
        if (tx == TxSt::ARMED_READY) {
            enter_started();
            tcp_status();
            if (got_last) { enter_draining(); tcp_status(); }
        } else {
            tcp_status();
        }
    }
    // A.5.1.1.5, A.5.1.1.6, A.5.1.2.5 steps 4, 10, 12. Returns false when the session closed
    // while waiting for queue space.
    bool tcp_data(std::unique_lock<std::mutex>& lk, const std::vector<uint8_t>& p) {
        uint8_t order = p[1];
        tcp::PacketId id;
        std::memcpy(id.data(), &p[2], 12);
        size_t n = p.size() - 14;
        bool armed = tx == TxSt::ARMED_READY || tx == TxSt::ARMED_NOT_READY || tx == TxSt::STARTED;
        if (!armed) { tcp_send(tcp::nack(tcp::NOT_ARMED, id)); return true; }
        bool first = order == tcp::FIRST_ONLY || order == tcp::FIRST_AND_LAST;
        bool last = order == tcp::FIRST_AND_LAST || order == tcp::LAST;
        if (first && have_first) { tcp_send(tcp::nack(tcp::MULTIPLE_FIRST, id)); return true; }
        if (!first && !have_first) { tcp_send(tcp::nack(tcp::MISSING_FIRST, id)); return true; }
        if (got_last) { tcp_send(tcp::nack(tcp::MISSING_FIRST, id)); return true; }
        // step 10: never insert while the free space is below the maximum packet data size;
        // the reader stops reading, so the DTE's socket blocks
        int fd = tcp_fd;
        cv.wait(lk, [&] { return stop || tcp_fd != fd || cfg.tx_queue - txq.size() >= tcp::kMaxData; });
        if (stop || tcp_fd != fd) return false;
        if (!(tx == TxSt::ARMED_READY || tx == TxSt::ARMED_NOT_READY || tx == TxSt::STARTED)) {
            tcp_send(tcp::nack(tcp::NOT_ARMED, id));
            return true;
        }
        if (first) have_first = true;
        txq.insert(txq.end(), p.begin() + 14, p.begin() + 14 + n);
        if (last) got_last = true;
        if (got_last && tx == TxSt::STARTED) enter_draining();
        tcp_status();  // A.5.1.1.7: unsolicited when the queues change
        return true;
    }

    // ======================= receiver =======================
    void abort_reception() {
        if (rx == RxSt::NO_CARRIER) return;
        if (tcp_established) {
            // A.5.1.2.6 step 6: a LAST packet, then NO_CARRIER
            tcp_send(tcp::data_transfer(deliver_idx == 0 ? tcp::FIRST_AND_LAST : tcp::LAST, next_pid(), nullptr, 0));
        }
        if (udp_have_dte) {
            // Table A-XVI: issue a LAST packet. The rest of the stream is replaced by one
            // interleaver set holding the EOM, flagged as the last set.
            std::vector<uint8_t> done(out_stream.begin(), out_stream.begin() + std::min(out_stream.size(), out_sets_sent * out_set_bytes));
            std::vector<uint8_t> eom = appa::rs::frame_stream({}, out_set_bytes);
            done.insert(done.end(), eom.begin(), eom.end());
            out_stream = done;
            udp_send_set();
        }
        cur = Reception{};  // its decode result, when it comes, is dropped
        rx = RxSt::NO_CARRIER;
        carrier_packet();
        if (udp_have_dte) udp_status(true);
        rx_went_idle();
    }
    // The scanner has synchronised on a preamble and read the mode from it. Returns false when
    // the burst is not taken (no DTE; or half duplex with the transmitter on, A.5.1.2.7).
    bool rx_sync(Reception r) {
        if (!dte()) {
            logf("rx: %s preamble, no DTE: burst dropped", mode_name(r.mode).c_str());
            return false;
        }
        if (cfg.duplex != Duplex::FULL &&
            (tx == TxSt::STARTED || tx == TxSt::DRAINING_OK || tx == TxSt::DRAINING_FORCED)) {
            logf("rx: transmitter active in half duplex, burst dropped");
            return false;
        }
        rx_pending.push_back(std::move(r));
        deliver_tick();  // CARRIER DETECTED now if the receiver is idle, not on the next tick
        return true;
    }
    // The decoder's verdict on burst id. A burst no longer held (aborted, or its DTE gone) is ignored.
    void rx_result(uint64_t id, bool ok, std::vector<uint8_t> data) {
        Reception* r = nullptr;
        if (rx != RxSt::NO_CARRIER && cur.id == id) r = &cur;
        for (auto& p : rx_pending) if (p.id == id) r = &p;
        if (!r) return;
        r->st = ok ? Reception::READY : Reception::FAILED;
        r->data = std::move(data);
        deliver_tick();
    }
    void start_reception(Reception r) {
        cur = std::move(r);
        rx = RxSt::CARRIER_DETECTED;  // A.5.1.2.6 step 2: the preamble is read
        deliver_off = 0;
        deliver_idx = 0;
        carrier_packet();
        if (udp_have_dte) {
            fec_nk(cur.rate, out_n, out_k);
            out_per = appa::rs::bytes_per_packet(cur.bf, out_k);
            out_set_bytes = appa::rs::user_bytes_per_set(out_per, out_k);
            out_stream.clear();  // framed once the data is decoded
            out_sets_sent = 0;
            out_seq = 0;
            out_tid = uint16_t(out_tid + 1);
            udp_status(true);
        }
        logf("rx: carrier detected, %s %u bps, blocking factor %u", mode_name(cur.mode).c_str(), cur.rate, cur.bf);
    }
    void end_reception() {
        rx = RxSt::NO_CARRIER;  // A.5.1.2.6 step 7
        cur = Reception{};
        carrier_packet();
        if (udp_have_dte) udp_status(true);
        rx_went_idle();
    }
    // Called from the timer thread, and on sync and decode results. Carrier stays up from preamble
    // sync through the decode ("processing data from the air", Table A-VI) and the delivery.
    void deliver_tick() {
        if (rx == RxSt::NO_CARRIER) {
            if (rx_pending.empty() || !dte()) return;
            Reception r = std::move(rx_pending.front());
            rx_pending.pop_front();
            start_reception(std::move(r));
        }
        if (cur.st == Reception::DECODING) return;
        if (cur.st == Reception::FAILED) { end_reception(); return; }  // no data packets
        if (!cur.data_started) {
            // delivery is paced from here as it was from the whole-burst decode: the first chunk
            // one blocking factor's air time after the data is in, then one per blocking factor
            // (--rx-fast: one per timer tick)
            cur.data_started = true;
            if (udp_have_dte) out_stream = appa::rs::frame_stream(cur.data, out_set_bytes);
            double block_s = double(cur.bf) / double(cur.rate);
            next_deliver = Clock::now() + std::chrono::microseconds(cfg.rx_realtime ? long(block_s * 1e6) : 0);
            logf("rx: %s %u bps, %zu bytes", mode_name(cur.mode).c_str(), cur.rate, cur.data.size());
            return;
        }
        if (Clock::now() < next_deliver) return;
        rx = RxSt::RECEIVING;  // the receive interleaver has filled
        double block_s = double(cur.bf) / double(cur.rate);
        bool done = false;
        if (tcp_established) {
            size_t chunk = (cur.bf + 7) / 8;
            size_t total = cur.data.size();
            size_t npk = std::max<size_t>(1, (total + chunk - 1) / chunk);
            size_t len = std::min(chunk, total - deliver_off);
            uint8_t order = npk == 1 ? tcp::FIRST_AND_LAST
                          : deliver_idx == 0 ? tcp::FIRST_ONLY
                          : size_t(deliver_idx) == npk - 1 ? tcp::LAST : tcp::CONTINUATION;
            tcp_send(tcp::data_transfer(order, next_pid(), cur.data.data() + deliver_off, len));
            deliver_off += len;
            deliver_idx++;
            done = size_t(deliver_idx) >= npk;
        } else if (udp_have_dte) {
            udp_send_set();
            deliver_idx++;
            done = out_sets_sent * out_set_bytes >= out_stream.size();
            block_s = double(out_set_bytes * 8) / double(cur.rate);
        } else {
            done = true;
        }
        next_deliver = Clock::now() + std::chrono::microseconds(cfg.rx_realtime ? long(block_s * 1e6) : 0);
        if (done) end_reception();
    }

    // ======================= UDP =======================
    void udp_send(uint8_t type, const std::vector<uint8_t>& payload) {
        if (!udp_have_dte || udp_fd < 0) return;
        auto b = udp::packet(type, session, payload);
        ::sendto(udp_fd, b.data(), b.size(), 0, reinterpret_cast<sockaddr*>(&udp_dte), sizeof udp_dte);
    }
    void udp_send_set() {
        size_t s = out_sets_sent;
        if (s * out_set_bytes >= out_stream.size()) return;
        bool first = s == 0, last = (s + 1) * out_set_bytes >= out_stream.size();
        auto pk = appa::rs::encode_set(&out_stream[s * out_set_bytes], out_per, out_n, out_k);
        for (int j = 0; j < out_n; j++) {
            udp::DataHeader h;
            h.transfer_id = out_tid;
            h.seq = out_seq++;
            h.control = uint8_t((first ? udp::kCvFirstSet : 0) | (last ? udp::kCvLastSet : 0));
            h.n = uint8_t(out_n);
            h.k = uint8_t(out_k);
            h.rate = cur.rate;
            h.bf = cur.bf;
            h.timestamp = udp_timestamp();
            // A.5.2.6.6 step 3: the first interleaver set as ENHANCED ENCODED DATA
            udp_send(first ? udp::ENHANCED_ENCODED_DATA : udp::ENCODED_DATA,
                     udp::data_payload(first, h, pk[j].data(), pk[j].size()));
        }
        out_sets_sent++;
    }
    std::vector<uint8_t> status_fields() {
        udp::StatusReply s;
        s.transfer_id = tx_tid;
        s.error = udp_error;
        bool cd = rx != RxSt::NO_CARRIER;
        s.status = udp::status_vector(rx == RxSt::NO_CARRIER ? udp::RX_FLUSHED : udp::RX_STARTED, cd, udp_tx_state(tx), in_control);
        s.dropped = in_dropped;
        double jit = 0;
        if (jitter_win.size() > 1) {
            double m = 0, v = 0;
            for (double d : jitter_win) m += d;
            m /= double(jitter_win.size());
            for (double d : jitter_win) v += (d - m) * (d - m);
            jit = std::sqrt(v / double(jitter_win.size()));
        }
        s.jitter = uint16_t(std::min(65535.0, jit));
        size_t fill = txq.size() + 1200 * in_sets.size();  // A.5.2.6.5 step 5
        s.tx_fill = uint32_t(fill);
        s.tx_space = uint32_t(cfg.tx_queue > fill ? cfg.tx_queue - fill : 0);
        if (rx != RxSt::NO_CARRIER) { s.rx_rate = cur.rate; s.rx_bf = cur.bf; }
        s.tx_rate = uint32_t(mode_bitrate(cfg.mode));
        s.tx_bf = blocking_factor(cfg.mode);
        return udp::status_payload(s);
    }
    // A.5.2.4.1: on any field change, or 500 ms since the last
    void udp_status(bool force) {
        if (!udp_have_dte) return;
        auto f = status_fields();
        std::vector<uint8_t> cmp(f.begin() + 2, f.end());  // without the sequence number
        bool due = Clock::now() - udp_last_status >= std::chrono::milliseconds(500);
        if (!force && !due && cmp == last_status_fields) return;
        uint16_t seq = status_seq++;
        f[0] = uint8_t(seq >> 8); f[1] = uint8_t(seq);
        last_status_fields = cmp;
        udp_send(udp::STATUS_REPLY, f);
        udp_last_status = Clock::now();
    }
    void udp_try_start() {
        // A.5.2.6.5 step 3: start once a set is buffered and the holdoff flag is clear
        if (tx != TxSt::ARMED_READY && tx != TxSt::ARMED_NOT_READY) return;
        if (in_stream.empty() && in_next_set == 0) return;
        if (in_holdoff) return;
        if (cfg.duplex == Duplex::HALF_RX_MASTER && rx != RxSt::NO_CARRIER) { tx = TxSt::ARMED_NOT_READY; return; }
        enter_started();
        udp_status(true);
    }
    void udp_transfer_end(bool ok) {
        if (ok && !appa::rs::strip_eom(in_stream)) logf("udp: last set has no EOM; sending the data as received");
        txq = in_stream;
        in_stream.clear();
        in_sets.clear();
        in_active = false;
        have_done_tid = true;
        done_tid = in_tid;
        if (tx == TxSt::FLUSHED || tx == TxSt::ARMED_READY || tx == TxSt::ARMED_NOT_READY) enter_started();
        tx = ok ? TxSt::DRAINING_OK : TxSt::DRAINING_FORCED;
        cv.notify_all();
        udp_status(true);
    }
    void udp_try_decode(bool timer) {
        if (!in_active) return;
        auto now = Clock::now();
        while (in_active) {
            auto it = in_sets.find(in_next_set);
            bool later = !in_sets.empty() && in_sets.rbegin()->first > in_next_set;
            if (it == in_sets.end()) {
                // a whole set missing: give up once a later set has waited out the timeout
                if (later && timer) {
                    auto l = in_sets.upper_bound(in_next_set);
                    if (now - l->second.first_seen > std::chrono::duration<double>(cfg.udp_set_timeout)) {
                        logf("udp: set %u lost", in_next_set);
                        udp_transfer_end(false);
                    }
                }
                return;
            }
            InSet& s = it->second;
            int have = 0;
            for (bool h : s.have) have += h;
            bool complete = have == s.n;
            bool expired = now - s.first_seen > std::chrono::duration<double>(cfg.udp_set_timeout);
            bool overtaken = in_sets.size() > 1 && in_sets.rbegin()->first >= in_next_set + 2;
            if (!complete && !expired && !overtaken) return;
            std::vector<uint8_t> user;
            in_dropped = uint16_t(in_dropped + (s.n - have));
            if (!appa::rs::decode_set(s.pk, s.have, s.per, s.n, s.k, user)) {
                logf("udp: set %u undecodable (%d of %d packets)", in_next_set, have, s.n);
                udp_transfer_end(false);  // A.5.2.6.3: end of message, DRAINING
                return;
            }
            in_stream.insert(in_stream.end(), user.begin(), user.end());
            bool last = s.last;
            in_sets.erase(it);
            in_next_set++;
            if (last) { udp_transfer_end(true); return; }
            udp_try_start();
        }
    }
    void udp_data(const udp::Packet& pk, const sockaddr_in&) {
        if (!in_control) {  // Table A-XI bit 4
            udp_error |= udp::kErrNotInControl;
            udp_status(false);
            return;
        }
        if (udp_ping_timeout) return;  // A.5.2.6.2
        udp::DataHeader h = udp::parse_data_header(pk);
        if (h.fec_type != udp::kFecTypeRsErasures || h.il_type != udp::kInterleaverStd) return;  // A.5.2.4.2
        if (h.n < 2 || h.n > 15 || h.k < 1 || h.k >= h.n) return;
        size_t len = udp::data_len(pk);
        if (len == 0) return;
        if (!in_active || h.transfer_id != in_tid) {
            // the previous transfer is still on the air, or this is a late packet of a finished one
            if (tx == TxSt::STARTED || tx == TxSt::DRAINING_OK || tx == TxSt::DRAINING_FORCED) return;
            if (have_done_tid && h.transfer_id == done_tid) return;
            // a new transfer
            in_active = true;
            in_tid = h.transfer_id;
            tx_tid = h.transfer_id;
            in_sets.clear();
            in_next_set = 0;
            in_stream.clear();
            in_dropped = 0;
            jitter_win.clear();
            udp_error &= uint8_t(~udp::kErrNotInControl);
            txq.clear();
            if (tx == TxSt::FLUSHED) {
                // A.5.2.6.5 step 2: QUEUEING when data starts to arrive
                tx = (cfg.duplex == Duplex::HALF_RX_MASTER && rx != RxSt::NO_CARRIER) ? TxSt::ARMED_NOT_READY : TxSt::ARMED_READY;
            }
        }
        uint32_t set = h.seq / h.n;
        int j = h.seq % h.n;
        if (set < in_next_set) return;  // late: the set is decoded (A.5.2.6.3)
        InSet& s = in_sets[set];
        if (s.pk.empty()) {
            s.n = h.n; s.k = h.k; s.per = len;
            s.pk.assign(h.n, std::vector<uint8_t>(len, 0));
            s.have.assign(h.n, false);
            s.first_seen = Clock::now();
        }
        if (s.n != h.n || s.k != h.k || s.per != len) return;  // incorrect format
        if (s.have[j]) return;  // duplicate (A.5.2.6.3)
        std::memcpy(s.pk[j].data(), udp::data_bytes(pk), len);
        s.have[j] = true;
        if (h.control & udp::kCvLastSet) s.last = true;
        in_holdoff = (h.control & udp::kCvHoldoff) != 0;
        // jitter: arrival minus timestamp, standard deviation over 100 packets (A.5.2.4.1)
        double d = double((int64_t(udp_timestamp()) - int64_t(h.timestamp) + 3600000000LL) % 3600000000LL) / 1000.0;
        jitter_win.push_back(d);
        if (jitter_win.size() > 100) jitter_win.pop_front();
        udp_try_decode(false);
        udp_try_start();
        udp_status(false);
    }
    void udp_command(const udp::Packet& pk) {
        uint16_t seq = uint16_t(appa::get16(pk.payload.data()));
        uint8_t cmd = pk.payload[2];
        if (cmd != udp::kCmdAbortReception) return;  // A.5.2.4.4: ignore unsupported commands
        bool repeat = cmd_seq_seen && seq == last_cmd_seq;
        if (!repeat) abort_reception();
        cmd_seq_seen = true;
        last_cmd_seq = seq;
        std::vector<uint8_t> ack;
        appa::put16(ack, seq);
        appa::put16(ack, 0);
        udp_send(udp::MODEM_COMMAND_ACK, ack);  // A.5.2.4.5
    }
    void udp_packet(const uint8_t* b, size_t n, const sockaddr_in& from) {
        if (!udp_have_dte) return;
        // A.5.2.6.1: data only from the DTE that holds the control connection
        if (from.sin_addr.s_addr != udp_dte.sin_addr.s_addr || from.sin_port != udp_dte.sin_port) return;
        auto pk = udp::parse(b, n);
        if (!pk) return;  // A.5.2.6.3
        if (!session_adopted || pk->session != session) {
            // A.5.2.3: the DTE's session id; a new one forces a resynchronisation
            if (session_adopted) {
                logf("udp: new session %06x", pk->session);
                in_active = false;
                in_sets.clear();
                in_stream.clear();
                if (tx == TxSt::ARMED_READY || tx == TxSt::ARMED_NOT_READY) tx_reset();
            }
            session = pk->session;
            session_adopted = true;
            udp_ping_timeout = false;
            udp_last_ping_reply = Clock::now();
        }
        switch (pk->type) {
            case udp::PING_REQUEST: udp_send(udp::PING_REPLY, {}); break;
            case udp::PING_REPLY:
                udp_last_ping_reply = Clock::now();
                udp_ping_timeout = false;
                break;
            case udp::STATUS_REQUEST: udp_status(true); break;
            case udp::ENCODED_DATA: udp_data(*pk, from); break;
            case udp::MODEM_COMMAND_REQUEST: udp_command(*pk); break;
            default: break;  // modem-to-DTE types from the DTE: incorrect, ignored
        }
    }

    // ======================= timers =======================
    void tick() {
        auto now = Clock::now();
        if (tcp_established) {
            if (now - tcp_last_rx_data >= std::chrono::seconds(30)) {  // A.5.1.2.2
                logf("tcp: 30 s without a DATA packet, closing");
                ::shutdown(tcp_fd, SHUT_RDWR);
            } else {
                if ((tx == TxSt::STARTED || tx == TxSt::DRAINING_OK || tx == TxSt::DRAINING_FORCED) &&
                    now - tcp_last_status >= std::chrono::seconds(1))
                    tcp_status();  // A.5.1.2.5 step 8: at least every 2 s
                if (now - tcp_last_sent >= std::chrono::seconds(2)) tcp_send(tcp::keepalive());
            }
        }
        if (udp_have_dte) {
            if (now - udp_last_ping_sent >= std::chrono::seconds(5)) {  // A.5.2.6.2
                udp_send(udp::PING_REQUEST, {});
                udp_last_ping_sent = now;
            }
            if (!udp_ping_timeout && now - udp_last_ping_reply >= std::chrono::seconds(30)) {
                logf("udp: 30 s without a PING REPLY, ignoring DTE data");
                udp_ping_timeout = true;
                if (in_active) { in_active = false; in_sets.clear(); in_stream.clear(); }
                if (tx == TxSt::ARMED_READY || tx == TxSt::ARMED_NOT_READY) tx_reset();
            }
            udp_try_decode(true);
            udp_status(false);
        }
        deliver_tick();
    }

    // ======================= session ends =======================
    void tcp_closed() {
        tcp_established = false;
        tcp_fd = -1;
        if (tx != TxSt::DRAINING_OK && tx != TxSt::DRAINING_FORCED) tx_reset();  // DISCONNECTED
        rx = RxSt::NO_CARRIER;
        cur = Reception{};
        rx_pending.clear();
        cv.notify_all();
    }
    void rci_closed() {
        rci_fd = -1;
        udp_have_dte = false;
        in_control = false;
        session_adopted = false;
        in_active = false;
        in_sets.clear();
        in_stream.clear();
        if (tx != TxSt::DRAINING_OK && tx != TxSt::DRAINING_FORCED) tx_reset();
        rx = RxSt::NO_CARRIER;
        cur = Reception{};
        rx_pending.clear();
        cv.notify_all();
    }
};
Modem M;

// ---------------------------------------------------------------------------------------------
// TCP data socket session (A.5.1.2)
// ---------------------------------------------------------------------------------------------
// Reads until a packet arrives or the deadline passes. Returns false on timeout or close.
bool read_packet(int fd, tcp::Parser& ps, tcp::Packet& out, Clock::time_point deadline) {
    for (;;) {
        if (auto p = ps.take()) { out = std::move(*p); return true; }
        int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        if (ms <= 0) return false;
        pollfd pf{fd, POLLIN, 0};
        int r = ::poll(&pf, 1, ms);
        if (r <= 0) return false;
        uint8_t buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        ps.feed(buf, size_t(n));
    }
}

void protocol_error(int fd, const char* why) {
    logf("tcp: protocol error (%s): ERROR and close", why);
    send_all(fd, tcp::packet(tcp::ERROR, {}));
}

void tcp_session(int fd) {
    tcp::Parser ps;
    tcp::Packet pk;
    auto t0 = Clock::now();
    bool ok = true;
    {
        std::lock_guard<std::mutex> lk(M.mu);
        M.tcp_send(tcp::connect(tcp::CONNECT));  // A.5.1.2.1
    }
    // CONNECT from the DTE and CONNECT_ACK to ours, each within 3 s
    bool got_connect = false, got_ack = false;
    while (ok && !(got_connect && got_ack)) {
        if (!read_packet(fd, ps, pk, t0 + std::chrono::seconds(3))) { logf("tcp: handshake timeout"); ok = false; break; }
        if (pk.type == tcp::CONNECT || pk.type == tcp::CONNECT_ACK) {
            if (pk.payload.size() != 1 || pk.payload[0] != tcp::kVersion) { logf("tcp: version is not 12, closing"); ok = false; break; }
            if (pk.type == tcp::CONNECT) {
                got_connect = true;
                std::lock_guard<std::mutex> lk(M.mu);
                M.tcp_send(tcp::connect(tcp::CONNECT_ACK));
            } else {
                got_ack = true;
            }
        } else if (pk.type == tcp::DATA && pk.payload.empty()) {
            // a keep-alive before the connection is up carries nothing; ignore it
        } else {
            protocol_error(fd, "packet before the initial connection");  // A.5.1.2.3
            ok = false;
        }
    }
    uint32_t rtt = 0;
    if (ok) {
        auto tp = Clock::now();
        {
            std::lock_guard<std::mutex> lk(M.mu);
            M.tcp_send(tcp::command(tcp::CONNECTION_PROBE));
        }
        bool got_probe = false;
        while (ok && !got_probe) {
            if (!read_packet(fd, ps, pk, tp + std::chrono::seconds(6))) { logf("tcp: no CONNECTION_PROBE in 6 s"); ok = false; break; }
            if (pk.type == tcp::DATA && pk.payload.size() == 1 && pk.payload[0] == tcp::CONNECTION_PROBE) got_probe = true;
            else if (pk.type == tcp::DATA && pk.payload.empty()) continue;
            else { protocol_error(fd, "expected CONNECTION_PROBE"); ok = false; }
        }
        rtt = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tp).count());
        if (ok && rtt > cfg.max_latency_ms) { logf("tcp: round trip %u ms above the maximum socket latency", rtt); ok = false; }
    }
    if (ok) {
        std::lock_guard<std::mutex> lk(M.mu);
        tcp::InitialSetup s;
        s.rtt_ms = rtt;
        s.min_latency_ms = cfg.min_latency_ms;
        s.max_latency_ms = cfg.max_latency_ms;
        M.tcp_send(tcp::initial_setup(s));                                                   // 1
        M.tcp_send(tcp::transmit_setup(uint32_t(mode_bitrate(cfg.mode)), blocking_factor(cfg.mode)));  // 2
        M.tx_reset();
        M.tcp_send(tcp::tx_status(tcp::FLUSHED, uint32_t(cfg.tx_queue), 0, 0, 0));          // 3
        M.tcp_established = true;
        M.carrier_packet();                                                                   // 4
        M.tcp_last_rx_data = Clock::now();
        M.tcp_last_status = Clock::now();
        logf("tcp: DTE connected, round trip %u ms", rtt);
    }
    // established: this thread blocks only on socket input (A.4.3)
    while (ok) {
        if (auto p = ps.take()) {
            pk = std::move(*p);
        } else {
            uint8_t buf[4096];
            ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            ps.feed(buf, size_t(n));
            continue;
        }
        std::unique_lock<std::mutex> lk(M.mu);
        if (M.tcp_fd != fd) break;
        if (pk.type == tcp::ERROR) { logf("tcp: ERROR from the DTE"); break; }
        if (pk.type != tcp::DATA) { protocol_error(fd, "CONNECT after the connection is up, or an unused type"); break; }
        M.tcp_last_rx_data = Clock::now();  // A.5.1.2.2: any DATA packet resets the timer
        if (pk.payload.empty()) continue;   // keep-alive
        const auto& p = pk.payload;
        uint8_t cmd = p[0];
        bool fail = false;
        switch (cmd) {
            case tcp::DATA_TRANSFER:
                if (p.size() < 14 || p[1] < tcp::FIRST_ONLY || p[1] > tcp::LAST) { fail = true; break; }
                if (!M.tcp_data(lk, p)) ok = false;
                break;
            case tcp::TRANSMIT_ARM: if (p.size() != 1) fail = true; else M.tcp_arm(); break;
            case tcp::TRANSMIT_START: if (p.size() != 1) fail = true; else M.tcp_start(); break;
            case tcp::REQUEST_TX_STATUS: if (p.size() != 1) fail = true; else M.tcp_status(); break;
            case tcp::ABORT_RECEPTION: if (p.size() != 1) fail = true; else M.abort_reception(); break;
            case tcp::CONNECTION_PROBE: if (p.size() != 1) fail = true; else M.tcp_send(tcp::command(tcp::CONNECTION_PROBE)); break;
            default: fail = true;  // A.5.1.1.4: unused, or sent by the modem only
        }
        if (fail) { protocol_error(fd, "bad or unimplemented payload command"); break; }
    }
    std::lock_guard<std::mutex> lk(M.mu);
    if (M.tcp_fd == fd) M.tcp_closed();
    ::close(fd);
    logf("tcp: DTE disconnected");
}

// ---------------------------------------------------------------------------------------------
// control connection for the UDP data socket (A.5.2.1). Its protocol is outside the appendix;
// this one is line-based text:  UDP <port>  |  CONTROL  |  RELEASE
// ---------------------------------------------------------------------------------------------
void rci_session(int fd, sockaddr_in peer) {
    std::string line;
    auto reply = [&](const std::string& s) { std::string t = s + "\n"; send_all(fd, std::vector<uint8_t>(t.begin(), t.end())); };
    for (;;) {
        char c;
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\r') continue;
        if (c != '\n') { if (line.size() < 256) line += c; continue; }
        std::lock_guard<std::mutex> lk(M.mu);
        if (line.rfind("UDP ", 0) == 0) {
            int port = atoi(line.c_str() + 4);
            if (port <= 0 || port > 65535) reply("ERR port");
            else {
                M.udp_dte = peer;
                M.udp_dte.sin_port = htons(uint16_t(port));
                M.udp_have_dte = true;
                M.session = std::random_device{}() & 0xFFFFFF;  // A.5.2.3, until the DTE's is seen
                M.session_adopted = false;
                M.udp_last_ping_reply = M.udp_last_ping_sent = Clock::now();
                M.udp_ping_timeout = false;
                M.tx_reset();
                reply("OK UDP " + std::to_string(cfg.udp_port));
                M.udp_status(true);
            }
        } else if (line == "CONTROL") {
            M.in_control = true;
            M.udp_error &= uint8_t(~udp::kErrNotInControl);
            reply("OK CONTROL");
            M.udp_status(true);
        } else if (line == "RELEASE") {
            M.in_control = false;
            reply("OK RELEASE");
            M.udp_status(true);
        } else {
            reply("ERR unknown");
        }
        line.clear();
    }
    std::lock_guard<std::mutex> lk(M.mu);
    if (M.rci_fd == fd) M.rci_closed();
    ::close(fd);
    logf("rci: DTE disconnected");
}

int listen_tcp(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    inet_pton(AF_INET, cfg.bind.c_str(), &a.sin_addr);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) || ::listen(fd, 4)) {
        logf("cannot listen on %s:%d: %s", cfg.bind.c_str(), port, strerror(errno));
        exit(1);
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);  // accept_loop drains until EAGAIN
    return fd;
}

// One thread accepts on both the TCP data socket and the control connection, so the DTE slot
// (A.4.1) goes to connections in the order the kernel completed their handshakes, not in the
// order two racing threads happen to run. On each wake it takes every pending connection from
// both listeners, then hands them out oldest first by TCP_INFO tcpi_last_ack_recv (ms since the
// handshake's final ACK; 1 ms resolution at HZ=1000), all read against one clock tick. Two
// handshakes completed in the same millisecond cannot be ordered from user space; that tie
// goes to the control connection.
void claim(int fd, bool rci, sockaddr_in peer);

struct Pending {
    int fd;
    bool rci;
    sockaddr_in peer;
    uint32_t age_ms;
};

void accept_loop(int tl, int rl) {
    for (;;) {
        pollfd pf[2] = {{rl, POLLIN, 0}, {tl, POLLIN, 0}};
        if (::poll(pf, 2, -1) < 0) { if (M.stop) return; continue; }
        std::vector<Pending> pend;
        for (int i = 0; i < 2; i++) {
            for (;;) {
                Pending p{-1, i == 0, {}, 0};
                socklen_t pl = sizeof p.peer;
                p.fd = ::accept4(pf[i].fd, reinterpret_cast<sockaddr*>(&p.peer), &pl, 0);
                if (p.fd < 0) break;  // the listeners are non-blocking: EAGAIN ends the drain
                pend.push_back(p);
            }
        }
        // The ages are "ms before now", so they compare only when read against the same now:
        // read them all, and read again until two passes agree (no clock tick in between).
        auto read_ages = [&pend] {
            std::vector<uint32_t> v;
            for (auto& p : pend) {
                tcp_info ti{};
                socklen_t len = sizeof ti;
                v.push_back(getsockopt(p.fd, IPPROTO_TCP, TCP_INFO, &ti, &len) == 0 ? ti.tcpi_last_ack_recv : 0);
            }
            return v;
        };
        std::vector<uint32_t> ages = read_ages();
        for (int tries = 0; tries < 10; tries++) {
            std::vector<uint32_t> again = read_ages();
            if (again == ages) break;
            ages = again;
        }
        for (size_t i = 0; i < pend.size(); i++) pend[i].age_ms = ages[i];
        // oldest first; stable, so FIFO within a port and the control connection first on a tie
        std::stable_sort(pend.begin(), pend.end(), [](const Pending& a, const Pending& b) { return a.age_ms > b.age_ms; });
        static unsigned drain = 0;
        drain++;
        for (auto& p : pend) {
            if (cfg.verbose) logf("accept: %s, handshake %u ms ago, drain %u", p.rci ? "rci" : "tcp", p.age_ms, drain);
            claim(p.fd, p.rci, p.peer);
        }
    }
}

void claim(int fd, bool rci, sockaddr_in peer) {
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        timeval tv{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        std::unique_lock<std::mutex> lk(M.mu);
        if (M.dte_busy()) {
            // A.4.1: a second DTE is rejected
            lk.unlock();
            logf("%s: second DTE rejected", rci ? "rci" : "tcp");
            ::close(fd);
            return;
        }
        if (rci) {
            M.rci_fd = fd;
            lk.unlock();
            logf("rci: DTE connected");
            std::thread(rci_session, fd, peer).detach();
        } else {
            M.tcp_fd = fd;
            lk.unlock();
            std::thread(tcp_session, fd).detach();
        }
    }
}

void udp_loop() {
    for (;;) {
        uint8_t buf[2048];
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = ::recvfrom(M.udp_fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 0) { if (M.stop) return; continue; }
        std::lock_guard<std::mutex> lk(M.mu);
        M.udp_packet(buf, size_t(n), from);
    }
}

void timer_loop() {
    while (!M.stop) {
        {
            std::lock_guard<std::mutex> lk(M.mu);
            M.tick();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// ---------------------------------------------------------------------------------------------
// transmitter: the queued message -> ModemTX -> audio sink
// ---------------------------------------------------------------------------------------------
void tx_loop() {
    std::unique_lock<std::mutex> lk(M.mu);
    for (;;) {
        M.cv.wait(lk, [] { return M.stop || M.tx == TxSt::DRAINING_OK || M.tx == TxSt::DRAINING_FORCED; });
        if (M.stop) return;
        std::vector<uint8_t> data = M.txq;
        lk.unlock();
        TxConfig tc;
        tc.mode = cfg.mode;
        tc.sample_rate = 48000;
        tc.include_leading_symbols = false;
        ModemTX txm(tc);
        auto r = txm.encode(data);
        bool ok = r.ok();
        if (ok) ok = write_audio(to_pcm(r.value()));
        if (r.ok()) logf("tx: %zu bytes, %s, %s", data.size(), cfg.mode_name.c_str(), ok ? "audio written" : "audio write FAILED");
        else logf("tx: encode failed: %s", r.error().message.c_str());
        lk.lock();
        M.tx_reset();  // A.5.1.2.5 step 13: FLUSHED
        M.status_changed();
        M.cv.notify_all();
    }
}

// ---------------------------------------------------------------------------------------------
// receiver: audio -> reader -> scanner -> decoder -> deliveries
//
// Three threads, so that nothing waits on a decode:
//   rx_reader  reads the input into a queue; it blocks only on the input, so no audio is dropped
//   rx_scan    finds each burst by its preamble (correlation, not level), reads the mode from
//              its D1/D2 blocks, raises CARRIER DETECTED, and ends the burst where its own
//              level falls away
//   rx_decode  decodes each whole burst with the 110A receiver, one at a time, and hands the
//              bytes to the modem
// ---------------------------------------------------------------------------------------------
constexpr long kFs = 48000;
constexpr long kDecim = 5;                 // scan baseband at 9600 Hz
constexpr int kSps = 4;                    // 9600 / 2400 baud
constexpr long kBlock = 96;                // 10 ms of scan baseband: the burst-end level block
constexpr long kHold = 40 * kSps;          // a correlation peak is the best within 40 symbols
constexpr long kPreroll = kFs / 4;         // decode from 0.25 s before the hit (a whole frame)

struct AudioQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::vector<float>> q;
    bool eof = false;
    void push(std::vector<float> v, bool end) {
        std::lock_guard<std::mutex> lk(mu);
        if (!v.empty()) q.push_back(std::move(v));
        if (end) eof = true;
        cv.notify_all();
    }
    bool pop(std::vector<float>& out) {  // false once the input has ended and all is taken
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&] { return !q.empty() || eof; });
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop_front();
        return true;
    }
};
AudioQueue g_audio;

struct DecodeJob {
    uint64_t id = 0;
    Mode mode = Mode::AUTO;  // as read from the preamble
    double t0 = 0, t1 = 0;   // seconds of input
    std::vector<float> s;
};
std::mutex g_dec_mu;
std::condition_variable g_dec_cv;
std::deque<DecodeJob> g_dec_q;

// Preamble D1, D2 to the mode: MIL-STD-188-110 Table XI, and the Harris application-specific
// codes this repo's receiver decodes (5,7 600H and 5,6 1200H; Table XI reserves them)
bool mode_from_d1d2(int d1, int d2, Mode& m) {
    static const std::map<std::pair<int, int>, Mode> k = {
        {{7, 5}, Mode::M75_SHORT}, {{5, 5}, Mode::M75_LONG}, {{7, 4}, Mode::M150_SHORT},
        {{5, 4}, Mode::M150_LONG}, {{6, 7}, Mode::M300_SHORT}, {{4, 7}, Mode::M300_LONG},
        {{6, 6}, Mode::M600_SHORT}, {{4, 6}, Mode::M600_LONG}, {{6, 5}, Mode::M1200_SHORT},
        {{4, 5}, Mode::M1200_LONG}, {{6, 4}, Mode::M2400_SHORT}, {{4, 4}, Mode::M2400_LONG},
        {{7, 6}, Mode::M4800_SHORT}, {{7, 7}, Mode::M2400_VOICE}, {{5, 7}, Mode::M600_HARRIS},
        {{5, 6}, Mode::M1200_HARRIS}};
    auto it = k.find({d1, d2});
    if (it == k.end()) return false;
    m = it->second;
    return true;
}

void rx_decode() {
    for (;;) {
        DecodeJob j;
        {
            std::unique_lock<std::mutex> lk(g_dec_mu);
            g_dec_cv.wait(lk, [] { return !g_dec_q.empty(); });
            j = std::move(g_dec_q.front());
            g_dec_q.pop_front();
        }
        RxConfig rc;
        rc.sample_rate = 48000;
        rc.mode = Mode::AUTO;
        rc.equalizer = Equalizer::DFE;
        auto t = Clock::now();
        DecodeResult r = decode(j.s, rc);
        double took = std::chrono::duration<double>(Clock::now() - t).count();
        double len = double(j.s.size()) / kFs;
        // below the gate: bytes decoded, quality scored under the mode's threshold
        bool below = !r.success && !r.data.empty() && r.quality_threshold_db > 0.0f &&
                     r.quality_db < r.quality_threshold_db;
        bool ok = r.success || (below && cfg.rx_low_quality);
        if (r.success) {
            logf("rx: burst %llu (%.2f-%.2f s of input) decoded in %.2f s: %s, %zu bytes, quality %.1f dB",
                 (unsigned long long)j.id, j.t0, j.t1, took, mode_name(r.mode).c_str(), r.data.size(), r.quality_db);
        } else if (ok) {
            // Appendix A has no field to mark received data as suspect (Tables A-II, A-VI, A-XI
            // to A-XV): the bytes go out as any others, and only this line says what they are
            logf("rx: burst %llu (%.2f-%.2f s of input) BELOW THE QUALITY GATE, delivered (--rx-low-quality): "
                 "%s, %zu bytes, quality %.1f dB, threshold %.1f dB, decode %.2f s",
                 (unsigned long long)j.id, j.t0, j.t1, mode_name(r.mode).c_str(), r.data.size(),
                 r.quality_db, r.quality_threshold_db, took);
        } else {
            logf("rx: burst of %.2f s not decoded (%s), decode %.2f s", len,
                 r.error ? r.error->message.c_str() : "quality below threshold", took);
        }
        if (ok && r.mode != j.mode)
            logf("rx: burst %llu decoded as %s, its preamble read %s", (unsigned long long)j.id,
                 mode_name(r.mode).c_str(), mode_name(j.mode).c_str());
        std::lock_guard<std::mutex> lk(M.mu);
        M.rx_result(j.id, ok, ok ? std::move(r.data) : std::vector<uint8_t>());
    }
}

// Finds bursts in the audio stream. All indices are absolute: audio samples at 48 kHz, scan
// baseband samples at 9600 Hz (audio index / kDecim).
class Scanner {
public:
    Scanner() : corr_(kSps) {
        taps_ = generate_srrc_taps(0.35f, 4, float(kFs) / 2400.0f);
        half_ = long(taps_.size() / 2);
        for (int n = 0; n < 80; n++) {  // 1800 Hz is 3 cycles in 80 samples
            double w = -2.0 * M_PI * 1800.0 * n / double(kFs);
            lo_[n] = std::complex<float>(float(std::cos(w)), float(std::sin(w)));
        }
        gap_blocks_ = std::max<long>(1, std::lround(cfg.rx_gap * 100));
    }
    void feed(const std::vector<float>& x) {
        audio_.insert(audio_.end(), x.begin(), x.end());
        audio_end_ += long(x.size());
        run(false);
    }
    void finish() { run(true); }

private:
    enum class St { IDLE, SYNC_WAIT, BURST };

    float at(long n) const { return n >= audio_base_ && n < audio_end_ ? audio_[size_t(n - audio_base_)] : 0.0f; }
    const std::complex<float>& bb(long j) const { return bb_[size_t(j - bb_base_)]; }
    long bb_end() const { return bb_base_ + long(bb_.size()); }
    static double sec(long audio_index) { return double(audio_index) / kFs; }

    // mean |bb|^2 over [j0, j1), as far as it is held; -1 when nothing of it is
    double bb_power(long j0, long j1) const {
        j0 = std::max(j0, bb_base_);
        j1 = std::min(j1, bb_end());
        if (j1 <= j0) return -1.0;
        double p = 0;
        for (long j = j0; j < j1; j++) p += std::norm(bb(j));
        return p / double(j1 - j0);
    }

    void extend_baseband() {
        for (;;) {
            long j = bb_end();
            long c = j * kDecim;
            if (c + half_ >= audio_end_) break;
            std::complex<float> acc(0.0f, 0.0f);
            for (long m = -half_; m <= half_; m++) {
                long n = c + m;
                if (n < audio_base_) continue;
                acc += taps_[size_t(m + half_)] * at(n) * lo_[n % 80];
            }
            bb_.push_back(acc);
        }
    }

    void run(bool eof) {
        extend_baseband();
        const long span = long(corr_.span());
        for (;;) {
            if (st_ == St::SYNC_WAIT) {
                if (bb_end() < mode_need_bb_) {
                    if (!eof) break;
                    logf("rx: preamble correlation %.2f at %.2f s of input, cut off by the end of input", hit_m_,
                         sec(hit_a_));
                    st_ = St::IDLE;
                    continue;
                }
                read_mode(eof);
                continue;
            }
            if (scan_t_ + span > bb_end()) break;
            const long t = scan_t_;
            // the 10 ms level block that ends at t
            if ((t + 1) % kBlock == 0 && st_ == St::BURST) burst_block(t + 1 - kBlock);
            if (st_ == St::BURST && t * kDecim - a0_ >= long(cfg.rx_max * kFs)) close_burst(t * kDecim, "longest burst");
            if (have_cand_ && t > cand_t_ + kHold) {
                have_cand_ = false;
                on_hit(cand_t_, cand_m_);
                if (st_ == St::SYNC_WAIT) continue;  // t is scanned once the mode is read
            }
            float m = corr_.evaluate(&bb_[size_t(t - bb_base_)]);
            if (m >= cfg.rx_threshold && (!have_cand_ || m > cand_m_)) {
                cand_t_ = t;
                cand_m_ = m;
                have_cand_ = true;
            }
            scan_t_++;
        }
        if (eof && st_ == St::BURST) close_burst(audio_end_, "end of input");
        trim();
    }

    void on_hit(long t, float metric) {
        const long a = t * kDecim;
        if (st_ == St::BURST) {
            // the burst's own later preamble frames; past them, a new transmission
            if (a < hit_a_ + (preamble_frames_ + 1) * kFs / 5) return;
            close_burst(a - kPreroll, "next preamble");
        }
        st_ = St::SYNC_WAIT;
        hit_t_ = t;
        hit_a_ = a;
        hit_m_ = metric;
        mode_frames_ = 0;
        frames_used_ = 0;
        d1_sc_.fill(0.0f);
        d2_sc_.fill(0.0f);
        // D2 ends 352 symbols into the frame (the next frame's common-segment check needs less)
        mode_need_bb_ = t + 352L * kSps + 1;
    }

    // Match of the 32 symbols of block `block` (9 = D1, 10 = D2) of the preamble frame whose
    // common segment starts at scan sample t, against each of the 8 Walsh patterns, with the
    // carrier offset f taken out. Coherent over the block, so 0 for a wrong pattern.
    std::array<float, 8> walsh_scores(long t, int block, float f) const {
        std::array<float, 8> sc{};
        std::complex<float> x[32];
        for (int i = 0; i < 32; i++) {
            const int n = block * 32 + i;
            const double w = -2.0 * M_PI * f * n / 2400.0;
            x[i] = bb(t + long(n) * kSps) * std::complex<float>(float(std::cos(w)), float(std::sin(w)));
        }
        for (int d = 0; d < 8; d++) {
            std::complex<float> acc(0.0f, 0.0f);
            for (int i = 0; i < 32; i++) {
                const int s = (m110a::brain::psymbol[d][i % 8] + m110a::brain::pscramble[i]) % 8;
                acc += x[i] * std::complex<float>(m110a::brain::psk8_i[s], -m110a::brain::psk8_q[s]);
            }
            sc[d] = std::abs(acc);
        }
        return sc;
    }
    static int best_of(const std::array<float, 8>& sc, float& ratio) {
        int b = 0;
        for (int d = 1; d < 8; d++) if (sc[d] > sc[b]) b = d;
        float second = 0;
        for (int d = 0; d < 8; d++) if (d != b) second = std::max(second, sc[d]);
        ratio = sc[b] / std::max(second, 1e-12f);
        return b;
    }

    // D1/D2 (MIL-STD-188-110 Table XI) from the preamble frames from the hit on: the Walsh
    // matches of each frame whose common segment correlates are summed until one pattern leads
    // the next by 2:1 in both, over up to 3 frames. A hit with no readable mode is not a preamble.
    void read_mode(bool eof) {
        const long fr = 480L * kSps;
        const long tf = hit_t_ + long(mode_frames_) * fr;
        if (mode_frames_ == 0) corr_.evaluate(&bb(hit_t_), &hit_f_);
        const bool frame_ok = mode_frames_ == 0 || corr_.evaluate(&bb(tf)) >= cfg.rx_threshold;
        if (frame_ok) {
            auto s1 = walsh_scores(tf, 9, hit_f_), s2 = walsh_scores(tf, 10, hit_f_);
            for (int d = 0; d < 8; d++) { d1_sc_[d] += s1[d]; d2_sc_[d] += s2[d]; }
            frames_used_++;
        }
        mode_frames_++;
        float r1, r2;
        const int d1 = best_of(d1_sc_, r1), d2 = best_of(d2_sc_, r2);
        Mode mode;
        const bool known = mode_from_d1d2(d1, d2, mode);
        if (known && r1 >= 2.0f && r2 >= 2.0f) {
            start_burst(mode);
            return;
        }
        if (mode_frames_ < 3 && !eof) {
            mode_need_bb_ = hit_t_ + long(mode_frames_) * fr + 352L * kSps + 1;
            return;
        }
        logf("rx: preamble correlation %.2f at %.2f s of input, mode not read (D1 %d %.1f:1, D2 %d %.1f:1, "
             "%d frames): ignored", hit_m_, sec(hit_a_), d1, r1, d2, r2, frames_used_);
        st_ = St::IDLE;
    }

    void start_burst(Mode mode) {
        Reception r;
        r.id = ++next_id_;
        r.mode = mode;
        r.rate = uint32_t(mode_bitrate(mode));
        r.bf = blocking_factor(mode);
        r.sync_at = Clock::now();
        r.st = Reception::DECODING;
        id_ = r.id;
        mode_ = mode;
        preamble_frames_ = mode_is_long(mode) ? 24 : 3;
        a0_ = std::max(audio_base_, hit_a_ - kPreroll);
        // The burst ends where its level falls to the geometric mean of the signal (over the
        // first preamble frame) and the noise before it (the quietest 0.1 s of the second
        // before the hit), measured in the signal's own band; never above 1/30 of the signal.
        const long fr = kFs / 5 / kDecim;  // one frame of scan baseband
        double ps = bb_power(hit_t_, hit_t_ + fr);
        double pn = -1;
        for (long j = hit_t_ - 10 * fr / 2; j + fr / 2 <= hit_t_ - fr / 2; j += fr / 2) {
            double p = bb_power(j, j + fr / 2);
            if (p >= 0 && (pn < 0 || p < pn)) pn = p;
        }
        end_level_ = std::max(pn > 0 ? std::sqrt(pn * ps) : 0.0, ps / 30.0);
        level_from_ = hit_t_ + fr;
        gap_.clear();
        gap_sum_ = 0;
        {
            std::lock_guard<std::mutex> lk(M.mu);
            deliver_ = M.rx_sync(r);
        }
        logf("rx: preamble sync at %.2f s of input: %s, correlation %.2f; burst %llu%s; level %.1f dB over %s",
             sec(hit_a_), mode_name(mode).c_str(), hit_m_, (unsigned long long)id_, deliver_ ? "" : " (dropped)",
             pn > 0 ? 10.0 * std::log10(ps / pn) : 0.0, pn > 0 ? "the noise before it" : "nothing measured before it");
        st_ = St::BURST;
    }

    void burst_block(long j0) {
        if (j0 < level_from_) return;
        double p = bb_power(j0, j0 + kBlock);
        gap_.push_back(p);
        gap_sum_ += p;
        if (long(gap_.size()) > gap_blocks_) { gap_sum_ -= gap_.front(); gap_.pop_front(); }
        if (long(gap_.size()) == gap_blocks_ && gap_sum_ / double(gap_blocks_) < end_level_)
            close_burst((j0 + kBlock) * kDecim, "level");
    }

    void close_burst(long end_a, const char* why) {
        end_a = std::min(end_a, audio_end_);
        logf("rx: burst %llu ends at %.2f s of input (%s), %.2f s long", (unsigned long long)id_, sec(end_a), why,
             sec(end_a - a0_));
        if (deliver_ && end_a > a0_) {
            DecodeJob j;
            j.id = id_;
            j.mode = mode_;
            j.t0 = sec(a0_);
            j.t1 = sec(end_a);
            j.s.resize(size_t(end_a - a0_));
            for (long n = a0_; n < end_a; n++) j.s[size_t(n - a0_)] = at(n);
            std::lock_guard<std::mutex> lk(g_dec_mu);
            g_dec_q.push_back(std::move(j));
            g_dec_cv.notify_all();
        } else if (deliver_) {
            std::lock_guard<std::mutex> lk(M.mu);
            M.rx_result(id_, false, {});
        }
        st_ = St::IDLE;
    }

    void trim() {
        const long from = st_ == St::IDLE ? scan_t_ : std::min(scan_t_, hit_t_);
        long keep_bb = from - 2 * kFs / kDecim;  // 2 s: the noise before a hit
        if (keep_bb - bb_base_ > 4 * kFs / kDecim) {
            bb_.erase(bb_.begin(), bb_.begin() + (keep_bb - bb_base_));
            bb_base_ = keep_bb;
        }
        long keep_a = std::min(bb_end() * kDecim - half_, scan_t_ * kDecim - kPreroll - kFs);
        if (st_ != St::IDLE) keep_a = std::min(keep_a, std::min(a0_, hit_a_ - kPreroll));
        if (keep_a - audio_base_ > 10 * kFs) {
            audio_.erase(audio_.begin(), audio_.begin() + (keep_a - audio_base_));
            audio_base_ = keep_a;
        }
    }

    PreambleCorrelator corr_;
    std::vector<float> taps_;
    long half_ = 0;
    std::complex<float> lo_[80];
    long gap_blocks_ = 50;

    std::vector<float> audio_;
    long audio_base_ = 0, audio_end_ = 0;
    std::vector<std::complex<float>> bb_;
    long bb_base_ = 0;
    long scan_t_ = 0;

    bool have_cand_ = false;
    long cand_t_ = 0;
    float cand_m_ = 0;

    St st_ = St::IDLE;
    long hit_t_ = 0, hit_a_ = 0;
    float hit_m_ = 0;
    float hit_f_ = 0;  // carrier offset, Hz, from the hit's common segment
    int mode_frames_ = 0, frames_used_ = 0;
    std::array<float, 8> d1_sc_{}, d2_sc_{};
    long mode_need_bb_ = 0;

    uint64_t next_id_ = 0, id_ = 0;
    Mode mode_ = Mode::AUTO;
    bool deliver_ = false;
    int preamble_frames_ = 3;
    long a0_ = 0;
    double end_level_ = 0;
    long level_from_ = 0;
    std::deque<double> gap_;
    double gap_sum_ = 0;
};

void rx_scan() {
    auto sc = std::make_unique<Scanner>();
    std::vector<float> x;
    while (g_audio.pop(x)) sc->feed(x);
    sc->finish();
}

void rx_reader() {
    struct EndOfInput {  // whatever way the reader leaves, the scanner sees the end
        ~EndOfInput() { g_audio.push({}, true); }
    } end_of_input;
    if (cfg.rx_in.empty()) return;
    int fd = cfg.rx_in == "-" ? 0 : ::open(cfg.rx_in.c_str(), O_RDONLY);
    if (fd < 0) { logf("rx: cannot open %s", cfg.rx_in.c_str()); return; }
    struct stat sb;
    if (fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode)) {
        // a file is read once a DTE is there to take it; a stream is read from the start, so
        // nothing backs up behind the modem
        std::unique_lock<std::mutex> lk(M.mu);
        M.cv.wait(lk, [] { return M.stop || M.dte(); });
    }
    auto readn = [&](uint8_t* b, size_t n) -> size_t {
        size_t got = 0;
        while (got < n) {
            ssize_t r = ::read(fd, b + got, n - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += size_t(r);
        }
        return got;
    };
    // WAV header (RIFF), or raw s16le 48 kHz mono
    uint8_t hdr[12];
    size_t pre = readn(hdr, 12);
    std::vector<uint8_t> carry;
    if (pre == 12 && !std::memcmp(hdr, "RIFF", 4) && !std::memcmp(hdr + 8, "WAVE", 4)) {
        for (;;) {
            uint8_t c[8];
            if (readn(c, 8) != 8) { logf("rx: WAV without a data chunk"); return; }
            uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | uint32_t(c[7]) << 24;
            if (!std::memcmp(c, "data", 4)) break;
            std::vector<uint8_t> b(sz + (sz & 1));
            if (readn(b.data(), b.size()) != b.size()) { logf("rx: short WAV chunk"); return; }
            if (!std::memcmp(c, "fmt ", 4)) {
                int fmt = b[0] | b[1] << 8, ch = b[2] | b[3] << 8, bits = b[14] | b[15] << 8;
                uint32_t sr = b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
                if (fmt != 1 || ch != 1 || bits != 16 || sr != 48000) {
                    logf("rx: only 16-bit mono 48 kHz PCM WAV is supported");
                    return;
                }
            }
        }
    } else {
        carry.assign(hdr, hdr + pre);
    }
    const size_t blk = 480;  // 10 ms
    std::vector<uint8_t> raw(blk * 2);
    uint64_t total = 0;
    for (;;) {
        size_t have = std::min(carry.size(), raw.size());
        std::memcpy(raw.data(), carry.data(), have);
        carry.erase(carry.begin(), carry.begin() + have);
        size_t got = have + readn(raw.data() + have, raw.size() - have);
        size_t ns = got / 2;
        std::vector<float> x(ns);
        for (size_t i = 0; i < ns; i++) x[i] = float(int16_t(raw[2 * i] | raw[2 * i + 1] << 8)) / 32768.0f;
        total += ns;
        g_audio.push(std::move(x), false);
        if (ns < blk) break;
    }
    logf("rx: input ended after %.2f s of audio", double(total) / kFs);
    if (fd > 0) ::close(fd);
}

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "m110_lan: %s\n", msg);
    fprintf(stderr,
            "usage: m110_lan [--mode M] [--tx-out FILE|-] [--tx-format wav|raw] [--rx-in FILE|-]\n"
            "                [--port P] [--rci-port P] [--udp-port P] [--bind ADDR]\n"
            "                [--duplex full|half-tx|half-rx] [--rx-fast] [--rx-gap S] [--rx-max S]\n"
            "                [--rx-threshold X] [--rx-low-quality]\n"
            "                [--tx-lead S] [--tx-tail S] [--tx-queue BYTES] [--min-latency MS] [--max-latency MS]\n"
            "  --mode       transmit waveform: 150S 150L 300S 300L 600S 600L 1200S 1200L 2400S 2400L 4800S\n"
            "               (default 600S). 75S, 75L, 4800L and 110D waveforms are refused: no data path.\n"
            "  --tx-out     transmit audio: a WAV file per transmission (%%d in the name numbers them)\n"
            "               or - for raw s16le 48 kHz on stdout\n"
            "  --rx-in      receive audio: WAV (16-bit mono 48 kHz) or raw s16le 48 kHz; - is stdin\n"
            "  --rx-gap     a burst ends after S seconds below its own end level (default 0.5)\n"
            "  --rx-max     longest burst, seconds (default 120)\n"
            "  --rx-threshold  preamble correlation that starts a burst, 0..1 (default 0.45)\n"
            "  --rx-low-quality  deliver bytes the decoder scores below its quality gate (default off;\n"
            "               Appendix A has no field to mark them, so only the log says so)\n"
            "  --rx-squelch is accepted and has no effect: bursts are found by their preamble\n");
    exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { if (++i >= argc) usage("missing value"); return argv[i]; };
        if (a == "--mode") cfg.mode_name = val();
        else if (a == "--tx-out") cfg.tx_out = val();
        else if (a == "--tx-format") cfg.tx_format = val();
        else if (a == "--rx-in") cfg.rx_in = val();
        else if (a == "--port") cfg.tcp_port = atoi(val().c_str());
        else if (a == "--rci-port") cfg.rci_port = atoi(val().c_str());
        else if (a == "--udp-port") cfg.udp_port = atoi(val().c_str());
        else if (a == "--bind") cfg.bind = val();
        else if (a == "--rx-fast") cfg.rx_realtime = false;
        else if (a == "--verbose") cfg.verbose = true;
        else if (a == "--rx-gap") cfg.rx_gap = atof(val().c_str());
        else if (a == "--rx-squelch") { val(); logf("--rx-squelch has no effect: bursts are found by their preamble"); }
        else if (a == "--rx-max") cfg.rx_max = atof(val().c_str());
        else if (a == "--rx-threshold") cfg.rx_threshold = atof(val().c_str());
        else if (a == "--rx-low-quality") cfg.rx_low_quality = true;
        else if (a == "--tx-lead") cfg.tx_lead = atof(val().c_str());
        else if (a == "--tx-tail") cfg.tx_tail = atof(val().c_str());
        else if (a == "--tx-queue") cfg.tx_queue = size_t(atol(val().c_str()));
        else if (a == "--min-latency") cfg.min_latency_ms = uint32_t(atol(val().c_str()));
        else if (a == "--max-latency") cfg.max_latency_ms = uint32_t(atol(val().c_str()));
        else if (a == "--udp-set-timeout") cfg.udp_set_timeout = atof(val().c_str());
        else if (a == "--duplex") {
            std::string d = val();
            if (d == "full") cfg.duplex = Duplex::FULL;
            else if (d == "half-tx") cfg.duplex = Duplex::HALF_TX_MASTER;
            else if (d == "half-rx") cfg.duplex = Duplex::HALF_RX_MASTER;
            else usage("--duplex takes full, half-tx or half-rx");
        } else if (a == "-h" || a == "--help") usage(nullptr);
        else usage(("unknown option " + a).c_str());
    }
    auto m = kModes.find(cfg.mode_name);
    if (m == kModes.end()) {
        logf("transmit waveform %s is not supported: this modem has a data path for 150S 150L 300S 300L "
             "600S 600L 1200S 1200L 2400S 2400L 4800S only", cfg.mode_name.c_str());
        return 2;
    }
    cfg.mode = m->second;
    if (cfg.tx_out.empty()) usage("--tx-out is required");
    if (cfg.tx_format.empty()) cfg.tx_format = cfg.tx_out == "-" ? "raw" : "wav";
    if (cfg.tx_format != "wav" && cfg.tx_format != "raw") usage("--tx-format takes wav or raw");
    if (cfg.tx_queue < 2 * tcp::kMaxData) usage("--tx-queue is below two packets");

    // The decoder prints to stdout; keep stdout for audio and send fd 1 to stderr.
    g_audio_out_fd = ::dup(1);
    ::dup2(2, 1);
    signal(SIGPIPE, SIG_IGN);

    int tl = listen_tcp(cfg.tcp_port);
    int rl = listen_tcp(cfg.rci_port);
    M.udp_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in ua{};
    ua.sin_family = AF_INET;
    ua.sin_port = htons(uint16_t(cfg.udp_port));
    inet_pton(AF_INET, cfg.bind.c_str(), &ua.sin_addr);
    if (::bind(M.udp_fd, reinterpret_cast<sockaddr*>(&ua), sizeof ua)) {
        logf("cannot bind UDP %d: %s", cfg.udp_port, strerror(errno));
        return 1;
    }
    logf("ready: tcp %d, rci %d, udp %d, tx %s %u bps blocking factor %u", cfg.tcp_port, cfg.rci_port, cfg.udp_port,
         cfg.mode_name.c_str(), unsigned(mode_bitrate(cfg.mode)), blocking_factor(cfg.mode));

    std::thread(accept_loop, tl, rl).detach();
    std::thread(udp_loop).detach();
    std::thread(timer_loop).detach();
    std::thread(tx_loop).detach();
    std::thread(rx_decode).detach();
    std::thread(rx_scan).detach();
    std::thread rx(rx_reader);
    // wake the reader of a file input when a DTE arrives
    std::thread([] {
        while (!M.stop) {
            { std::lock_guard<std::mutex> lk(M.mu); if (M.dte()) M.cv.notify_all(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();
    rx.join();
    for (;;) pause();
}
