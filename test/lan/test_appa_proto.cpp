// Unit checks for m110_lan/appa_proto.h against MIL-STD-188-110D Appendix A.
// Each check names the paragraph, table or figure it tests.
#include "m110_lan/appa_proto.h"

#include <cstdio>
#include <string>

using namespace appa;

static int passed = 0, failed = 0;
static void check(bool ok, const std::string& what) {
    printf("%s ... %s\n", what.c_str(), ok ? "PASS" : "FAIL");
    (ok ? passed : failed)++;
}

// CRC by the polynomial written out MSB-first on bit-reversed input, a second route to the
// A.5.3 definition (x16+x15+x12+x11+x8+x6+x3+1, LSB of each byte first, initial 0).
static uint16_t crc_by_poly(const uint8_t* p, size_t n) {
    const uint32_t poly = 0x19949;  // bits 16,15,12,11,8,6,3,0
    uint32_t r = 0;
    for (size_t i = 0; i < n; i++)
        for (int b = 0; b < 8; b++) {  // LSB first
            uint32_t in = (p[i] >> b) & 1;
            uint32_t top = (r >> 15) & 1;
            r = ((r << 1) & 0xFFFF);
            if (top ^ in) r ^= poly & 0xFFFF;
        }
    // the register above holds the CRC bit-reversed relative to the LSB-first shift register
    uint16_t out = 0;
    for (int b = 0; b < 16; b++) if (r & (1u << b)) out |= uint16_t(1u << (15 - b));
    return out;
}

