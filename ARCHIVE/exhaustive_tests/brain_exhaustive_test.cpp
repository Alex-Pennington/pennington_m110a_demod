/**
 * @file brain_exhaustive_test.cpp
 * @brief Exhaustive test suite for Brain (Paul's) M110A modem - Pure JSON Output
 * 
 * Tests Brain modem across all modes, SNR levels, and channel conditions.
 * All output is JSON Lines (JSONL) format for machine consumption.
 */

// Include Brain wrapper FIRST to avoid std::byte conflict
#include "extern/brain_wrapper.h"
#include "json_output.h"

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

using namespace std;
using namespace std::chrono;
using namespace test_output;

// Global JSON output
static JsonOutput out;

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
        
        if (cond.freq_offset_hz > 0.01f) {
            apply_freq_offset(samples, cond.freq_offset_hz, 48000.0f);
        }
        
        if (cond.multipath_delay > 0 && cond.multipath_gain > 0.01f) {
            apply_multipath(samples, cond.multipath_delay, cond.multipath_gain);
        }
        
        if (cond.snr_db < 99.0f) {
            apply_awgn(samples, cond.snr_db);
        }
        
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
        float sig_power = 0.0f;
        for (float s : samples) sig_power += s * s;
        sig_power /= samples.size();
        
        float snr_linear = powf(10.0f, snr_db / 10.0f);
        float noise_power = sig_power / snr_linear;
        float noise_std = sqrtf(noise_power);
        
        for (size_t i = 0; i < samples.size(); i++) {
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

// ============================================================
// Main test runner
// ============================================================

int main(int argc, char* argv[]) {
    // Force unbuffered stdout for real-time JSON streaming
    cout.setf(ios::unitbuf);
    setvbuf(stdout, NULL, _IONBF, 0);
    
    // Parse arguments
    int duration_sec = 0;
    string mode_filter;
    vector<string> mode_list;
    
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_sec = stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_filter = argv[++i];
            for (auto& c : mode_filter) c = toupper(c);
        } else if (arg == "--modes" && i + 1 < argc) {
            string list_str = argv[++i];
            stringstream ss(list_str);
            string mode;
            while (getline(ss, mode, ',')) {
                for (auto& c : mode) c = toupper(c);
                if (!mode.empty()) mode_list.push_back(mode);
            }
        } else if (arg == "--help" || arg == "-h") {
            // Help to stderr so it doesn't pollute JSON
            cerr << "Brain M110A Exhaustive Test - Pure JSON Output\n\n"
                 << "Usage: " << argv[0] << " [options]\n\n"
                 << "Options:\n"
                 << "  --duration N    Run for N seconds\n"
                 << "  --mode MODE     Test specific mode (e.g., 600S)\n"
                 << "  --modes LIST    Comma-separated modes (e.g., 600S,600L)\n"
                 << "  --help          Show this help\n\n"
                 << "Output: Pure JSON Lines (JSONL) to stdout\n";
            return 0;
        }
    }
    
    // Build mode list
    vector<ModeInfo> modes;
    
    if (!mode_list.empty()) {
        for (int i = 0; i < NUM_MODES; i++) {
            for (const auto& want : mode_list) {
                if (want == ALL_MODES[i].name) {
                    modes.push_back(ALL_MODES[i]);
                    break;
                }
            }
        }
    } else if (!mode_filter.empty()) {
        for (int i = 0; i < NUM_MODES; i++) {
            if (mode_filter == ALL_MODES[i].name) {
                modes.push_back(ALL_MODES[i]);
            }
        }
    } else {
        for (int i = 0; i < NUM_MODES; i++) {
            modes.push_back(ALL_MODES[i]);
        }
    }
    
    if (modes.empty()) {
        out.error("No modes match filter");
        out.end(1);
        return 1;
    }
    
    auto channels = get_standard_channels();
    
    // Emit start event
    out.start("brain_exhaustive_test", "Brain Direct API", "", "", mode_filter, "exhaustive");
    out.config(42, false);
    
    // Test message
    string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    vector<uint8_t> expected_data = test_data;
    
    ChannelSimulator channel(42);
    
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
        
        if (use_duration) {
            if (steady_clock::now() >= end_time) break;
        } else {
            if (iteration > 1) break;
        }
        
        for (const auto& mode : modes) {
            for (const auto& cond : channels) {
                if (use_duration && steady_clock::now() >= end_time) {
                    should_stop = true;
                    break;
                }
                
                bool passed = false;
                double ber = 1.0;
                
                try {
                    brain::Modem tx;
                    auto pcm = tx.encode_48k(test_data, mode.mode);
                    auto noisy = channel.apply(pcm, cond);
                    
                    brain::Modem rx;
                    auto decoded = rx.decode_48k(noisy);
                    
                    int bit_errors = calc_bit_errors(expected_data, decoded);
                    ber = expected_data.size() > 0 ? (double)bit_errors / (expected_data.size() * 8) : 1.0;
                    passed = (bit_errors == 0);
                    
                } catch (const exception& e) {
                    out.warning(string("Test exception: ") + e.what());
                } catch (...) {
                    out.warning("Unknown test exception");
                }
                
                out.test(mode.name, cond.name, passed, ber, iteration);
            }
            if (should_stop) break;
        }
    }
    
    out.end(0);
    return 0;
}
