# ============================================================================
# MELPe Codec - Self-Contained Export
# NATO STANAG 4591 Voice Codec (600/1200/2400 bps)
# ============================================================================
#
# OVERVIEW:
#   This is a complete, self-contained build of the MELPe voice codec.
#   MELPe (Mixed Excitation Linear Prediction enhanced) is a military-grade
#   voice codec designed for secure communications over low-bandwidth channels.
#
# SUPPORTED BIT RATES:
#   - 2400 bps: 180 samples/frame (22.5 ms), 7 bytes/frame
#   - 1200 bps: 540 samples/frame (67.5 ms), 11 bytes/frame  
#   - 600 bps:  720 samples/frame (90 ms), 7 bytes/frame
#
# AUDIO FORMAT:
#   - Sample Rate: 8000 Hz
#   - Sample Format: 16-bit signed PCM, mono, little-endian
#
# BUILD OUTPUTS:
#   - libmelpe.a      : Core codec static library
#   - libmelpe_api.a  : High-level streaming API library
#   - melpe_cli       : Command-line encoder/decoder tool
#
# USAGE IN YOUR PROJECT:
#   add_subdirectory(melpe_export)
#   target_link_libraries(your_app PRIVATE melpe_api)
#
# QUICK TEST:
#   ./melpe_cli -i input.raw -o output.raw -r 2400 -l   # loopback test
#
# ============================================================================
cmake_minimum_required(VERSION 3.10)
project(melpe VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler options
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# ============================================================================
# Core MELPe Codec Sources
# ============================================================================
set(MELPE_CORE_SOURCES
    # Analysis/Synthesis/Channel
    melp_ana.c
    melp_syn.c
    melp_chn.c
    melp_sub.c
    
    # DSP and Math libraries
    dsp_sub.c
    fft_lib.c
    fs_lib.c
    lpc_lib.c
    mat_lib.c
    math_lib.c
    mathdp31.c
    mathhalf.c
    vq_lib.c
    
    # Pitch and Voicing
    pitch.c
    pit_lib.c
    classify.c
    harm.c
    postfilt.c
    npp.c
    
    # Quantization
    qnt12.c
    qnt12_cb.c
    msvq_cb.c
    fsvq_cb.c
    coeff.c
    
    # FEC
    fec_code.c
    
    # Global state
    global.c
    
    # Transcoding
    transcode.c
)

# 600 bps specific sources
set(MELPE_600_SOURCES
    bitorder600.c
    cbk600_gain.c
    cbk600_msvq.c
    cbk600_qpit.c
    cbk600_voicing.c
    lib600_bfi.c
    lib600_gain.c
    lib600_mode.c
    lib600_msvq.c
    lib600_qpit.c
    lib600_rds.c
    lib600_srt.c
    lib600_str.c
    lib600_transc.c
    lib600_voicing.c
    lib600_wrs.c
    var600_bfi.c
    var600_gain.c
    var600_mode.c
    var600_msvq.c
    var600_qpit.c
    var600_voicing.c
)

# ============================================================================
# Build Targets
# ============================================================================

# Static library (core codec)
add_library(melpe STATIC
    ${MELPE_CORE_SOURCES}
    ${MELPE_600_SOURCES}
)
target_include_directories(melpe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Static library with streaming API
add_library(melpe_api STATIC
    melpe_api.c
)
target_include_directories(melpe_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(melpe_api PUBLIC melpe)

# Command-line tool (unlicensed reference - for development only)
add_executable(melpe_cli
    sc6enc6.c
    main.c
)
target_link_libraries(melpe_cli PRIVATE melpe)

# ============================================================================
# Phoenix Nest Licensed Vocoder
# ============================================================================
# Enable C++ for the licensed wrapper
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phoenix Nest MELPe Vocoder (licensed version)
add_executable(melpe_vocoder
    melpe_main.cpp
    sc6enc6.c
)
target_include_directories(melpe_vocoder PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..  # Project root for api/version.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..     # src/ for common/license.h
)
target_link_libraries(melpe_vocoder PRIVATE melpe)

# ============================================================================
# Installation
# ============================================================================
install(TARGETS melpe melpe_api melpe_cli melpe_vocoder
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES melpe_api.h sc1200.h DESTINATION include)

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════════╗")
message(STATUS "║           MELPe Codec v${PROJECT_VERSION} - Build Configuration              ║")
message(STATUS "╠══════════════════════════════════════════════════════════════╣")
message(STATUS "║  Bit Rates:    600 / 1200 / 2400 bps                         ║")
message(STATUS "║  Audio:        8000 Hz, 16-bit signed PCM, mono              ║")
message(STATUS "║  Arithmetic:   Fixed-point Q15 (no FPU required)             ║")
message(STATUS "╠══════════════════════════════════════════════════════════════╣")
message(STATUS "║  Targets:                                                    ║")
message(STATUS "║    libmelpe.a      - Core codec library                      ║")
message(STATUS "║    libmelpe_api.a  - Streaming API wrapper                   ║")
message(STATUS "║    melpe_cli       - Unlicensed CLI (development only)       ║")
message(STATUS "║    melpe_vocoder   - Phoenix Nest Licensed Vocoder           ║")
message(STATUS "╠══════════════════════════════════════════════════════════════╣")
message(STATUS "║  Frame Sizes:                                                ║")
message(STATUS "║    2400 bps: 180 samples (22.5 ms) ->  7 bytes               ║")
message(STATUS "║    1200 bps: 540 samples (67.5 ms) -> 11 bytes               ║")
message(STATUS "║     600 bps: 720 samples (90.0 ms) ->  7 bytes               ║")
message(STATUS "╚══════════════════════════════════════════════════════════════╝")
message(STATUS "")
