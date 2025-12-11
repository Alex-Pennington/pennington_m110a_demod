/**
 * @file brain_exhaustive_test.cpp
 * @brief Exhaustive test suite for Brain (Paul's) M110A modem
 * 
 * Tests Brain modem across all modes, SNR levels, and channel conditions.
 * Uses the brain_wrapper.h interface to the Cm110s class.
 * 
 * Usage:
 *   brain_exhaustive_test.exe [options]
 * 
 * Options:
 *   --duration N    Run for N seconds
 *   --mode MODE     Test only specific mode (e.g., 600S, 1200L)
 *   --modes LIST    Comma-separated list of modes (e.g., 600S,600L,1200S)
 *   --json          Machine-readable JSON output
 *   --help          Show this help
 */

// Include Brain wrapper FIRST to avoid std::byte conflict
#include "extern/brain_wrapper.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <random>
#include <cmath>
#include <map>

#include "api/version.h"

using namespace std;
using namespace std::chrono;

// ============================================================
// Mode definitions
// ============================================================

struct ModeInfo {
    brain::Mode mode;
    const char* name;
    int bps;
};

const ModeInfo ALL_MODES[] = {
    { brain::Mode::M75S,   "75S",   75   },
    { brain::Mode::M75L,   "75L",   75   },
    { brain::Mode::M150S,  "150S",  150  },
    { brain::Mode::M150L,  "150L",  150  },
    { brain::Mode::M300S,  "300S",  300  },
    { brain::Mode::M300L,  "300L",  300  },
    { brain::Mode::M600S,  "600S",  600  },
    { brain::Mode::M600L,  "600L",  600  },
    { brain::Mode::M1200S, "1200S", 1200 },
    { brain::Mode::M1200L, "1200L", 1200 },
    { brain::Mode::M2400S, "2400S", 2400 },
    { brain::Mode::M2400L, "2400L", 2400 },
};
const int NUM_MODES = 12;

// ============================================================
// Channel simulation
// ============================================================

struct ChannelCondition {
    string name;
    float snr_db;
    float freq_offset_hz;
    int multipath_delay;
    float multipath_gain;
};

vector<ChannelCondition> get_standard_channels() {
    return {
        {"clean",       100.0f, 0.0f, 0,  0.0f},
        {"awgn_30db",   30.0f,  0.0f, 0,  0.0f},
        {"awgn_25db",   25.0f,  0.0f, 0,  0.0f},
        {"awgn_20db",   20.0f,  0.0f, 0,  0.0f},
        {"awgn_15db",   15.0f,  0.0f, 0,  0.0f},
        {"foff_1hz",    30.0f,  1.0f, 0,  0.0f},
        {"foff_5hz",    30.0f,  5.0f, 0,  0.0f},
        {"mp_24samp",   30.0f,  0.0f, 24, 0.5f},
        {"mp_48samp",   30.0f,  0.0f, 48, 0.5f},
        {"moderate_hf", 20.0f,  2.0f, 24, 0.3f},
        {"poor_hf",     15.0f,  5.0f, 48, 0.5f},
    };
}

class ChannelSimulator {
public:
    ChannelSimulator(unsigned seed = 42) : rng_(seed), dist_(0.0f, 1.0f) {}
    
    void reset(unsigned seed) { rng_.seed(seed); }
    
