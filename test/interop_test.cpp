/**
 * @file interop_test.cpp
 * @brief Cross-modem interoperability test - parallel version
 * 
 * Tests all combinations:
 *   - PN TX → Brain RX (auto-detect only - Brain has no explicit mode set)
 *   - Brain TX → PN RX (explicit mode)
 *   - Brain TX → PN RX (auto-detect)
 * 
 * Runs modes in parallel for ~10s instead of 2min.
 */

// Include Brain wrapper FIRST to avoid std::byte conflict
#include "extern/brain_wrapper.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

// PhoenixNest modem API
#include "api/modem_tx.h"
#include "api/modem_rx.h"
#include "api/modem_config.h"
#include "api/modem_types.h"
#include "api/version.h"

using namespace std;

// Alias PhoenixNest types
using PNMode = m110a::api::Mode;
using PNTxConfig = m110a::api::TxConfig;
using PNRxConfig = m110a::api::RxConfig;
using PNModemTX = m110a::api::ModemTX;
using PNModemRX = m110a::api::ModemRX;

// Thread-safe output
mutex g_output_mutex;
bool g_json_output = false;

const string TEST_MESSAGE = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

struct ModeMapping { PNMode phoenix; brain::Mode brain; const char* name; int bps; };

const ModeMapping MODES[] = {
    { PNMode::M75_SHORT,   brain::Mode::M75S,   "75S",   75   },
    { PNMode::M75_LONG,    brain::Mode::M75L,   "75L",   75   },
    { PNMode::M150_SHORT,  brain::Mode::M150S,  "150S",  150  },
    { PNMode::M150_LONG,   brain::Mode::M150L,  "150L",  150  },
    { PNMode::M300_SHORT,  brain::Mode::M300S,  "300S",  300  },
    { PNMode::M300_LONG,   brain::Mode::M300L,  "300L",  300  },
    { PNMode::M600_SHORT,  brain::Mode::M600S,  "600S",  600  },
    { PNMode::M600_LONG,   brain::Mode::M600L,  "600L",  600  },
    { PNMode::M1200_SHORT, brain::Mode::M1200S, "1200S", 1200 },
    { PNMode::M1200_LONG,  brain::Mode::M1200L, "1200L", 1200 },
    { PNMode::M2400_SHORT, brain::Mode::M2400S, "2400S", 2400 },
    { PNMode::M2400_LONG,  brain::Mode::M2400L, "2400L", 2400 },
};
const int NUM_MODES = 12;

struct TestResult { 
    bool pass = false; 
    int decoded = 0; 
    int expected = 0; 
    double ber = 1.0; 
    string detected_mode; 
    string error; 
};

struct ModeResults {
    string mode_name;
    int bps;
    TestResult pn_to_brain;      // PN TX → Brain RX (auto-detect)
    TestResult brain_to_pn_set;  // Brain TX → PN RX (explicit mode)
    TestResult brain_to_pn_auto; // Brain TX → PN RX (auto-detect)
};

int calc_bit_errors(const vector<uint8_t>& exp, const vector<uint8_t>& act) {
    int errors = 0;
    size_t len = min(exp.size(), act.size());
    for (size_t i = 0; i < len; i++) { 
        uint8_t d = exp[i] ^ act[i]; 
        while(d) { errors += d&1; d>>=1; } 
    }
    if (exp.size() > act.size()) errors += (exp.size() - act.size()) * 8;
    return errors;
}

vector<int16_t> f2i(const vector<float>& s) {
    vector<int16_t> r(s.size());
    for (size_t i = 0; i < s.size(); i++) { 
        float v = s[i]*32767.0f; 
        r[i] = (int16_t)max(-32768.0f, min(32767.0f, v)); 
    }
    return r;
}

vector<float> i2f(const vector<int16_t>& s) {
    vector<float> r(s.size());
    for (size_t i = 0; i < s.size(); i++) r[i] = s[i] / 32767.0f;
    return r;
}

// PN TX → Brain RX (Brain only supports auto-detect)
TestResult test_pn_to_brain(const ModeMapping& m, const vector<uint8_t>& data) {
    TestResult r; r.expected = data.size(); r.detected_mode = "---";
    try {
        PNTxConfig cfg = PNTxConfig::for_mode(m.phoenix); 
        cfg.sample_rate = 48000.0f;
        PNModemTX tx(cfg);
        auto res = tx.encode(data);
        if (!res.ok()) { r.error = "TX failed"; return r; }
        
        brain::Modem rx;
        auto dec = rx.decode_48k(f2i(res.value()));
        r.detected_mode = rx.get_detected_mode_name();
        r.decoded = dec.size();
        int err = calc_bit_errors(data, dec);
        r.ber = data.size() > 0 ? (double)err / (data.size() * 8) : 1.0;
        r.pass = (err == 0);
    } catch (const exception& e) { r.error = e.what(); } 
    catch (...) { r.error = "Unknown"; }
    return r;
}

