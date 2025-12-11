// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest MELPe Vocoder - NATO STANAG 4591 Voice Codec
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file melpe_main.cpp
 * @brief MELPe Vocoder - Entry Point
 * 
 * This is a C++ wrapper around the NATO STANAG 4591 MELPe reference codec.
 * Adds Phoenix Nest branding while preserving the core codec functionality 
 * from the public NATO standard reference implementation.
 * 
 * Core Codec Attribution:
 *   NATO STANAG 4591 MELPe Reference Implementation
 *   Mixed Excitation Linear Prediction enhanced (MELPe)
 *   Public standard for military voice communications
 * 
 * Usage:
 *   melpe_vocoder [options] -i infile -o outfile
 *   melpe_vocoder --help
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Phoenix Nest common includes
#include "api/version.h"

// MELPe core codec entry point (C linkage)
extern "C" int sc6enc6(int argc, char *argv[]);

using namespace m110a;

//------------------------------------------------------------------------------
// Version and Attribution
//------------------------------------------------------------------------------

static const char* MELPE_PRODUCT_NAME = "Phoenix Nest MELPe Vocoder";
static const char* MELPE_VERSION = "1.0.0";
static const char* MELPE_CODEC_ATTRIBUTION = 
    "Core Codec: NATO STANAG 4591 MELPe Reference Implementation\n"
    "            Mixed Excitation Linear Prediction enhanced (MELPe)\n"
    "            600/1200/2400 bps military voice codec";

static void print_banner(bool quiet = false) {
    if (quiet) return;
    
    std::cout << "================================================================\n";
    std::cout << MELPE_PRODUCT_NAME << " v" << MELPE_VERSION << "\n";
    std::cout << "================================================================\n";
    std::cout << m110a::copyright_notice() << "\n";
    std::cout << "----------------------------------------------------------------\n";
    std::cout << MELPE_CODEC_ATTRIBUTION << "\n";
    std::cout << "================================================================\n\n";
}

static void print_usage(const char* program) {
    print_banner();
    
    std::cout << "Usage:\n";
    std::cout << "  " << program << " [options] -i infile -o outfile\n\n";
    std::cout << "Options:\n";
    std::cout << "  -q           Quiet mode (suppress frame counter)\n";
    std::cout << "  -p           Bypass Noise Preprocessor\n";
    std::cout << "  -b density   Channel bit density:\n";
    std::cout << "                 6  = 6 bits/word (CTF compatible)\n";
    std::cout << "                 54 = 54 of 56 bits (default)\n";
    std::cout << "                 56 = 56 of 56 bits (packed)\n";
    std::cout << "  -r rate      Encoding rate:\n";
    std::cout << "                 2400 = MELPe 2400 bps (default)\n";
    std::cout << "                 1200 = MELPe 1200 bps\n";
    std::cout << "                 600  = MELPe 600 bps\n";
    std::cout << "  -m mode      Processing mode:\n";
    std::cout << "                 C = Analysis + Synthesis (encode/decode loopback)\n";
    std::cout << "                 A = Analysis only (encode PCM to bitstream)\n";
    std::cout << "                 S = Synthesis only (decode bitstream to PCM)\n";
    std::cout << "                 U = Transcode up (600->2400 or 1200->2400)\n";
    std::cout << "                 D = Transcode down (2400->600 or 2400->1200)\n";
    std::cout << "  -i infile    Input file (raw 16-bit PCM or bitstream)\n";
    std::cout << "  -o outfile   Output file (bitstream or raw 16-bit PCM)\n";
    std::cout << "  --help       Show this help\n";
    std::cout << "  --version    Show version information\n";
    std::cout << "\n";
    std::cout << "Audio Format:\n";
    std::cout << "  Input/Output: Raw PCM, 16-bit signed, little-endian, 8000 Hz, mono\n";
    std::cout << "\n";
    std::cout << "Frame Sizes:\n";
    std::cout << "  2400 bps: 180 samples (22.5 ms) ->  7 bytes\n";
    std::cout << "  1200 bps: 540 samples (67.5 ms) -> 11 bytes\n";
    std::cout << "   600 bps: 720 samples (90.0 ms) ->  7 bytes\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  Encode at 2400 bps:  " << program << " -r 2400 -m A -i speech.raw -o speech.mel\n";
    std::cout << "  Decode 2400 bps:     " << program << " -r 2400 -m S -i speech.mel -o speech.raw\n";
    std::cout << "  Loopback test:       " << program << " -r 2400 -m C -i speech.raw -o output.raw\n";
}

static void print_version() {
    std::cout << MELPE_PRODUCT_NAME << " v" << MELPE_VERSION << "\n";
    std::cout << m110a::build_info() << "\n";
    std::cout << "\n";
    std::cout << MELPE_CODEC_ATTRIBUTION << "\n";
}

//------------------------------------------------------------------------------
// Main Entry Point
//------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    bool quiet = false;
    bool show_help = false;
    bool show_version = false;
    
    // Quick scan for our custom flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = true;
        }
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            show_version = true;
        }
        else if (strcmp(argv[i], "-q") == 0) {
            quiet = true;
        }
    }
    
    // Handle info requests
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (show_version) {
        print_version();
        return 0;
    }
    
    // No arguments - show usage
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Print banner
    if (!quiet) {
        print_banner();
    }
    
    // Pass through to the MELPe codec
    // The core codec handles all the -i, -o, -r, -m, -b, -p, -q flags
    return sc6enc6(argc, argv);
}
