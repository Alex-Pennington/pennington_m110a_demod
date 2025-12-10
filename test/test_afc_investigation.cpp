/**
 * AFC Investigation Test
 * 
 * Tests to understand WHY the AFC fails at >2 Hz offset
 * and what the developer claimed vs actual behavior
 */

#include "../api/modem.h"
#include "../src/m110a/brain_decoder.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace m110a;

// Apply frequency offset to PCM samples
void apply_freq_offset(std::vector<float>& samples, float offset_hz, float sample_rate = 48000.0f) {
    float phase = 0.0f;
    float phase_inc = 2.0f * 3.14159265f * offset_hz / sample_rate;
    for (float& s : samples) {
        s *= std::cos(phase);
        phase += phase_inc;
        if (phase > 6.28318f) phase -= 6.28318f;
    }
}

// Test AFC with different configurations
void test_afc_performance() {
    std::cout << "=== AFC Performance Investigation ===\n\n";
    
    // Create test message
    std::vector<uint8_t> test_data(64);
    for (size_t i = 0; i < test_data.size(); i++) {
        test_data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    // Test different frequency offsets
    std::vector<float> offsets = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.0f, 10.0f};
    
    std::cout << "Testing 600S mode with clean signal (no noise, no multipath)\n";
    std::cout << "AFC Search Range: Â±10 Hz, Step: 1 Hz (default)\n\n";
    std::cout << std::left << std::setw(12) << "Freq Offset"
              << std::setw(15) << "Detected?"
              << std::setw(15) << "Detected Freq"
              << std::setw(12) << "Correlation"
              << std::setw(10) << "BER"
              << "Status\n";
    std::cout << std::string(80, '-') << "\n";
    
    for (float offset : offsets) {
        // Encode
        auto encode_result = api::encode(test_data, api::Mode::M600_SHORT);
        if (!encode_result) continue;
        std::vector<float> pcm = encode_result.value();
        
        // Apply frequency offset
        if (std::abs(offset) > 0.01f) {
            apply_freq_offset(pcm, offset);
        }
        
        // Decode with known mode
        api::RxConfig cfg;
        cfg.mode = api::Mode::M600_SHORT;  // Known mode
        cfg.equalizer = api::Equalizer::DFE;
        cfg.phase_tracking = true;
        
        auto decode_result = api::decode(pcm, cfg);
        
        // Calculate BER
        int errors = 0;
        int bits = std::min(test_data.size(), decode_result.data.size()) * 8;
        for (size_t i = 0; i < std::min(test_data.size(), decode_result.data.size()); i++) {
            uint8_t xor_val = test_data[i] ^ decode_result.data[i];
            for (int b = 0; b < 8; b++) {
                if (xor_val & (1 << b)) errors++;
            }
        }
        float ber = (bits > 0) ? static_cast<float>(errors) / bits : 1.0f;
        
        std::string status;
        if (ber < 0.01f) status = "âœ“ PASS";
        else if (ber < 0.1f) status = "âš  MARGINAL";
        else status = "âœ— FAIL";
        
        std::cout << std::left << std::setw(12) << (std::to_string(offset) + " Hz")
                  << std::setw(15) << (decode_result.success ? "YES" : "NO")
                  << std::setw(15) << (std::to_string(decode_result.freq_offset_hz) + " Hz")
                  << std::setw(12) << std::fixed << std::setprecision(3) << decode_result.snr_db
                  << std::setw(10) << std::fixed << std::setprecision(4) << ber
                  << status << "\n";
    }
    
    std::cout << "\n=== Direct BrainDecoder Test ===\n";
    std::cout << "Testing decoder's frequency search directly\n\n";
    
    std::cout << std::left << std::setw(12) << "Freq Offset"
              << std::setw(18) << "Detected Offset"
              << std::setw(15) << "Correlation"
              << std::setw(15) << "Mode Detected"
              << "Status\n";
    std::cout << std::string(80, '-') << "\n";
    
    for (float offset : offsets) {
        // Encode
        auto encode_result = api::encode(test_data, api::Mode::M600_SHORT);
        if (!encode_result) continue;
        std::vector<float> pcm = encode_result.value();
        
        // Apply frequency offset
        if (std::abs(offset) > 0.01f) {
            apply_freq_offset(pcm, offset);
        }
        
        // Test with BrainDecoder directly
        BrainDecoderConfig decoder_cfg;
        decoder_cfg.sample_rate = 48000.0f;
        decoder_cfg.carrier_freq = 1800.0f;
        decoder_cfg.baud_rate = 2400.0f;
        decoder_cfg.freq_search_range = 10.0f;  // Â±10 Hz
        decoder_cfg.freq_search_step = 1.0f;    // 1 Hz steps
        decoder_cfg.unknown_data_len = 32;  // 600S frame structure
        decoder_cfg.known_data_len = 16;
        
        BrainDecoder decoder(decoder_cfg);
        auto result = decoder.decode(pcm);
        
        std::string status;
        if (result.preamble_found && result.mode_name != "UNKNOWN") {
            float freq_error = std::abs(result.freq_offset_hz - offset);
            if (freq_error < 0.5f) status = "âœ“ CORRECT";
            else status = "âš  WRONG FREQ";
        } else {
            status = "âœ— NO LOCK";
        }
        
        std::cout << std::left << std::setw(12) << (std::to_string(offset) + " Hz")
                  << std::setw(18) << (std::to_string(result.freq_offset_hz) + " Hz")
                  << std::setw(15) << std::fixed << std::setprecision(3) << result.correlation
                  << std::setw(15) << result.mode_name
                  << status << "\n";
    }
    
    std::cout << "\n=== Testing Different AFC Search Parameters ===\n";
    std::cout << "Frequency Offset: 5 Hz (known failure case)\n\n";
    
    struct TestConfig {
        float search_range;
        float search_step;
        std::string description;
    };
    
    std::vector<TestConfig> configs = {
        {10.0f, 1.0f, "Default (Â±10 Hz, 1 Hz step)"},
        {10.0f, 0.5f, "Finer step (Â±10 Hz, 0.5 Hz step)"},
        {20.0f, 1.0f, "Wider range (Â±20 Hz, 1 Hz step)"},
        {10.0f, 0.25f, "Very fine (Â±10 Hz, 0.25 Hz step)"},
        {5.0f, 0.5f, "Narrow/fine (Â±5 Hz, 0.5 Hz step)"}
    };
    
    // Encode once with 5 Hz offset
    auto encode_result = api::encode(test_data, api::Mode::M600_SHORT);
    if (encode_result) {
        std::vector<float> pcm = encode_result.value();
        apply_freq_offset(pcm, 5.0f);
        
        std::cout << std::left << std::setw(40) << "Configuration"
                  << std::setw(18) << "Detected Offset"
                  << std::setw(15) << "Correlation"
                  << "Status\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& config : configs) {
            BrainDecoderConfig decoder_cfg;
            decoder_cfg.sample_rate = 48000.0f;
            decoder_cfg.carrier_freq = 1800.0f;
            decoder_cfg.baud_rate = 2400.0f;
            decoder_cfg.freq_search_range = config.search_range;
            decoder_cfg.freq_search_step = config.search_step;
            decoder_cfg.unknown_data_len = 32;
            decoder_cfg.known_data_len = 16;
            
            BrainDecoder decoder(decoder_cfg);
            auto result = decoder.decode(pcm);
            
            float freq_error = std::abs(result.freq_offset_hz - 5.0f);
            std::string status;
            if (!result.preamble_found) status = "âœ— NO PREAMBLE";
            else if (freq_error < 0.5f) status = "âœ“ CORRECT FREQ";
            else status = "âš  WRONG FREQ (err=" + std::to_string(freq_error) + "Hz)";
            
            std::cout << std::left << std::setw(40) << config.description
                      << std::setw(18) << (std::to_string(result.freq_offset_hz) + " Hz")
                      << std::setw(15) << std::fixed << std::setprecision(3) << result.correlation
                      << status << "\n";
        }
    }
}

int main() {
    std::cout << "\n";
    std::cout << "â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—\n";
    std::cout << "â•‘           AFC (Automatic Frequency Control) Investigation      â•‘\n";
    std::cout << "â•‘                                                                â•‘\n";
    std::cout << "â•‘  Testing why AFC fails at >2-3 Hz frequency offset            â•‘\n";
    std::cout << "â•‘  when developer claimed it works                               â•‘\n";
    std::cout << "â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•\n";
    std::cout << "\n";
    
    test_afc_performance();
    
    std::cout << "\n=== ANALYSIS ===\n";
    std::cout << "1. Check if preamble is detected at all frequencies\n";
    std::cout << "2. Check if detected frequency matches actual offset\n";
    std::cout << "3. Check if correlation metric changes with offset\n";
    std::cout << "4. Test if finer search steps or wider range helps\n";
    std::cout << "\nThis will reveal WHERE in the AFC chain it breaks down.\n";
    
    return 0;
}
