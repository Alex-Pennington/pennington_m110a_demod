/**
 * @file progressive_runner.h
 * @brief Progressive test runner - finds mode performance limits
 * 
 * Progressive tests determine the minimum SNR, maximum frequency offset,
 * and maximum multipath delay each mode can handle.
 */

#ifndef PROGRESSIVE_RUNNER_H
#define PROGRESSIVE_RUNNER_H

#include "cli.h"
#include "output.h"
#include "../test_framework.h"

#include <chrono>
#include <map>

namespace exhaustive {

using namespace test_framework;
using namespace std::chrono;

// ============================================================
// Progressive Test Runner
// ============================================================

class ProgressiveRunner {
public:
    ProgressiveRunner(ITestBackend& backend, IOutput& output, const Config& cfg)
        : backend_(backend), output_(output), cfg_(cfg) {}
    
    std::map<std::string, ProgressiveResult> run() {
        std::map<std::string, ProgressiveResult> results;
        
        auto all_modes = get_all_modes();
        auto modes = filter_modes(all_modes, cfg_.mode_filter);
        
        if (modes.empty()) {
            output_.on_error("No modes match filter");
            return results;
        }
        
        std::vector<uint8_t> test_data(cfg_.test_message.begin(), cfg_.test_message.end());
        
        output_.on_start(
            backend_.backend_name(),
            cfg_.use_auto_detect ? "AUTO" : "KNOWN",
            cfg_.equalizers,
            0,  // Progressive mode doesn't use iterations
            false,
            cfg_.mode_filter
        );
        
        output_.on_info("Progressive mode: finding performance limits...");
        if (cfg_.prog_snr) output_.on_info("  - SNR sensitivity");
        if (cfg_.prog_freq) output_.on_info("  - Frequency offset tolerance");
        if (cfg_.prog_multipath) output_.on_info("  - Multipath delay tolerance");
        output_.on_info("");
        
        auto start_time = steady_clock::now();
        
        for (const auto& eq : cfg_.equalizers) {
            backend_.set_equalizer(eq);
            output_.on_info("*** Testing with Equalizer: " + eq + " ***");
            
            for (const auto& mode : modes) {
                ProgressiveResult result;
                result.mode_name = (cfg_.equalizers.size() > 1) ? 
                    eq + ":" + mode.name : mode.name;
                
                // SNR test: find minimum SNR that still decodes
                if (cfg_.prog_snr) {
                    result.snr_limit_db = find_snr_limit(mode, test_data);
                    result.snr_tested = true;
                }
                
                // Frequency offset test: find maximum offset that still decodes
                if (cfg_.prog_freq) {
                    result.freq_offset_limit_hz = find_freq_limit(mode, test_data);
                    result.freq_tested = true;
                }
                
                // Multipath test: find maximum delay that still decodes
                if (cfg_.prog_multipath) {
                    result.multipath_limit_samples = find_multipath_limit(mode, test_data);
                    result.multipath_tested = true;
                }
                
                results[result.mode_name] = result;
                
                output_.on_progressive_result(
                    result.mode_name,
                    result.snr_limit_db,
                    result.freq_offset_limit_hz,
                    result.multipath_limit_samples
                );
            }
        }
        
        auto elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
        output_.on_info("\nProgressive tests completed in " + std::to_string(elapsed) + " seconds");
        
        return results;
    }

private:
    // Find minimum SNR where mode still decodes
    float find_snr_limit(const ModeInfo& mode, const std::vector<uint8_t>& test_data) {
        // Test SNRs from high to low
        std::vector<float> snr_values = {30, 25, 20, 18, 15, 12, 10, 8, 6, 4, 2, 0, -2, -4};
        
        float min_snr = 30.0f;  // Default to "failed at max SNR"
        
        for (float snr : snr_values) {
            ChannelCondition channel;
            channel.name = "snr_test";
            channel.snr_db = snr;
            
            double ber;
            bool passed = backend_.run_test(mode, channel, test_data, ber);
            
            if (passed && ber < 0.001) {  // Require very low BER
                min_snr = snr;
            } else {
                break;  // Failed, stop searching
            }
        }
        
        return min_snr;
    }
    
    // Find maximum frequency offset where mode still decodes
    float find_freq_limit(const ModeInfo& mode, const std::vector<uint8_t>& test_data) {
        // Test frequency offsets from low to high
        std::vector<float> freq_values = {0, 0.5, 1, 2, 3, 4, 5, 7, 10, 15, 20, 25, 30, 40, 50};
        
        float max_freq = 0.0f;
        
        for (float freq : freq_values) {
            ChannelCondition channel;
            channel.name = "freq_test";
            channel.snr_db = 30.0f;  // Clean for freq test
            channel.freq_offset_hz = freq;
            
            double ber;
            bool passed = backend_.run_test(mode, channel, test_data, ber);
            
            if (passed && ber < 0.01) {
                max_freq = freq;
            } else {
                break;
            }
        }
        
        return max_freq;
    }
    
    // Find maximum multipath delay where mode still decodes
    int find_multipath_limit(const ModeInfo& mode, const std::vector<uint8_t>& test_data) {
        // Test multipath delays from none to severe
        std::vector<int> delay_values = {0, 6, 12, 18, 24, 30, 36, 42, 48, 60, 72, 84, 96};
        
        int max_delay = 0;
        
        for (int delay : delay_values) {
            ChannelCondition channel;
            channel.name = "mp_test";
            channel.snr_db = 30.0f;  // Clean SNR for multipath test
            channel.multipath_delay_samples = delay;
            channel.multipath_gain = 0.5f;
            
            double ber;
            bool passed = backend_.run_test(mode, channel, test_data, ber);
            
            if (passed && ber < 0.01) {
                max_delay = delay;
            } else {
                break;
            }
        }
        
        return max_delay;
    }
    
    ITestBackend& backend_;
    IOutput& output_;
    const Config& cfg_;
};

} // namespace exhaustive

#endif // PROGRESSIVE_RUNNER_H
