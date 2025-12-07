/**
 * @file test_api.cpp
 * @brief Test M110A Modem API
 */

#include "api/modem.h"
#include <iostream>
#include <iomanip>

using namespace m110a::api;

void test_types() {
    std::cout << "=== Test Types ===\n";
    
    // Test Result<T>
    Result<int> r1(42);
    Result<int> r2(ErrorCode::INTERNAL_ERROR, "test error");
    
    std::cout << "r1.ok() = " << r1.ok() << " value = " << r1.value() << "\n";
    std::cout << "r2.ok() = " << r2.ok() << " error = " << r2.error().message << "\n";
    
    // Test Result<void>
    Result<void> v1;
    Result<void> v2(ErrorCode::INVALID_MODE);
    
    std::cout << "v1.ok() = " << v1.ok() << "\n";
    std::cout << "v2.ok() = " << v2.ok() << " error = " << v2.error().message << "\n";
    
    // Test mode functions
    std::cout << "mode_name(M2400_SHORT) = " << mode_name(Mode::M2400_SHORT) << "\n";
    std::cout << "mode_bitrate(M2400_SHORT) = " << mode_bitrate(Mode::M2400_SHORT) << "\n";
    
    std::cout << "✓ Types OK\n\n";
}

void test_config() {
    std::cout << "=== Test Config ===\n";
    
    // Test TxConfig
    TxConfig tx_cfg;
    tx_cfg.mode = Mode::M2400_SHORT;
    auto tx_valid = tx_cfg.validate();
    std::cout << "TxConfig valid: " << tx_valid.ok() << "\n";
    
    // Test invalid
    tx_cfg.mode = Mode::AUTO;
    auto tx_invalid = tx_cfg.validate();
    std::cout << "TxConfig AUTO invalid: " << !tx_invalid.ok() << "\n";
    
    // Test RxConfig
    RxConfig rx_cfg;
    auto rx_valid = rx_cfg.validate();
    std::cout << "RxConfig valid: " << rx_valid.ok() << "\n";
    
    // Test builder
    auto built = TxConfigBuilder()
        .mode(Mode::M1200_SHORT)
        .sample_rate(48000.0f)
        .amplitude(0.9f)
        .build();
    std::cout << "Builder OK: " << built.ok() << "\n";
    
    std::cout << "✓ Config OK\n\n";
}

void test_loopback() {
    std::cout << "=== Test Loopback ===\n";
    
    // Test data
    std::string message = "Hello, M110A API!";
    std::cout << "Message: " << message << "\n";
    
    // Encode
    auto encode_result = encode(message, Mode::M2400_SHORT);
    if (!encode_result.ok()) {
        std::cout << "✗ Encode failed: " << encode_result.error().message << "\n";
        return;
    }
    
    std::cout << "Encoded: " << encode_result.value().size() << " samples\n";
    
    // Decode
    auto decode_result = decode(encode_result.value());
    
    std::cout << "Decode success: " << decode_result.success << "\n";
    if (decode_result.success) {
        std::cout << "Decoded: " << decode_result.as_string() << "\n";
        std::cout << "Mode: " << mode_name(decode_result.mode) << "\n";
        std::cout << "SNR: " << std::fixed << std::setprecision(1) 
                  << decode_result.snr_db << " dB\n";
        
        if (decode_result.as_string().find(message) != std::string::npos) {
            std::cout << "✓ Loopback OK\n";
        } else {
            std::cout << "✗ Message mismatch\n";
        }
    } else {
        std::cout << "✗ Decode failed\n";
    }
    std::cout << "\n";
}

void test_file_io() {
    std::cout << "=== Test File I/O ===\n";
    
    // Generate test signal
    auto samples = encode("Test", Mode::M2400_SHORT);
    if (!samples.ok()) {
        std::cout << "✗ Encode failed\n";
        return;
    }
    
    // Save PCM
    auto save_result = save_pcm("/tmp/test_api.pcm", samples.value());
    std::cout << "Save PCM: " << (save_result.ok() ? "OK" : "FAILED") << "\n";
    
    // Load PCM
    auto load_result = load_pcm("/tmp/test_api.pcm");
    std::cout << "Load PCM: " << (load_result.ok() ? "OK" : "FAILED");
    if (load_result.ok()) {
        std::cout << " (" << load_result.value().size() << " samples)";
    }
    std::cout << "\n";
    
    // Save WAV
    save_result = save_wav("/tmp/test_api.wav", samples.value());
    std::cout << "Save WAV: " << (save_result.ok() ? "OK" : "FAILED") << "\n";
    
    // Load WAV
    float sr;
    load_result = load_wav("/tmp/test_api.wav", &sr);
    std::cout << "Load WAV: " << (load_result.ok() ? "OK" : "FAILED");
    if (load_result.ok()) {
        std::cout << " (" << load_result.value().size() << " samples @ " << sr << " Hz)";
    }
    std::cout << "\n";
    
    std::cout << "✓ File I/O OK\n\n";
}

void test_version() {
    std::cout << "=== Test Version ===\n";
    std::cout << "API Version: " << version() << "\n";
    std::cout << "✓ Version OK\n\n";
}

int main() {
    std::cout << "M110A Modem API Test\n";
    std::cout << "====================\n\n";
    
    test_version();
    test_types();
    test_config();
    test_file_io();
    test_loopback();
    
    std::cout << "All tests complete.\n";
    return 0;
}
