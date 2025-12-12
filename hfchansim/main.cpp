/**
 * @file main.cpp
 * @brief HF Channel Simulator CLI
 * 
 * Standalone tool to apply HF channel impairments to PCM files.
 * Uses the existing m110a channel models from src/channel/
 * 
 * Usage:
 *   hfchansim --ref 600S --preset moderate
 *   hfchansim input.pcm output.pcm [options]
 * 
 * Copyright (c) 2025 Phoenix Nest LLC
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <string>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <map>

// Use existing channel implementations
#include "../src/channel/awgn.h"
#include "../src/channel/watterson.h"
#include "../src/channel/multipath.h"
#include "../api/version.h"

namespace fs = std::filesystem;

// ============================================================
// Reference PCM Mode Mapping
// ============================================================

struct RefMode {
    std::string mode_id;      // e.g., "600S"
    std::string mode_name;    // e.g., "600 BPS SHORT"
    int bps;
    std::string interleave;
    std::string modulation;
    int symbol_rate;
};

const std::map<std::string, RefMode> REF_MODES = {
    {"75S",   {"75S",   "75 BPS SHORT",   75,   "SHORT", "BPSK", 75}},
    {"75L",   {"75L",   "75 BPS LONG",    75,   "LONG",  "BPSK", 75}},
    {"150S",  {"150S",  "150 BPS SHORT",  150,  "SHORT", "QPSK", 150}},
    {"150L",  {"150L",  "150 BPS LONG",   150,  "LONG",  "QPSK", 150}},
    {"300S",  {"300S",  "300 BPS SHORT",  300,  "SHORT", "QPSK", 300}},
    {"300L",  {"300L",  "300 BPS LONG",   300,  "LONG",  "QPSK", 300}},
    {"600S",  {"600S",  "600 BPS SHORT",  600,  "SHORT", "8-PSK", 2400}},
    {"600L",  {"600L",  "600 BPS LONG",   600,  "LONG",  "8-PSK", 2400}},
    {"1200S", {"1200S", "1200 BPS SHORT", 1200, "SHORT", "8-PSK", 2400}},
    {"1200L", {"1200L", "1200 BPS LONG",  1200, "LONG",  "8-PSK", 2400}},
    {"2400S", {"2400S", "2400 BPS SHORT", 2400, "SHORT", "8-PSK", 2400}},
    {"2400L", {"2400L", "2400 BPS LONG",  2400, "LONG",  "8-PSK", 2400}},
};

// ============================================================
// Utility Functions
// ============================================================

std::string get_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string get_iso_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

fs::path get_executable_dir() {
    // This is a simplification - works for most cases
    return fs::current_path();
}

fs::path find_reference_pcm_dir() {
    // Try relative paths from typical execution locations
    std::vector<fs::path> search_paths = {
        "../examples/refrence_pcm",      // From release/bin/
        "examples/refrence_pcm",         // From release/
        "release/examples/refrence_pcm", // From project root
        "refrence_pcm",                  // Direct
    };
    
    for (const auto& p : search_paths) {
        if (fs::exists(p) && fs::is_directory(p)) {
            return fs::canonical(p);
        }
    }
    
    return "";
}

fs::path find_reference_pcm(const std::string& mode_id, const fs::path& ref_dir) {
    if (ref_dir.empty()) return "";
    
    // Look for tx_<mode>_*.pcm pattern
    std::string prefix = "tx_" + mode_id + "_";
    
    for (const auto& entry : fs::directory_iterator(ref_dir)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.find(prefix) == 0 && 
                filename.size() >= 4 && filename.substr(filename.size() - 4) == ".pcm") {
                return entry.path();
            }
        }
    }
    
    return "";
}

fs::path ensure_output_dir() {
    fs::path out_dir = "hfchansim_out";
    if (!fs::exists(out_dir)) {
        fs::create_directories(out_dir);
    }
    return out_dir;
}

// ============================================================
// PCM File I/O (headerless 16-bit signed mono)
// ============================================================

std::vector<float> read_pcm(const std::string& filename, float sample_rate) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open input file: " << filename << std::endl;
        return {};
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    
    size_t num_samples = size / sizeof(int16_t);
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

bool write_pcm(const std::string& filename, const std::vector<float>& samples) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open output file: " << filename << std::endl;
        return false;
    }
    
    std::vector<int16_t> raw(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        raw[i] = static_cast<int16_t>(s * 32767.0f);
    }
    
    file.write(reinterpret_cast<char*>(raw.data()), raw.size() * sizeof(int16_t));
    return true;
}

// ============================================================
// Metadata JSON Generation
// ============================================================

struct ChannelSettings {
    std::string model;
    float snr_db;
    float freq_offset_hz;
    float doppler_hz;
    float delay_ms;
    float path1_gain_db;
    float path2_gain_db;
    std::string preset;
    uint32_t seed;
};

void write_metadata_json(
    const std::string& json_path,
    const std::string& input_path,
    const std::string& output_path,
    const RefMode* ref_mode,
    float sample_rate,
    size_t sample_count,
    const ChannelSettings& settings)
{
    std::ofstream f(json_path);
    if (!f) {
        std::cerr << "Warning: Could not write metadata to " << json_path << std::endl;
        return;
    }
    
    float duration_sec = static_cast<float>(sample_count) / sample_rate;
    
    f << "{\n";
    f << "  \"toolInfo\": {\n";
    f << "    \"name\": \"hfchansim\",\n";
    f << "    \"version\": \"" << m110a::version() << "\",\n";
    f << "    \"build\": " << m110a::BUILD_NUMBER << ",\n";
    f << "    \"timestamp\": \"" << get_iso_timestamp() << "\"\n";
    f << "  },\n";
    
    f << "  \"inputFile\": {\n";
    f << "    \"path\": \"" << input_path << "\",\n";
    f << "    \"sampleRate\": " << static_cast<int>(sample_rate) << ",\n";
    f << "    \"sampleCount\": " << sample_count << ",\n";
    f << "    \"bitsPerSample\": 16,\n";
    f << "    \"channels\": 1,\n";
    f << "    \"durationSeconds\": " << std::fixed << std::setprecision(3) << duration_sec << "\n";
    f << "  },\n";
    
    if (ref_mode) {
        f << "  \"sourceMode\": {\n";
        f << "    \"id\": \"" << ref_mode->mode_id << "\",\n";
        f << "    \"name\": \"" << ref_mode->mode_name << "\",\n";
        f << "    \"bitsPerSecond\": " << ref_mode->bps << ",\n";
        f << "    \"interleave\": \"" << ref_mode->interleave << "\",\n";
        f << "    \"modulation\": \"" << ref_mode->modulation << "\",\n";
        f << "    \"symbolRate\": " << ref_mode->symbol_rate << "\n";
        f << "  },\n";
    }
    
    f << "  \"outputFile\": {\n";
    f << "    \"path\": \"" << output_path << "\",\n";
    f << "    \"sampleRate\": " << static_cast<int>(sample_rate) << ",\n";
    f << "    \"sampleCount\": " << sample_count << ",\n";
    f << "    \"bitsPerSample\": 16,\n";
    f << "    \"channels\": 1,\n";
    f << "    \"durationSeconds\": " << std::fixed << std::setprecision(3) << duration_sec << "\n";
    f << "  },\n";
    
    f << "  \"channelSettings\": {\n";
    f << "    \"model\": \"" << settings.model << "\",\n";
    if (!settings.preset.empty()) {
        f << "    \"preset\": \"" << settings.preset << "\",\n";
    }
    f << "    \"snr_dB\": " << (std::isfinite(settings.snr_db) ? std::to_string(settings.snr_db) : "null") << ",\n";
    f << "    \"frequencyOffset_Hz\": " << std::fixed << std::setprecision(2) << settings.freq_offset_hz << ",\n";
    
    if (settings.model == "watterson") {
        f << "    \"dopplerSpread_Hz\": " << std::fixed << std::setprecision(2) << settings.doppler_hz << ",\n";
        f << "    \"differentialDelay_ms\": " << std::fixed << std::setprecision(2) << settings.delay_ms << ",\n";
        f << "    \"path1Gain_dB\": " << std::fixed << std::setprecision(1) << settings.path1_gain_db << ",\n";
        f << "    \"path2Gain_dB\": " << std::fixed << std::setprecision(1) << settings.path2_gain_db << ",\n";
    }
    
    f << "    \"seed\": " << settings.seed << "\n";
    f << "  }\n";
    f << "}\n";
    
    f.close();
}

// ============================================================
// CLI Configuration
// ============================================================

struct Config {
    std::string input_file;
    std::string output_file;
    std::string ref_mode;        // e.g., "600S"
    bool list_refs = false;
    
    // Channel model selection
    enum Model { AWGN_ONLY, WATTERSON, MULTIPATH } model = AWGN_ONLY;
    
    // AWGN settings
    float snr_db = INFINITY;
    
    // Watterson settings
    float doppler_hz = 1.0f;
    float delay_ms = 1.0f;
    float path1_gain_db = 0.0f;
    float path2_gain_db = 0.0f;
    
    // Frequency offset (applied separately)
    float freq_offset_hz = 0.0f;
    
    // General
    float sample_rate = 48000.0f;
    uint32_t seed = 42;
    bool verbose = false;
    
    // Preset name (empty = custom)
    std::string preset;
};

void apply_preset(Config& cfg, const std::string& name) {
    cfg.preset = name;
    
    if (name == "clean" || name == "CLEAN") {
        cfg.model = Config::AWGN_ONLY;
        cfg.snr_db = INFINITY;
        cfg.freq_offset_hz = 0.0f;
    }
    else if (name == "awgn" || name == "AWGN") {
        cfg.model = Config::AWGN_ONLY;
        cfg.snr_db = 15.0f;
    }
    else if (name == "good" || name == "GOOD") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::CCIR_GOOD.doppler_spread_hz;
        cfg.delay_ms = m110a::CCIR_GOOD.delay_ms;
        cfg.path1_gain_db = m110a::CCIR_GOOD.path1_gain_db;
        cfg.path2_gain_db = m110a::CCIR_GOOD.path2_gain_db;
        cfg.snr_db = 20.0f;
    }
    else if (name == "moderate" || name == "MODERATE") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::CCIR_MODERATE.doppler_spread_hz;
        cfg.delay_ms = m110a::CCIR_MODERATE.delay_ms;
        cfg.path1_gain_db = m110a::CCIR_MODERATE.path1_gain_db;
        cfg.path2_gain_db = m110a::CCIR_MODERATE.path2_gain_db;
        cfg.snr_db = 15.0f;
    }
    else if (name == "poor" || name == "POOR") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::CCIR_POOR.doppler_spread_hz;
        cfg.delay_ms = m110a::CCIR_POOR.delay_ms;
        cfg.path1_gain_db = m110a::CCIR_POOR.path1_gain_db;
        cfg.path2_gain_db = m110a::CCIR_POOR.path2_gain_db;
        cfg.snr_db = 10.0f;
    }
    else if (name == "flutter" || name == "FLUTTER") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::CCIR_FLUTTER.doppler_spread_hz;
        cfg.delay_ms = m110a::CCIR_FLUTTER.delay_ms;
        cfg.path1_gain_db = m110a::CCIR_FLUTTER.path1_gain_db;
        cfg.path2_gain_db = m110a::CCIR_FLUTTER.path2_gain_db;
        cfg.snr_db = 12.0f;
    }
    else if (name == "midlat" || name == "MIDLAT") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::MID_LAT_DISTURBED.doppler_spread_hz;
        cfg.delay_ms = m110a::MID_LAT_DISTURBED.delay_ms;
        cfg.path1_gain_db = m110a::MID_LAT_DISTURBED.path1_gain_db;
        cfg.path2_gain_db = m110a::MID_LAT_DISTURBED.path2_gain_db;
        cfg.snr_db = 12.0f;
    }
    else if (name == "highlat" || name == "HIGHLAT") {
        cfg.model = Config::WATTERSON;
        cfg.doppler_hz = m110a::HIGH_LAT_DISTURBED.doppler_spread_hz;
        cfg.delay_ms = m110a::HIGH_LAT_DISTURBED.delay_ms;
        cfg.path1_gain_db = m110a::HIGH_LAT_DISTURBED.path1_gain_db;
        cfg.path2_gain_db = m110a::HIGH_LAT_DISTURBED.path2_gain_db;
        cfg.snr_db = 8.0f;
    }
    else {
        std::cerr << "Warning: Unknown preset '" << name << "'" << std::endl;
    }
}

// ============================================================
// Frequency Offset Application
// ============================================================

void apply_freq_offset(std::vector<float>& samples, float freq_hz, float sample_rate) {
    if (std::abs(freq_hz) < 0.001f) return;
    
    float phase = 0.0f;
    float phase_inc = 2.0f * M_PI * freq_hz / sample_rate;
    
    for (size_t i = 0; i < samples.size(); i++) {
        samples[i] *= std::cos(phase);
        phase += phase_inc;
        if (phase > M_PI) phase -= 2.0f * M_PI;
    }
}

// ============================================================
// Help & List Functions
// ============================================================

void print_version() {
    std::cout << "hfchansim - HF Channel Simulator" << std::endl;
    std::cout << m110a::version_header() << std::endl;
    std::cout << m110a::copyright_notice() << std::endl;
}

void print_usage(const char* prog) {
    std::cout << R"(
HF Channel Simulator - Apply realistic HF channel impairments to PCM files

USAGE:
    )" << prog << R"( --ref <mode> [options]           Use reference PCM input
    )" << prog << R"( <input> <output> [options]       Use custom input/output files
    )" << prog << R"( --list-ref                       List available reference PCMs
    )" << prog << R"( --help                           Show this help

REFERENCE MODE (--ref):
    Use bundled reference PCM files. Output goes to hfchansim_out/ directory.
    
    Modes: 75S, 75L, 150S, 150L, 300S, 300L, 600S, 600L, 1200S, 1200L, 2400S, 2400L
    
    Examples:
      )" << prog << R"( --ref 600S --preset moderate
      )" << prog << R"( --ref 2400L --snr 10 --freq 5

CHANNEL OPTIONS:
    --snr <dB>            Target SNR for AWGN (default: no noise)
    --freq <Hz>           Frequency offset (default: 0)
    
    --model <type>        Channel model: awgn, watterson (default: awgn)
    --doppler <Hz>        Watterson Doppler spread (default: 1.0)
    --delay <ms>          Watterson differential delay (default: 1.0)
    --path1-gain <dB>     Watterson path 1 gain (default: 0)
    --path2-gain <dB>     Watterson path 2 gain (default: 0)
    
    --preset <name>       Use preset channel profile:
                            clean    - No impairments (passthrough)
                            awgn     - Pure AWGN at 15 dB SNR
                            good     - CCIR Good (0.5 Hz Doppler, 0.5 ms delay)
                            moderate - CCIR Moderate (1 Hz Doppler, 1 ms delay)
                            poor     - CCIR Poor (2 Hz Doppler, 2 ms delay)
                            flutter  - CCIR Flutter (10 Hz Doppler)
                            midlat   - Mid-latitude disturbed
                            highlat  - High-latitude disturbed

GENERAL OPTIONS:
    --sample-rate <Hz>    Sample rate for raw PCM (default: 48000)
    --seed <n>            Random seed for reproducibility (default: 42)
    --verbose             Show detailed progress
    --version             Show version information

OUTPUT:
    When using --ref, output files are written to hfchansim_out/ directory:
      - <mode>_<preset>_<timestamp>.pcm      Degraded audio
      - <mode>_<preset>_<timestamp>.json     Metadata with all settings

EXAMPLES:
    # Apply moderate HF channel to 600 BPS SHORT reference
    )" << prog << R"( --ref 600S --preset moderate

    # Custom channel settings
    )" << prog << R"( --ref 2400L --model watterson --doppler 2 --delay 1.5 --snr 12

    # Add just noise and frequency offset
    )" << prog << R"( --ref 1200S --snr 15 --freq 3.5

    # Process custom file
    )" << prog << R"( my_signal.pcm degraded.pcm --preset poor

    # Reproducible results with seed
    )" << prog << R"( --ref 600S --preset moderate --seed 12345

Copyright (c) 2025 Phoenix Nest LLC
)";
}

void list_reference_pcms() {
    fs::path ref_dir = find_reference_pcm_dir();
    
    std::cout << "Available Reference PCM Files\n";
    std::cout << "=============================\n\n";
    
    if (ref_dir.empty()) {
        std::cout << "Reference PCM directory not found!\n";
        std::cout << "Expected location: ../examples/refrence_pcm/ (relative to executable)\n";
        return;
    }
    
    std::cout << "Directory: " << ref_dir << "\n\n";
    std::cout << "Mode    | BPS  | Interleave | Modulation | File\n";
    std::cout << "--------|------|------------|------------|-----\n";
    
    for (const auto& [id, mode] : REF_MODES) {
        fs::path pcm = find_reference_pcm(id, ref_dir);
        std::string filename = pcm.empty() ? "(not found)" : pcm.filename().string();
        
        printf("%-7s | %4d | %-10s | %-10s | %s\n",
               id.c_str(), mode.bps, mode.interleave.c_str(), 
               mode.modulation.c_str(), filename.c_str());
    }
    
    std::cout << "\nUsage: hfchansim --ref <mode> --preset <preset>\n";
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    Config cfg;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--version") {
            print_version();
            return 0;
        }
        else if (arg == "--list-ref") {
            cfg.list_refs = true;
        }
        else if (arg == "--ref" && i + 1 < argc) {
            cfg.ref_mode = argv[++i];
            // Normalize to uppercase
            for (char& c : cfg.ref_mode) c = std::toupper(c);
        }
        else if (arg == "--snr" && i + 1 < argc) {
            cfg.snr_db = std::stof(argv[++i]);
        }
        else if (arg == "--freq" && i + 1 < argc) {
            cfg.freq_offset_hz = std::stof(argv[++i]);
        }
        else if (arg == "--model" && i + 1 < argc) {
            std::string model = argv[++i];
            if (model == "awgn") cfg.model = Config::AWGN_ONLY;
            else if (model == "watterson") cfg.model = Config::WATTERSON;
            else if (model == "multipath") cfg.model = Config::MULTIPATH;
            else std::cerr << "Warning: Unknown model '" << model << "'" << std::endl;
        }
        else if (arg == "--doppler" && i + 1 < argc) {
            cfg.doppler_hz = std::stof(argv[++i]);
            cfg.model = Config::WATTERSON;
        }
        else if (arg == "--delay" && i + 1 < argc) {
            cfg.delay_ms = std::stof(argv[++i]);
            cfg.model = Config::WATTERSON;
        }
        else if (arg == "--path1-gain" && i + 1 < argc) {
            cfg.path1_gain_db = std::stof(argv[++i]);
        }
        else if (arg == "--path2-gain" && i + 1 < argc) {
            cfg.path2_gain_db = std::stof(argv[++i]);
        }
        else if (arg == "--preset" && i + 1 < argc) {
            apply_preset(cfg, argv[++i]);
        }
        else if (arg == "--sample-rate" && i + 1 < argc) {
            cfg.sample_rate = std::stof(argv[++i]);
        }
        else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoul(argv[++i]);
        }
        else if (arg == "--verbose" || arg == "-v") {
            cfg.verbose = true;
        }
        else if (arg[0] != '-' && cfg.input_file.empty()) {
            cfg.input_file = arg;
        }
        else if (arg[0] != '-' && cfg.output_file.empty()) {
            cfg.output_file = arg;
        }
        else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        }
    }
    
    // Handle --list-ref
    if (cfg.list_refs) {
        list_reference_pcms();
        return 0;
    }
    
    // Resolve reference mode
    const RefMode* ref_mode_ptr = nullptr;
    if (!cfg.ref_mode.empty()) {
        auto it = REF_MODES.find(cfg.ref_mode);
        if (it == REF_MODES.end()) {
            std::cerr << "Error: Unknown reference mode '" << cfg.ref_mode << "'" << std::endl;
            std::cerr << "Use --list-ref to see available modes" << std::endl;
            return 1;
        }
        ref_mode_ptr = &it->second;
        
        fs::path ref_dir = find_reference_pcm_dir();
        if (ref_dir.empty()) {
            std::cerr << "Error: Reference PCM directory not found" << std::endl;
            std::cerr << "Expected: ../examples/refrence_pcm/ (relative to executable)" << std::endl;
            return 1;
        }
        
        fs::path pcm_path = find_reference_pcm(cfg.ref_mode, ref_dir);
        if (pcm_path.empty()) {
            std::cerr << "Error: Reference PCM not found for mode " << cfg.ref_mode << std::endl;
            return 1;
        }
        
        cfg.input_file = pcm_path.string();
        
        // Generate output filename
        fs::path out_dir = ensure_output_dir();
        std::string preset_str = cfg.preset.empty() ? "custom" : cfg.preset;
        std::string out_name = cfg.ref_mode + "_" + preset_str + "_" + get_timestamp() + ".pcm";
        cfg.output_file = (out_dir / out_name).string();
    }
    
    // Validate inputs
    if (cfg.input_file.empty()) {
        std::cerr << "Error: No input file specified" << std::endl;
        std::cerr << "Use --ref <mode> or provide input filename" << std::endl;
        return 1;
    }
    
    if (cfg.output_file.empty()) {
        std::cerr << "Error: No output file specified" << std::endl;
        return 1;
    }
    
    // Print header
    if (cfg.verbose) {
        print_version();
        std::cout << std::endl;
    }
    
    // Read input
    if (cfg.verbose) {
        std::cout << "Reading: " << cfg.input_file << std::endl;
    }
    
    std::vector<float> samples = read_pcm(cfg.input_file, cfg.sample_rate);
    
    if (samples.empty()) {
        return 1;
    }
    
    if (cfg.verbose) {
        std::cout << "  Samples: " << samples.size() << std::endl;
        std::cout << "  Sample rate: " << cfg.sample_rate << " Hz" << std::endl;
        std::cout << "  Duration: " << (samples.size() / cfg.sample_rate) << " sec" << std::endl;
    }
    
    // Build channel settings for metadata
    ChannelSettings settings;
    settings.snr_db = cfg.snr_db;
    settings.freq_offset_hz = cfg.freq_offset_hz;
    settings.doppler_hz = cfg.doppler_hz;
    settings.delay_ms = cfg.delay_ms;
    settings.path1_gain_db = cfg.path1_gain_db;
    settings.path2_gain_db = cfg.path2_gain_db;
    settings.preset = cfg.preset;
    settings.seed = cfg.seed;
    
    // Apply frequency offset first
    if (std::abs(cfg.freq_offset_hz) > 0.001f) {
        if (cfg.verbose) {
            std::cout << "Applying frequency offset: " << cfg.freq_offset_hz << " Hz" << std::endl;
        }
        apply_freq_offset(samples, cfg.freq_offset_hz, cfg.sample_rate);
    }
    
    // Apply channel model
    if (cfg.model == Config::WATTERSON) {
        settings.model = "watterson";
        if (cfg.verbose) {
            std::cout << "Applying Watterson channel:" << std::endl;
            std::cout << "  Doppler spread: " << cfg.doppler_hz << " Hz" << std::endl;
            std::cout << "  Delay: " << cfg.delay_ms << " ms" << std::endl;
            std::cout << "  Path 1 gain: " << cfg.path1_gain_db << " dB" << std::endl;
            std::cout << "  Path 2 gain: " << cfg.path2_gain_db << " dB" << std::endl;
        }
        
        m110a::WattersonChannel::Config wcfg;
        wcfg.sample_rate = cfg.sample_rate;
        wcfg.doppler_spread_hz = cfg.doppler_hz;
        wcfg.delay_ms = cfg.delay_ms;
        wcfg.path1_gain_db = cfg.path1_gain_db;
        wcfg.path2_gain_db = cfg.path2_gain_db;
        wcfg.seed = cfg.seed;
        
        m110a::WattersonChannel channel(wcfg);
        samples = channel.process(samples);
    } else {
        settings.model = "awgn";
    }
    
    // Apply AWGN last (to get correct SNR)
    if (std::isfinite(cfg.snr_db)) {
        if (cfg.verbose) {
            std::cout << "Applying AWGN: " << cfg.snr_db << " dB SNR" << std::endl;
        }
        
        m110a::AWGNChannel awgn(cfg.seed + 9999);
        awgn.add_noise_snr(samples, cfg.snr_db);
    }
    
    // Write output PCM
    if (cfg.verbose) {
        std::cout << "Writing: " << cfg.output_file << std::endl;
    }
    
    if (!write_pcm(cfg.output_file, samples)) {
        return 1;
    }
    
    // Write metadata JSON
    std::string json_path = cfg.output_file;
    if (json_path.size() > 4 && json_path.substr(json_path.size() - 4) == ".pcm") {
        json_path = json_path.substr(0, json_path.size() - 4) + ".json";
    } else {
        json_path += ".json";
    }
    
    write_metadata_json(json_path, cfg.input_file, cfg.output_file, 
                        ref_mode_ptr, cfg.sample_rate, samples.size(), settings);
    
    // Summary
    std::cout << "\nHF Channel Simulation Complete" << std::endl;
    std::cout << "  Input:  " << cfg.input_file << std::endl;
    std::cout << "  Output: " << cfg.output_file << std::endl;
    std::cout << "  Metadata: " << json_path << std::endl;
    
    if (!cfg.preset.empty()) {
        std::cout << "  Preset: " << cfg.preset << std::endl;
    }
    if (cfg.model == Config::WATTERSON) {
        std::cout << "  Model:  Watterson (Doppler=" << cfg.doppler_hz 
                  << " Hz, Delay=" << cfg.delay_ms << " ms)" << std::endl;
    }
    if (std::isfinite(cfg.snr_db)) {
        std::cout << "  SNR:    " << cfg.snr_db << " dB" << std::endl;
    }
    if (std::abs(cfg.freq_offset_hz) > 0.001f) {
        std::cout << "  Freq offset: " << cfg.freq_offset_hz << " Hz" << std::endl;
    }
    
    return 0;
}