int main() {
    // ---- A.5.3 CRC ----
    {
        const char* s = "123456789";
        uint16_t a = crc16(reinterpret_cast<const uint8_t*>(s), 9);
        uint16_t b = crc_by_poly(reinterpret_cast<const uint8_t*>(s), 9);
        char buf[160];
        snprintf(buf, sizeof buf, "A.5.3 CRC of \"123456789\": listing 0x%04X, polynomial route 0x%04X", a, b);
        check(a == b, buf);
        bool all = true;
        uint8_t d[64];
        for (int t = 0; t < 200; t++) {
            size_t n = size_t(t % 64);
            for (size_t i = 0; i < n; i++) d[i] = uint8_t(t * 31 + i * 7);
            all &= crc16(d, n) == crc_by_poly(d, n);
        }
        check(all, "A.5.3 CRC: listing and polynomial route agree on 200 buffers");
        check(crc16(d, 0) == 0, "A.5.3 CRC: initial value 0");
    }
    // ---- A.5.1.1 TCP framing ----
    {
        auto p = tcp::connect();
        bool ok = p.size() == 11 && p[0] == 0x49 && p[1] == 0x50 && p[2] == 0x55 && p[3] == 0x01 && p[4] == 0 &&
                  p[5] == 1 && get16(&p[6]) == crc16(p.data(), 6) && p[8] == 12 && get16(&p[9]) == crc16(&p[8], 1);
        check(ok, "A.5.1.1.1/A.5.1.1.2 CONNECT: preamble 49 50 55, type 01, size 1, header CRC, version 12, payload CRC");
        auto k = tcp::keepalive();
        check(k.size() == 8 && k[3] == 0 && k[4] == 0 && k[5] == 0, "A.5.1.2.2 keep-alive: DATA header, size 0, no payload CRC");
        tcp::Parser ps;
        auto good = tcp::command(tcp::TRANSMIT_ARM);
        auto bad = good;
        bad[8] ^= 1;  // payload byte damaged: payload CRC fails
        std::vector<uint8_t> stream = {0x00, 0x49};
        stream.insert(stream.end(), bad.begin(), bad.end());
        stream.insert(stream.end(), good.begin(), good.end());
        ps.feed(stream.data(), stream.size());
        auto t = ps.take();
        bool only_good = t && t->type == tcp::DATA && t->payload.size() == 1 && t->payload[0] == tcp::TRANSMIT_ARM && !ps.take();
        check(only_good, "A.5.1.2.3 parser: the packet with a bad payload CRC is dropped, the next one read");
    }
    // ---- A.5.2.5 RS generator: g(x) = x^4 + a^13 x^3 + a^6 x^2 + a^3 x + a^10 ----
    {
        const rs::Gf16& f = rs::gf();
        auto g = rs::generator(4);  // g[i] = coefficient of x^i
        bool ok = g.size() == 5 && g[4] == 1 && g[3] == f.pow_a(13) && g[2] == f.pow_a(6) && g[1] == f.pow_a(3) &&
                  g[0] == f.pow_a(10);
        check(ok, "A.5.2.5 generator with roots a^1..a^4 over GF(16) mod x^4+x+1 equals the printed g(x)");
    }
    // ---- A.5.2.5 RS erasure decoding, every erasure pattern up to n-k ----
    {
        bool ok = true;
        int cases = 0;
        const int codes[3][2] = {{6, 2}, {7, 4}, {8, 5}};  // Table A-XVII shortened (N, K)
        for (auto& c : codes) {
            int n = c[0], k = c[1];
            for (int trial = 0; trial < 16; trial++) {
                std::vector<uint8_t> d(k);
                for (int i = 0; i < k; i++) d[i] = uint8_t((trial * 5 + i * 11 + 3) & 15);
                auto cw = rs::encode(d, n);
                for (int i = 0; i < k; i++) ok &= cw[n - k + i] == d[i];  // systematic, data after parity
                for (unsigned mask = 0; mask < (1u << n); mask++) {
                    if (__builtin_popcount(mask) > n - k) continue;
                    std::vector<int> er;
                    auto r = cw;
                    for (int j = 0; j < n; j++) if (mask & (1u << j)) { er.push_back(j); r[j] = 0xF; }
                    ok &= rs::decode(r, k, er) && r == cw;
                    cases++;
                }
                std::vector<int> too_many;
                for (int j = 0; j <= n - k; j++) too_many.push_back(j);
                auto r = cw;
                ok &= !rs::decode(r, k, too_many);
            }
        }
        check(ok, "A.5.2.5 erasure decode: (6,2) (7,4) (8,5), every pattern of up to N-K erasures (" +
                      std::to_string(cases) + " cases), and N-K+1 refused");
    }
    // ---- A.5.2.5.1 FEC and interleaving example: ABCDEFGHIJKLMNOP, (7,4), L = 14 ----
    {
        std::string s = "ABCDEFGHIJKLMNOP";
        std::vector<uint8_t> user(s.begin(), s.end());
        size_t per = 14 / 7;  // L / N
        check(rs::user_bytes_per_set(per, 4) == 8, "A.5.2.5.1 one interleaver set carries 8 user bytes (L/N * K)");
        auto set1 = rs::encode_set(user.data(), per, 7, 4);
        auto set2 = rs::encode_set(user.data() + 8, per, 7, 4);
        // Table A-XIX packets 4-7: AaEe BbFf CcGg DdHh; Table A-XX packets 11-14: IiMm JjNn KkOo LlPp
        bool ok = true;
        for (int j = 0; j < 4; j++) {
            ok &= set1[3 + j][0] == uint8_t('A' + j) && set1[3 + j][1] == uint8_t('E' + j);
            ok &= set2[3 + j][0] == uint8_t('I' + j) && set2[3 + j][1] == uint8_t('M' + j);
        }
        check(ok, "Table A-XIX/A-XX: data packets 4-7 carry AaEe BbFf CcGg DdHh, packets 11-14 IiMm JjNn KkOo LlPp");
        // first byte of packet 1 = parity X of codeword 1 (high nibble) and x of codeword 2 (low)
        auto cw1 = rs::encode({uint8_t('A' >> 4), uint8_t('B' >> 4), uint8_t('C' >> 4), uint8_t('D' >> 4)}, 7);
        auto cw2 = rs::encode({uint8_t('A' & 15), uint8_t('B' & 15), uint8_t('C' & 15), uint8_t('D' & 15)}, 7);
        check(set1[0][0] == uint8_t(cw1[0] << 4 | cw2[0]), "Table A-XVIII: packet 1 byte 1 is X (codeword 1) and x (codeword 2)");
        // packets 2, 5 and 6 lost
        std::vector<bool> present = {true, false, true, true, false, false, true};
        auto lost = set1;
        for (int j = 0; j < 7; j++) if (!present[j]) lost[j].assign(per, 0x5A);
        std::vector<uint8_t> out;
        bool dec = rs::decode_set(lost, present, per, 7, 4, out);
        check(dec && std::string(out.begin(), out.end()) == "ABCDEFGH",
              "A.5.2.5.1: packets 2, 5, 6 lost, the set still decodes to ABCDEFGH");
        present[0] = false;
        check(!rs::decode_set(lost, present, per, 7, 4, out), "A.5.2.5: four lost packets of a (7,4) set are refused");
    }
    // ---- Figure A-22 interleaver length ----
    {
        // 600S: B = 360 bits, K = 2 -> ceil(360/16) = 23 bytes per packet, L = 138
        check(rs::bytes_per_packet(360, 2) == 23, "Figure A-22: B=360, K=2 gives L/N = 23");
        check(rs::bytes_per_packet(4800 * 48, 2) == 1200, "Figure A-22: L/N is capped at 1200");
    }
    // ---- A.5.2.5.2 EOM ----
    {
        std::vector<uint8_t> u = {1, 2, 0, 0};
        auto st = rs::frame_stream(u, 46);
        bool ok = st.size() == 46 && st[4] == 0x4B && st[11] == 0x69 && st[45] == 0;
        auto back = st;
        ok &= rs::strip_eom(back) && back == u;
        check(ok, "A.5.2.5.2: EOM 4B 65 A5 B2 26 D2 D3 69 appended, zero padding to the set, both removed (trailing user zeros kept)");
    }
    // ---- A.5.2.2 / A.5.2.3 UDP framing ----
    {
        auto p = udp::packet(udp::PING_REQUEST, 0xABCDEF, {});
        bool ok = p.size() == 6 && p[0] == 0x11 && p[1] == 0xAB && p[2] == 0xCD && p[3] == 0xEF &&
                  get16(&p[4]) == crc16(p.data(), 4);
        check(ok, "A.5.2.3 PING REQUEST: version 1 / type 1 in byte 0, session 24 bits, CRC over header");
        ok = udp::parse(p.data(), p.size()).has_value();
        p[0] = 0x21;  // version 2
        p[4] = uint8_t(crc16(p.data(), 4) >> 8); p[5] = uint8_t(crc16(p.data(), 4));
        ok &= !udp::parse(p.data(), p.size()).has_value();
        check(ok, "A.5.2.3: only version 1 is processed");
        udp::StatusReply s;
        check(udp::status_payload(s).size() == 32, "Table A-X: STATUS REPLY payload is 32 bytes");
        udp::DataHeader h;
        uint8_t d[3] = {1, 2, 3};
        check(udp::data_payload(false, h, d, 3).size() == 19 && udp::data_payload(true, h, d, 3).size() == 23,
              "Table A-X: ENCODED DATA 16 + data, ENHANCED ENCODED DATA 20 + data");
        check(udp::status_vector(1, true, 3, true) == 0x37, "Figure A-15: rx b6-5, cd b4, tx b3-1, in control b0");
    }
    printf("\n%d/%d Appendix A protocol checks passed\n", passed, passed + failed);
    return failed ? 1 : 0;
}