// Brain TX → PN RX (explicit mode)
TestResult test_brain_to_pn_set(const ModeMapping& m, const vector<uint8_t>& data) {
    TestResult r; r.expected = data.size();
    try {
        brain::Modem tx;
        auto pcm = tx.encode_48k(data, m.brain);
        
        PNRxConfig cfg = PNRxConfig::for_mode(m.phoenix); 
        cfg.sample_rate = 48000.0f;
        PNModemRX rx(cfg);
        auto res = rx.decode(i2f(pcm));
        if (!res.success) { 
            r.error = res.error.has_value() ? res.error->message : "RX failed"; 
            return r; 
        }
        r.decoded = res.data.size();
        int err = calc_bit_errors(data, res.data);
        r.ber = data.size() > 0 ? (double)err / (data.size() * 8) : 1.0;
        r.pass = (err == 0);
    } catch (const exception& e) { r.error = e.what(); } 
    catch (...) { r.error = "Unknown"; }
    return r;
}

// Brain TX → PN RX (auto-detect)
TestResult test_brain_to_pn_auto(const ModeMapping& m, const vector<uint8_t>& data) {
    TestResult r; r.expected = data.size(); r.detected_mode = "---";
    try {
        brain::Modem tx;
        auto pcm = tx.encode_48k(data, m.brain);
        
        PNRxConfig cfg; 
        cfg.mode = PNMode::AUTO; 
        cfg.sample_rate = 48000.0f;
        PNModemRX rx(cfg);
        auto res = rx.decode(i2f(pcm));
        if (!res.success) { 
            r.error = res.error.has_value() ? res.error->message : "RX failed"; 
            return r; 
        }
        r.detected_mode = m110a::api::mode_name(res.mode);
        r.decoded = res.data.size();
        int err = calc_bit_errors(data, res.data);
        r.ber = data.size() > 0 ? (double)err / (data.size() * 8) : 1.0;
        r.pass = (err == 0);
    } catch (const exception& e) { r.error = e.what(); } 
    catch (...) { r.error = "Unknown"; }
    return r;
}

// Thread-safe JSON output
void json_evt(const string& t, const string& b) { 
    lock_guard<mutex> lock(g_output_mutex);
    cout << "{\"event\":\"" << t << "\"," << b << "}\n"; 
    cout.flush(); 
}

// Run all tests for a single mode (called from thread)
ModeResults test_mode(const ModeMapping& m, const vector<uint8_t>& data) {
    ModeResults results;
    results.mode_name = m.name;
    results.bps = m.bps;
    
    results.pn_to_brain = test_pn_to_brain(m, data);
    results.brain_to_pn_set = test_brain_to_pn_set(m, data);
    results.brain_to_pn_auto = test_brain_to_pn_auto(m, data);
    
    return results;
}