    vector<int16_t> apply(const vector<int16_t>& pcm, const ChannelCondition& cond) {
        vector<float> samples(pcm.size());
        for (size_t i = 0; i < pcm.size(); i++) {
            samples[i] = pcm[i] / 32768.0f;
        }
        
        // Apply frequency offset
        if (cond.freq_offset_hz > 0.01f) {
            apply_freq_offset(samples, cond.freq_offset_hz, 48000.0f);
        }
        
        // Apply multipath
        if (cond.multipath_delay > 0 && cond.multipath_gain > 0.01f) {
            apply_multipath(samples, cond.multipath_delay, cond.multipath_gain);
        }
        
        // Apply AWGN
        if (cond.snr_db < 99.0f) {
            apply_awgn(samples, cond.snr_db);
        }
        
        // Convert back
        vector<int16_t> out(samples.size());
        for (size_t i = 0; i < samples.size(); i++) {
            float v = samples[i] * 32767.0f;
            out[i] = (int16_t)max(-32768.0f, min(32767.0f, v));
        }
        return out;
    }
    
private:
    void apply_freq_offset(vector<float>& samples, float offset_hz, float sample_rate) {
        float phase = 0.0f;
        float phase_inc = 2.0f * 3.14159265f * offset_hz / sample_rate;
        for (size_t i = 0; i < samples.size(); i++) {
            float s = samples[i];
            samples[i] = s * cosf(phase);
            phase += phase_inc;
            if (phase > 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
        }
    }
    
    void apply_multipath(vector<float>& samples, int delay, float gain) {
        if (delay <= 0 || delay >= (int)samples.size()) return;
        vector<float> delayed(samples.size(), 0.0f);
        for (size_t i = delay; i < samples.size(); i++) {
            delayed[i] = samples[i - delay] * gain;
        }
        for (size_t i = 0; i < samples.size(); i++) {
            samples[i] += delayed[i];
        }
    }
    
    void apply_awgn(vector<float>& samples, float snr_db) {
        // Calculate signal power
        float sig_power = 0.0f;
        for (float s : samples) sig_power += s * s;
        sig_power /= samples.size();
        
        // Calculate noise power
        float snr_linear = powf(10.0f, snr_db / 10.0f);
        float noise_power = sig_power / snr_linear;
        float noise_std = sqrtf(noise_power);
        
        // Add Gaussian noise
        for (size_t i = 0; i < samples.size(); i++) {
            // Box-Muller transform
            float u1 = dist_(rng_);
            float u2 = dist_(rng_);
            if (u1 < 1e-10f) u1 = 1e-10f;
            float z = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265f * u2);
            samples[i] += z * noise_std;
        }
    }
    
    mt19937 rng_;
    uniform_real_distribution<float> dist_;
};

// ============================================================
// Test statistics
// ============================================================

struct Stats {
    int passed = 0;
    int failed = 0;
    int total = 0;
    double total_ber = 0.0;
    int ber_count = 0;
    
    void record(bool pass, double ber) {
        total++;
        if (pass) passed++;
        else failed++;
        total_ber += ber;
        ber_count++;
    }
    
    double pass_rate() const { return total > 0 ? 100.0 * passed / total : 0.0; }
    double avg_ber() const { return ber_count > 0 ? total_ber / ber_count : 0.0; }
};

// ============================================================
// Helpers
// ============================================================

int calc_bit_errors(const vector<uint8_t>& exp, const vector<uint8_t>& act) {
    int errors = 0;
    size_t len = min(exp.size(), act.size());
    for (size_t i = 0; i < len; i++) {
        uint8_t d = exp[i] ^ act[i];
        while (d) { errors += d & 1; d >>= 1; }
    }
    if (exp.size() > act.size()) errors += (int)(exp.size() - act.size()) * 8;
    return errors;
}

string json_escape(const string& s) {
    string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// ============================================================
// Main test runner
// ============================================================

int main(int argc, char* argv[]) {
    // Force unbuffered output
    cout.setf(ios::unitbuf);
    
    // Parse arguments
    int duration_sec = 0;
    string mode_filter;
    vector<string> mode_list;
    bool json_output = false;
    
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_sec = stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_filter = argv[++i];
            for (auto& c : mode_filter) c = toupper(c);
        } else if (arg == "--modes" && i + 1 < argc) {
            // Parse comma-separated list
            string list_str = argv[++i];
            stringstream ss(list_str);
            string mode;
            while (getline(ss, mode, ',')) {
                for (auto& c : mode) c = toupper(c);
                if (!mode.empty()) mode_list.push_back(mode);
            }
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--help" || arg == "-h") {
            cout << "Brain M110A Exhaustive Test\n\n";
            cout << "Usage: " << argv[0] << " [options]\n\n";
            cout << "Options:\n";
            cout << "  --duration N    Run for N seconds\n";
            cout << "  --mode MODE     Test specific mode (e.g., 600S)\n";
            cout << "  --modes LIST    Comma-separated list of modes (e.g., 600S,600L,1200S)\n";
            cout << "  --json          JSON output for GUI\n";
            cout << "  --help          Show this help\n";
            return 0;
        }
    }
    
    // Build mode list
    vector<ModeInfo> modes;
    
    if (!mode_list.empty()) {
        // Use comma-separated list
        for (int i = 0; i < NUM_MODES; i++) {
            for (const auto& want : mode_list) {
                if (want == ALL_MODES[i].name) {
                    modes.push_back(ALL_MODES[i]);
                    break;
                }
            }
        }
    } else if (!mode_filter.empty()) {
        // Use single mode filter
        for (int i = 0; i < NUM_MODES; i++) {
            if (mode_filter == ALL_MODES[i].name) {
                modes.push_back(ALL_MODES[i]);
            }
        }
    } else {
        // All modes
        for (int i = 0; i < NUM_MODES; i++) {
            modes.push_back(ALL_MODES[i]);
        }
    }
    
    if (modes.empty()) {
        if (json_output) {
            cout << "{\"type\":\"error\",\"message\":\"No modes match filter: " 
                 << (mode_list.empty() ? mode_filter : "(list)") << "\"}\n";
        } else {
            cerr << "No modes match filter: " << (mode_list.empty() ? mode_filter : "(list)") << "\n";
        }
        return 1;
    }
    
    auto channels = get_standard_channels();
    
    // Test message - must be long enough to survive Brain's interleaver startup discard (~26 bytes)
    // We prepend padding that will be lost, then check if the rest matches
    const int BRAIN_DISCARD_BYTES = 32;  // Conservative estimate (actual ~26)
    string padding(BRAIN_DISCARD_BYTES, 'X');  // Padding to be discarded
    string actual_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    string test_msg = padding + actual_msg;
    vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    vector<uint8_t> expected_data(actual_msg.begin(), actual_msg.end());  // What we expect to receive
    
    // Stats
    map<string, Stats> mode_stats;
    map<string, Stats> channel_stats;
    int total_tests = 0;
    int total_passed = 0;
    
    ChannelSimulator channel(42);
    
    // Start message
    if (json_output) {
        cout << "{\"type\":\"start\",\"backend\":\"Brain Direct API\",\"modes\":" << modes.size()
             << ",\"channels\":" << channels.size() << "}\n";
    } else {
        cout << "==============================================\n";
        cout << m110a::version_header() << "\n";
        cout << "==============================================\n";
        cout << m110a::build_info() << "\n";
        cout << "Test: Brain M110A Exhaustive\n";
        cout << "Brain Core: Charles Brain (G4GUO) & Steve Hajducek (N2CKH)\n";
        cout << "Wrapper: brain_wrapper.h (48kHz resampling)\n";
        cout << "Modes: " << modes.size() << " | Channels: " << channels.size() << "\n";
        if (duration_sec > 0) cout << "Duration: " << duration_sec << "s\n";
        cout << "\n";
        
        // Sanity check - simple loopback with 600S (most reliable mode)
        // Note: Brain discards first 26 RX bytes as interleaver startup garbage,
        // so we need a message long enough to survive that
        cout << "Sanity check: 600S encode/decode loopback... " << flush;
        try {
            string sanity_str = "THE QUICK BROWN FOX JUMPS";
            vector<uint8_t> sanity_msg(sanity_str.begin(), sanity_str.end());
            brain::Modem tx_check;
            auto pcm_check = tx_check.encode_48k(sanity_msg, brain::Mode::M600S);
            cout << "TX=" << pcm_check.size() << " samples, ";
            
            brain::Modem rx_check;
            auto dec_check = rx_check.decode_48k(pcm_check);
            cout << "RX=" << dec_check.size() << " bytes";
            
            if (dec_check.size() >= 4 && 
                dec_check[0] == 'T' && dec_check[1] == 'E' && 
                dec_check[2] == 'S' && dec_check[3] == 'T') {
                cout << " [PASS]\n";
            } else {
                cout << " [FAIL: got '";
                for (size_t i = 0; i < min(dec_check.size(), (size_t)10); i++) {
                    if (dec_check[i] >= 32 && dec_check[i] < 127) cout << (char)dec_check[i];
                    else cout << "\\x" << hex << (int)dec_check[i] << dec;
                }
                cout << "']\n";
            }
        } catch (const exception& e) {
            cout << " [ERROR: " << e.what() << "]\n";
        }
        cout << "\n";
    }
    
    auto start_time = steady_clock::now();
    steady_clock::time_point end_time;
    bool use_duration = duration_sec > 0;
    if (use_duration) {
        end_time = start_time + seconds(duration_sec);
    }
    
    int iteration = 0;
    bool should_stop = false;
    
    while (!should_stop) {
        iteration++;
        
        // Check termination
        if (use_duration) {
            if (steady_clock::now() >= end_time) break;
        } else {
            if (iteration > 1) break;  // Single iteration if no duration
        }
        
        for (const auto& mode : modes) {
            for (const auto& cond : channels) {
                // Check time
                if (use_duration && steady_clock::now() >= end_time) {
                    should_stop = true;
                    break;
                }
                
                auto now = steady_clock::now();
                int elapsed = (int)duration_cast<seconds>(now - start_time).count();
                
                // Run test
                bool passed = false;
                double ber = 1.0;
                string error;
                int decoded_bytes = 0;
                
                try {
                    // Encode (send full message with padding)
                    brain::Modem tx;
                    auto pcm = tx.encode_48k(test_data, mode.mode);
                    
                    // Apply channel
                    auto noisy = channel.apply(pcm, cond);
                    
                    // Decode (new modem instance - Brain has global state issues)
                    brain::Modem rx;
                    auto decoded = rx.decode_48k(noisy);
                    
                    decoded_bytes = (int)decoded.size();
                    
                    // Compare against expected_data (without padding)
                    // Brain discards first ~26-32 bytes, so we should get approximately expected_data
                    int bit_errors = calc_bit_errors(expected_data, decoded);
                    ber = expected_data.size() > 0 ? (double)bit_errors / (expected_data.size() * 8) : 1.0;
                    passed = (bit_errors == 0);
                    
                } catch (const exception& e) {
                    error = e.what();
                } catch (...) {
                    error = "Unknown error";
                }
                
                // Record stats
                total_tests++;
                if (passed) total_passed++;
                mode_stats[mode.name].record(passed, ber);
                channel_stats[cond.name].record(passed, ber);
                
                // Output
                double rate = total_tests > 0 ? 100.0 * total_passed / total_tests : 0.0;
                
                if (json_output) {
                    cout << "{\"type\":\"test\""
                         << ",\"elapsed\":" << elapsed
                         << ",\"mode\":\"" << mode.name << "\""
                         << ",\"channel\":\"" << cond.name << "\""
                         << ",\"tests\":" << total_tests
                         << ",\"passed\":" << total_passed
                         << ",\"rate\":" << fixed << setprecision(1) << rate
                         << ",\"result\":\"" << (passed ? "PASS" : "FAIL") << "\""
                         << ",\"ber\":" << scientific << setprecision(6) << ber
                         << ",\"decoded\":" << decoded_bytes
                         << ",\"expected\":" << expected_data.size()
                         << "}\n";
                } else {
                    cout << "\r[" << elapsed << "s] " << setw(6) << mode.name 
                         << " + " << setw(12) << left << cond.name << right
                         << " | " << (passed ? "PASS" : "FAIL")
                         << " | Tests: " << total_tests
                         << " | Rate: " << fixed << setprecision(1) << rate << "%   " << flush;
                }
            }
            if (should_stop) break;
        }
    }
    
    auto total_elapsed = (int)duration_cast<seconds>(steady_clock::now() - start_time).count();
    
    // Calculate rating
    double final_rate = total_tests > 0 ? 100.0 * total_passed / total_tests : 0.0;
    string rating;
    if (final_rate >= 95.0) rating = "EXCELLENT";
    else if (final_rate >= 80.0) rating = "GOOD";
    else if (final_rate >= 60.0) rating = "FAIR";
    else rating = "NEEDS WORK";
    
    // Output final stats
    if (json_output) {
        // Mode stats
        for (const auto& [name, stats] : mode_stats) {
            cout << "{\"type\":\"mode_stats\""
                 << ",\"mode\":\"" << name << "\""
                 << ",\"passed\":" << stats.passed
                 << ",\"failed\":" << stats.failed
                 << ",\"total\":" << stats.total
                 << ",\"rate\":" << fixed << setprecision(1) << stats.pass_rate()
                 << ",\"avg_ber\":" << scientific << setprecision(6) << stats.avg_ber()
                 << "}\n";
        }
        
        // Channel stats
        for (const auto& [name, stats] : channel_stats) {
            cout << "{\"type\":\"channel_stats\""
                 << ",\"channel\":\"" << name << "\""
                 << ",\"passed\":" << stats.passed
                 << ",\"failed\":" << stats.failed
                 << ",\"total\":" << stats.total
                 << ",\"rate\":" << fixed << setprecision(1) << stats.pass_rate()
                 << ",\"avg_ber\":" << scientific << setprecision(6) << stats.avg_ber()
                 << "}\n";
        }
        
        // Final summary
        double avg_ber = 0.0;
        int ber_count = 0;
        for (const auto& [_, stats] : mode_stats) {
            avg_ber += stats.total_ber;
            ber_count += stats.ber_count;
        }
        if (ber_count > 0) avg_ber /= ber_count;
        
        cout << "{\"type\":\"done\""
             << ",\"duration\":" << total_elapsed
             << ",\"tests\":" << total_tests
             << ",\"passed\":" << total_passed
             << ",\"failed\":" << (total_tests - total_passed)
             << ",\"rate\":" << fixed << setprecision(1) << final_rate
             << ",\"avg_ber\":" << scientific << setprecision(6) << avg_ber
             << ",\"rating\":\"" << rating << "\""
             << "}\n";
    } else {
        cout << "\n\n";
        cout << "==============================================\n";
        cout << "BRAIN EXHAUSTIVE TEST RESULTS\n";
        cout << "==============================================\n";
        cout << "Duration: " << total_elapsed << " seconds\n";
        cout << "Total Tests: " << total_tests << "\n";
        cout << "Passed: " << total_passed << "\n";
        cout << "Failed: " << (total_tests - total_passed) << "\n";
        cout << "Rate: " << fixed << setprecision(1) << final_rate << "%\n";
        cout << "Rating: " << rating << "\n";
        cout << "==============================================\n";
        
        cout << "\nBy Mode:\n";
        for (const auto& [name, stats] : mode_stats) {
            cout << "  " << setw(6) << name << ": " 
                 << stats.passed << "/" << stats.total 
                 << " (" << setprecision(1) << stats.pass_rate() << "%)\n";
        }
        
        cout << "\nBy Channel:\n";
        for (const auto& [name, stats] : channel_stats) {
            cout << "  " << setw(12) << left << name << right << ": "
                 << stats.passed << "/" << stats.total
                 << " (" << setprecision(1) << stats.pass_rate() << "%)\n";
        }
    }
    
    return final_rate >= 80.0 ? 0 : 1;
}