int main(int argc, char* argv[]) {
    // Force unbuffered output for real-time output
    cout.setf(ios::unitbuf);
    cerr.setf(ios::unitbuf);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    // Always output version header first - critical for record keeping
    cerr << "==============================================\n";
    cerr << m110a::version_header() << "\n";
    cerr << "==============================================\n";
    cerr << m110a::build_info() << "\n";
    cerr << "Test: M110A Cross-Modem Interoperability\n";
    cerr << "==============================================\n" << flush;
    
    bool parallel = false;  // Brain modem has global state, not thread-safe
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) g_json_output = true;
        if (strcmp(argv[i], "--parallel") == 0) parallel = true;  // Use at own risk
    }
    
    vector<uint8_t> data(TEST_MESSAGE.begin(), TEST_MESSAGE.end());
    const int TESTS_PER_MODE = 3;  // PN→Brain, Brain→PN(set), Brain→PN(auto)
    const int TOTAL_TESTS = NUM_MODES * TESTS_PER_MODE;
    
    if (g_json_output) {
        json_evt("start", "\"version\":\"" + string(m110a::version()) + "\",\"build\":" + to_string(m110a::BUILD_NUMBER) + ",\"commit\":\"" + string(m110a::GIT_COMMIT) + "\",\"branch\":\"" + string(m110a::GIT_BRANCH) + "\",\"total_tests\":" + to_string(TOTAL_TESTS) + ",\"message_size\":" + to_string(data.size()));
    } else {
        cout << "+====================================================================================+\n";
        cout << "|              M110A CROSS-MODEM INTEROPERABILITY TEST                              |\n";
        cout << "+====================================================================================+\n";
        cout << m110a::version_header() << "\n";
        cout << m110a::build_info() << "\n\n";
        cout << "Test: \"" << TEST_MESSAGE << "\" (" << data.size() << " bytes)\n";
        cout << "Modes: " << NUM_MODES << " | Tests per mode: " << TESTS_PER_MODE << " | Total: " << TOTAL_TESTS << "\n";
        cout << "Note: Brain RX only supports auto-detect (no explicit mode setting)\n\n";
        cout << "+--------+------+--------------+--------------+--------------+\n";
        cout << "|  Mode  |  BPS | PN->Brain    | Br->PN(set)  | Br->PN(auto) |\n";
        cout << "+--------+------+--------------+--------------+--------------+\n";
    }
    
    auto start = chrono::high_resolution_clock::now();
    
    // Storage for results
    vector<ModeResults> all_results(NUM_MODES);
    
    if (parallel) {
        // Parallel execution - one thread per mode
        vector<thread> threads;
        for (int i = 0; i < NUM_MODES; i++) {
            threads.emplace_back([i, &data, &all_results]() {
                all_results[i] = test_mode(MODES[i], data);
            });
        }
        for (auto& t : threads) t.join();
    } else {
        // Sequential execution
        for (int i = 0; i < NUM_MODES; i++) {
            all_results[i] = test_mode(MODES[i], data);
        }
    }
    
    // Output results in order
    int p1=0, p2=0, p3=0;
    for (int i = 0; i < NUM_MODES; i++) {
        const auto& r = all_results[i];
        if (r.pn_to_brain.pass) p1++;
        if (r.brain_to_pn_set.pass) p2++;
        if (r.brain_to_pn_auto.pass) p3++;
        
        if (g_json_output) {
            ostringstream s;
            s << "\"mode\":\"" << r.mode_name << "\""
              // Pass/fail flags
              << ",\"pn_brain\":" << (r.pn_to_brain.pass ? "true" : "false")
              << ",\"brain_pn\":" << (r.brain_to_pn_set.pass ? "true" : "false")
              << ",\"brain_pn_set\":" << (r.brain_to_pn_set.pass ? "true" : "false")
              << ",\"brain_pn_auto\":" << (r.brain_to_pn_auto.pass ? "true" : "false")
              << ",\"auto\":" << (r.brain_to_pn_auto.pass ? "true" : "false")
              // Detected modes
              << ",\"detected_pn\":\"" << r.brain_to_pn_auto.detected_mode << "\""
              << ",\"detected_brain\":\"" << r.pn_to_brain.detected_mode << "\""
              // BER values
              << ",\"ber_pn_brain\":" << fixed << setprecision(4) << r.pn_to_brain.ber
              << ",\"ber_brain_pn_set\":" << r.brain_to_pn_set.ber
              << ",\"ber_brain_pn_auto\":" << r.brain_to_pn_auto.ber
              // Byte counts
              << ",\"decoded_pn_brain\":" << r.pn_to_brain.decoded
              << ",\"decoded_brain_pn_set\":" << r.brain_to_pn_set.decoded
              << ",\"decoded_brain_pn_auto\":" << r.brain_to_pn_auto.decoded
              << ",\"expected\":" << r.pn_to_brain.expected
              // Error messages
              << ",\"error_pn_brain\":\"" << r.pn_to_brain.error << "\""
              << ",\"error_brain_pn_set\":\"" << r.brain_to_pn_set.error << "\""
              << ",\"error_brain_pn_auto\":\"" << r.brain_to_pn_auto.error << "\"";
            json_evt("result", s.str());
        } else {
            auto fmt = [](const TestResult& t, bool show_mode = false) {
                string s;
                if (show_mode && !t.detected_mode.empty() && t.detected_mode != "---") {
                    s = t.pass ? " PASS " : " FAIL ";
                    s += t.detected_mode.substr(0, 6);
                } else {
                    s = t.pass ? "    PASS     " : "    FAIL     ";
                }
                return s;
            };
            
            cout << "| " << setw(6) << r.mode_name << " | " << setw(4) << r.bps << " |";
            cout << fmt(r.pn_to_brain, true) << "|";
            cout << fmt(r.brain_to_pn_set) << "|";
            cout << fmt(r.brain_to_pn_auto, true) << "|\n";
        }
    }
    
    auto end = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double>(end - start).count();
    
    int total_passed = p1 + p2 + p3;
    
    if (g_json_output) {
        ostringstream s;
        s << "\"passed\":" << total_passed 
          << ",\"total\":" << TOTAL_TESTS
          << ",\"pn_brain\":" << p1
          << ",\"brain_pn_set\":" << p2
          << ",\"brain_pn_auto\":" << p3
          << ",\"elapsed\":" << fixed << setprecision(1) << elapsed;
        json_evt("complete", s.str());
    } else {
        cout << "+--------+------+--------------+--------------+--------------+\n";
        cout << "| TOTAL  |      |  " << setw(2) << p1 << "/12       |  " << setw(2) << p2 << "/12       |  " << setw(2) << p3 << "/12      |\n";
        cout << "+--------+------+--------------+--------------+--------------+\n";
        cout << "\nPN->Brain:       " << p1 << "/12 (Brain auto-detects mode from preamble)\n";
        cout << "Brain->PN(set):  " << p2 << "/12 (PN RX mode explicitly set)\n";
        cout << "Brain->PN(auto): " << p3 << "/12 (PN RX auto-detects mode)\n";
        cout << "\nTotal: " << total_passed << "/" << TOTAL_TESTS << " passed (" 
             << fixed << setprecision(1) << 100.0*total_passed/TOTAL_TESTS << "%) in " 
             << setprecision(1) << elapsed << "s" << (parallel ? " (parallel)" : " (sequential)") << "\n";
    }
    
    return (total_passed == TOTAL_TESTS) ? 0 : 1;
}
