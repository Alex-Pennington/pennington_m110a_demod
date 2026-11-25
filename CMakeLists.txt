# CMake build for Linux (and any other host with a C++17 compiler).
#
# The Windows release build is build.ps1, which stays the build of record for
# release/bin/*.exe. This file builds the same sources natively:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build -L fast          # quick suite
#   ctest --test-dir build                  # everything, including slow sweeps
#
# Compile flags match build.ps1 ($CXXFLAGS) minus the Windows-only link flags
# (-static -lws2_32), which have no Linux equivalent and are not needed.

cmake_minimum_required(VERSION 3.16)
project(m110a_modem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(Threads REQUIRED)

add_compile_definitions(_USE_MATH_DEFINES)
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

# ---- modem library (api/modem_tx.cpp api/modem_rx.cpp, as $MODEM_SOURCES) ----
add_library(m110a_modem STATIC api/modem_tx.cpp api/modem_rx.cpp)
target_link_libraries(m110a_modem PUBLIC Threads::Threads)

# ---- executables built by build.ps1 ----
add_executable(m110a_server server/main.cpp server/tcp_server_base.cpp)
target_link_libraries(m110a_server PRIVATE m110a_modem)

add_executable(hfchansim hfchansim/main.cpp)

# SDR I/Q receive path: I/Q file or stream -> decoded bytes
add_executable(m110a_rx m110a_rx/main.cpp)
target_link_libraries(m110a_rx PRIVATE m110a_modem)

# Transmitter for the Harris modes and 75 bps: payload bits -> WAV (600H, 1200H, 2400V,
# docs/harris-tx.md; 75S, 75L, docs/m110a-75bps.md)
add_executable(m110a_tx m110a_tx/main.cpp)
target_link_libraries(m110a_tx PRIVATE m110a_modem)

# MIL-STD-188-110D Appendix D (3 kHz) receiver: WAV -> WID, interleaver, text
add_executable(m110d_rx m110d_rx/main.cpp)

# MIL-STD-188-110D Appendix E channel simulator for test WAVs (test/110d/channel.py drives it)
add_executable(m110d_chan m110d_chan/main.cpp)

# test_gui is Windows-only (test/test_gui/brain_client.h uses winsock without a
# POSIX fallback); build.ps1 builds it on Windows.

# Waveform candidate scorer for recorded 110A messages (test/harris/score.py drives it
# over the Harris MELP test set; docs/harris-melpe.md)
add_executable(harris_score test/harris/harris_score.cpp)

# Standard modes through ModemTX::encode to a WAV (test/tx/tx_loopback.py drives it)
add_executable(modem_tx_wav test/tx/modem_tx_wav.cpp)
target_link_libraries(modem_tx_wav PRIVATE m110a_modem)

# MIL-STD-188-110D Appendix A LAN interface (TCP and UDP data sockets) in front of the 110A
# modem; audio in and out through files or stdin/stdout (docs/110d-appa-lan.md)
add_executable(m110_lan m110_lan/main.cpp)
target_link_libraries(m110_lan PRIVATE m110a_modem)

# ModemRX::decode() against the streaming receiver on one audio buffer: bytes, verdict, CPU
# (docs/m110a-streaming-rx.md)
add_executable(stream_vs_whole test/rx/stream_vs_whole.cpp)
target_link_libraries(stream_vs_whole PRIVATE m110a_modem)

set_target_properties(m110a_server hfchansim m110a_rx m110a_tx m110d_rx m110d_chan harris_score modem_tx_wav m110_lan stream_vs_whole PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- tests ----
enable_testing()

# MELP parameter-domain synthesiser, its CLI and its fast tests (docs/melp-synth.md)
add_subdirectory(src/melp_synth)

foreach(t test_sample_source test_iq_file_source test_iq_loopback)
  add_executable(${t} test/${t}.cpp)
  target_link_libraries(${t} PRIVATE Threads::Threads)
  # Run from the build tree: test_iq_loopback writes and deletes
  # test_roundtrip.iqr / test_metadata.iqr in its working directory, and the
  # repo root tracks files of those names.
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  set_tests_properties(${t} PROPERTIES LABELS "fast;unit")
endforeach()

# Data must start after the last preamble frame whichever frame is found
add_executable(test_preamble_countdown test/test_preamble_countdown.cpp)
target_link_libraries(test_preamble_countdown PRIVATE m110a_modem)
add_test(NAME test_preamble_countdown
         COMMAND test_preamble_countdown ${CMAKE_SOURCE_DIR}/release/examples/refrence_pcm)
set_tests_properties(test_preamble_countdown PROPERTIES LABELS "rx")

# Receiver tests from fix/m110a-mode-detect; both take the reference
# directory as argv[1]
foreach(t test_mode_detect test_decode_quality)
  add_executable(${t} test/${t}.cpp)
  target_link_libraries(${t} PRIVATE m110a_modem)
  add_test(NAME ${t} COMMAND ${t} ${CMAKE_SOURCE_DIR}/release/examples/refrence_pcm)
  set_tests_properties(${t} PROPERTIES LABELS "rx")
endforeach()

# AUTO decode when the signal starts up to 10 s into the buffer, after silence
# or noise (fix/m110a-auto-late-start)
add_executable(test_late_start test/test_late_start.cpp)
target_link_libraries(test_late_start PRIVATE m110a_modem)
add_test(NAME test_late_start COMMAND test_late_start)
set_tests_properties(test_late_start PROPERTIES LABELS "rx" TIMEOUT 3600)

# The bytes follow the signal when it ends without EOM, and no success for noise or for a
# forced mode whose D1/D2 the preamble contradicts (fix/m110a-forced-mode-gate)
add_executable(test_signal_end test/test_signal_end.cpp)
target_link_libraries(test_signal_end PRIVATE m110a_modem)
add_test(NAME test_signal_end COMMAND test_signal_end)
set_tests_properties(test_signal_end PROPERTIES LABELS "rx" TIMEOUT 3600)

# success reads every interleaver block of the message, not only its first 1440 data
# symbols (fix/m110a-quality-gate)
add_executable(test_block_gate test/test_block_gate.cpp)
target_link_libraries(test_block_gate PRIVATE m110a_modem)
add_test(NAME test_block_gate COMMAND test_block_gate)
set_tests_properties(test_block_gate PROPERTIES LABELS "rx" TIMEOUT 3600)

# A 15 ms timing step mid-message (silence or replay inserted, or deleted): the data grid
# follows it and the payload decodes exact (fix/m110a-onair-tracking)
add_executable(test_grid_step test/test_grid_step.cpp)
target_link_libraries(test_grid_step PRIVATE m110a_modem)
add_test(NAME test_grid_step COMMAND test_grid_step)
set_tests_properties(test_grid_step PROPERTIES LABELS "rx" TIMEOUT 3600)

# The 110A DFE enters the data, and leaves each block-boundary probe, with its delay lines
# holding the symbols before the next output; unity center tap on the symbol decided
# (fix/m110a-dfe-context)
add_executable(test_dfe_context test/test_dfe_context.cpp)
target_link_libraries(test_dfe_context PRIVATE m110a_modem)
add_test(NAME test_dfe_context COMMAND test_dfe_context)
set_tests_properties(test_dfe_context PROPERTIES LABELS "fast;rx")

# ModemTX output level and configuration checks (docs/TX_CHAIN.md)
add_executable(test_modem_tx test/test_modem_tx.cpp)
target_link_libraries(test_modem_tx PRIVATE m110a_modem)
add_test(NAME test_modem_tx COMMAND test_modem_tx)
set_tests_properties(test_modem_tx PROPERTIES LABELS "fast;tx")

# Table XI codes 7,7 (2400 digital voice), 7,5 / 5,5 (75 bps) and the Harris
# application-specific codes 5,7 / 5,6: own TX -> AUTO RX
add_executable(test_table_xi_modes test/test_table_xi_modes.cpp)
target_link_libraries(test_table_xi_modes PRIVATE m110a_modem)
add_test(NAME test_table_xi_modes COMMAND test_table_xi_modes)
set_tests_properties(test_table_xi_modes PROPERTIES LABELS "rx")

# 75 bps (75S, 75L): Tables IX/X, the 75N interleaver and the symbol stream against the
# text of MIL-STD-188-110D 5.3.2; ModemTX -> ModemRX loopback, noise and forced-mode
# gates (docs/m110a-75bps.md)
add_executable(test_m75 test/test_m75.cpp)
target_link_libraries(test_m75 PRIVATE m110a_modem)
add_test(NAME test_m75 COMMAND test_m75)
set_tests_properties(test_m75 PROPERTIES LABELS "rx;tx;m75" TIMEOUT 1800)
# WAV -> ModemRX, printing m110a_rx's message lines: test/m75/m75_sweep.py --wav-rx measures
# the modem without m110a_rx's I/Q front end and preamble scanner
add_executable(m75_wav_rx test/m75/wav_rx.cpp)
target_link_libraries(m75_wav_rx PRIVATE m110a_modem)
set_target_properties(m75_wav_rx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# 75 bps BER in process: ModemTX -> Appendix E channel -> interference -> ModemRX
# (test/m75/ber.cpp, test/m75/ber_sweep.py; docs/m110a-75bps.md, STANAG 4415 section)
add_executable(m75_ber test/m75/ber.cpp)
target_link_libraries(m75_ber PRIVATE m110a_modem)
set_target_properties(m75_ber PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# The Appendix E fading model at 0.5-50 Hz Doppler and the two-path, 10 ms channel built on it
add_executable(m75_fading_check test/m75/fading_check.cpp)
set_target_properties(m75_fading_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME m75_fading_check COMMAND m75_fading_check --seconds 0.5)
set_tests_properties(m75_fading_check PROPERTIES LABELS "fast;chansim;m75" TIMEOUT 900)

# ---- I/Q receive path through m110a_rx ----
# Synthetic RSP2 I/Q (test/rx/synth_iq.py: 2 MS/s cs16, dial at +100 kHz)
# carrying the MS-DMT reference transmissions, generated at test time in the
# build tree and deleted after each case. Needs python3 with numpy and scipy;
# skipped otherwise.
find_package(Python3 COMPONENTS Interpreter)
set(M110A_HAVE_SCIPY OFF)
if(Python3_Interpreter_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -I -c "import numpy, scipy.signal"
                  RESULT_VARIABLE _scipy_rc OUTPUT_QUIET ERROR_QUIET)
  if(_scipy_rc EQUAL 0)
    set(M110A_HAVE_SCIPY ON)
  endif()
endif()
if(M110A_HAVE_SCIPY)
  set(_ref ${CMAKE_SOURCE_DIR}/release/examples/refrence_pcm)
  set(_sweep ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/test/rx/rx_sweep.py
             --rx $<TARGET_FILE:m110a_rx> --ref ${_ref})
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/rx_work)

  # gate: the I/Q front end within 1 dB of a scipy polyphase chain
  add_test(NAME iq_frontend_vs_scipy
           COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/test/rx/frontend_vs_scipy.py
                   --rx $<TARGET_FILE:m110a_rx> --ref ${_ref}
                   --work ${CMAKE_BINARY_DIR}/rx_work/fe)

  # name | sweep arguments. Each case must decode the exact payload with EOM.
  set(_iq_cases
    "iq_600S_auto_15dB_p15Hz_lead2.3|--modes 600S --auto --snr 15 --foff 15 --lead 2.3"
    "iq_600S_10dB_m15Hz_lead0.01|--modes 600S --snr 10 --foff -15 --lead 0.01"
    "iq_150S_auto_6dB_m15Hz_lead0.7|--modes 150S --auto --snr 6 --foff -15 --lead 0.7"
    "iq_1200S_auto_12dB_p10Hz_lead1.1_stdin|--modes 1200S --auto --snr 12 --foff 10 --lead 1.1 --stdin"
    "iq_600L_auto_10dB_m7.6Hz_lead3.05|--modes 600L --auto --snr 10 --foff -7.6 --lead 3.05"
    "iq_2400L_auto_20dB_p15Hz_lead0.5|--modes 2400L --auto --snr 20 --foff 15 --lead 0.5"
    "iq_300S_ppm_7MHz|--modes 300S --auto --snr 12 --lead 0.4 --ppm-dial 7000000:-1.09"
  )
  foreach(c ${_iq_cases})
    string(REPLACE "|" ";" _p "${c}")
    list(GET _p 0 _name)
    list(GET _p 1 _args)
    separate_arguments(_args UNIX_COMMAND "${_args}")
    add_test(NAME ${_name} COMMAND ${_sweep} ${_args} --jobs 1
                                   --work ${CMAKE_BINARY_DIR}/rx_work/${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "iq")
  endforeach()
  set_tests_properties(iq_frontend_vs_scipy PROPERTIES LABELS "iq")

  # Harris-mode TX: payload -> m110a_tx -> WAV -> m110a_rx, byte-exact with EOM
  # (600H, 1200H, 2400V; test/harris/tx_loopback.py, docs/harris-tx.md)
  add_test(NAME harris_tx_loopback
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/harris/tx_loopback.py
                   --rx $<TARGET_FILE:m110a_rx> --tx $<TARGET_FILE:m110a_tx>
                   --work ${CMAKE_BINARY_DIR}/rx_work/harris_tx_loopback)
  set_tests_properties(harris_tx_loopback PROPERTIES LABELS "iq")

  # Standard modes: ModemTX::encode -> WAV -> noise -> I/Q -> m110a_rx (AUTO), the exact
  # payload with success=1 and eom=1 (test/tx/tx_loopback.py, docs/TX_CHAIN.md).
  # 4800S is uncoded 8PSK: at 10 dB its BER is near 1e-2, so it runs clean and at
  # 18 dB, where theory gives below 1e-5.
  set(_txlb ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/tx/tx_loopback.py
            --tool $<TARGET_FILE:modem_tx_wav> --rx $<TARGET_FILE:m110a_rx>)
  set(_coded 150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L)
  set(_tx_cases
    "tx_loopback_rrc|--modes ${_coded} --snr inf,10"
    "tx_loopback_rrc_4800S|--modes 4800S --snr inf,18 --seeds 1,2,3"
    "tx_loopback_rect|--modes ${_coded} --snr inf,10 --rect"
    # message lengths around frame, block and byte boundaries
    "tx_loopback_lengths|--modes 150S,600S,2400S,1200L,4800S --snr inf --bytes 1,23,157,158,159,400"
    # the sender's clock 36 ppm off the receiver's, as on air (docs/m110a-timing-steps.md)
    "tx_loopback_drift|--modes 150L,300L,600L,1200L --snr 10 --bytes 400 --ppm 36,-36"
  )
  foreach(c ${_tx_cases})
    string(REPLACE "|" ";" _p "${c}")
    list(GET _p 0 _name)
    list(GET _p 1 _args)
    separate_arguments(_args UNIX_COMMAND "${_args}")
    add_test(NAME ${_name} COMMAND ${_txlb} ${_args} --work ${CMAKE_BINARY_DIR}/rx_work/${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "iq;tx" TIMEOUT 3600)
  endforeach()

  # message end: a fade or dropout inside the message does not end it, the end does not
  # move with the lead-in, a real end ends it within 1 s, a cut message gives a whole-block
  # prefix and nothing after it, two messages 1 s apart both decode, and a preamble split by
  # dropouts still decodes (test/rx/end_fade.py)
  add_test(NAME rx_end_fade
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/rx/end_fade.py
                   --tool $<TARGET_FILE:modem_tx_wav> --rx $<TARGET_FILE:m110a_rx>
                   --work ${CMAKE_BINARY_DIR}/rx_work/rx_end_fade)
  set_tests_properties(rx_end_fade PROPERTIES LABELS "iq;tx" TIMEOUT 1800)

  # the re-encode check: wrong bytes with a fade inside their block are not real, right bytes
  # are, and its block lines keep the form gale-rx parses (test/rx/reencode_window.py)
  add_test(NAME rx_reencode_window
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/rx/reencode_window.py
                   --tool $<TARGET_FILE:modem_tx_wav> --rx $<TARGET_FILE:m110a_rx>
                   --work ${CMAKE_BINARY_DIR}/rx_work/rx_reencode_window)
  set_tests_properties(rx_reencode_window PROPERTIES LABELS "iq;tx" TIMEOUT 3600)

  # the decode-quality gate says success=0 for noise after a genuine preamble
  add_test(NAME tx_noise_gate
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/tx/noise_gate.py
                   --tool $<TARGET_FILE:modem_tx_wav> --rx $<TARGET_FILE:m110a_rx>
                   --work ${CMAKE_BINARY_DIR}/rx_work/tx_noise_gate)
  set_tests_properties(tx_noise_gate PROPERTIES LABELS "iq;tx" TIMEOUT 3600)

  # hfchansim --freq is a true frequency shift: one tone at f + F, the image at f - F at
  # least 60 dB down; and ModemTX -> hfchansim --freq -> m110a_rx decodes to +/-75 Hz,
  # the offset MIL-STD-188-110D tests at (test/chansim/freq_offset.py)
  set(_fo ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/chansim/freq_offset.py
          --chansim $<TARGET_FILE:hfchansim>)
  add_test(NAME hfchansim_freq_tones
           COMMAND ${_fo} --tones --offsets 5,-5,20,-20,75,-75 --image-db 60
                   --work ${CMAKE_BINARY_DIR}/rx_work/hfchansim_freq_tones)
  set_tests_properties(hfchansim_freq_tones PROPERTIES LABELS "fast;chansim" TIMEOUT 600)
  add_test(NAME hfchansim_freq_loopback
           COMMAND ${_fo} --loopback --tool $<TARGET_FILE:modem_tx_wav> --rx $<TARGET_FILE:m110a_rx>
                   --modes 150S,600S,2400S,1200L --offsets=5,-5,20,-20,40,-40,75,-75 --snr inf,10
                   --work ${CMAKE_BINARY_DIR}/rx_work/hfchansim_freq_loopback)
  set_tests_properties(hfchansim_freq_loopback PROPERTIES LABELS "iq;chansim" TIMEOUT 3600)

  # long sweeps: ctest -L slow
  set(_all_modes 150S,150L,300S,300L,600S,600L,1200S,1200L,2400L)
  set(_slow
    "iq_sweep_mistune|--modes ${_all_modes} --auto --snr 15 --foff=-15,-10,-7.6,-3,0,3,7.6,10,15 --lead 0.5"
    "iq_sweep_snr|--modes 150S,600S --auto --snr 4,6,8,10 --foff=-15,0,15 --seeds 1,2"
    # 1200S fails at 4 dB at 0 Hz as well as at +/-15 Hz: the mode's noise
    # limit, not acquisition
    "iq_sweep_snr_1200S|--modes 1200S --auto --snr 6,8,10 --foff=-15,0,15 --seeds 1,2"
    "iq_sweep_start|--modes 600S,1200L --auto --snr 10 --foff 7.6 --lead 0,0.005,0.01,0.02,0.05,0.137,0.5,1.9,7.3"
  )
  foreach(c ${_slow})
    string(REPLACE "|" ";" _p "${c}")
    list(GET _p 0 _name)
    list(GET _p 1 _args)
    separate_arguments(_args UNIX_COMMAND "${_args}")
    add_test(NAME ${_name} COMMAND ${_sweep} ${_args} --jobs 3
                                   --work ${CMAKE_BINARY_DIR}/rx_work/${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "slow" TIMEOUT 7200)
  endforeach()

  # 75 bps robustness gates (test/m75/m75_sweep.py, docs/m110a-75bps.md): 64-byte messages
  # starting "DE AAM402", noise in 3 kHz; each listed seed must decode exact (success=1,
  # eom=1, mode named, bytes equal) and no decode may succeed with wrong bytes.
  set(_m75 ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/m75/m75_sweep.py
           --tool $<TARGET_FILE:modem_tx_wav> --chansim $<TARGET_FILE:hfchansim> --jobs 3)
  set(_m75_cases
    # AWGN through ModemRX: exact at -8 dB (the knee is -9 to -10 dB)
    "m75_awgn|--wav-rx $<TARGET_FILE:m75_wav_rx> --modes 75S,75L --snr=-8 --seeds 1,2,3,4 --require 75S:-8:4 --require 75L:-8:4"
    # CCIR poor (Watterson 2 Hz, 2 ms) through ModemRX
    "m75_watterson_poor|--wav-rx $<TARGET_FILE:m75_wav_rx> --channel poor --modes 75S,75L --snr=0 --seeds 1,2,3 --require 75S:0:3 --require 75L:0:3"
    # the I/Q route, m110a_rx's preamble scan in front (it finds no preamble below about -7 dB)
    "m75_iq|--rx $<TARGET_FILE:m110a_rx> --modes 75S,75L --snr=0 --seeds 1,2 --require 75S:0:2 --require 75L:0:2"
  )
  foreach(c ${_m75_cases})
    string(REPLACE "|" ";" _p "${c}")
    list(GET _p 0 _name)
    list(GET _p 1 _args)
    separate_arguments(_args UNIX_COMMAND "${_args}")
    add_test(NAME ${_name} COMMAND ${_m75} ${_args} --work ${CMAKE_BINARY_DIR}/rx_work/${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "m75;tx;rx" TIMEOUT 3600)
  endforeach()

  # STANAG 4415 points (secondary figures, docs/m110a-75bps.md), cheapest representative
  # cases of the measured tables: m75_ber in process, 180-byte messages starting
  # "DE AAM402", 2 seeds each; every case 0 bit errors (max_ber 1e-4 over 2880 bits) and no
  # decode may succeed with wrong bits
  set(_m75ber ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/m75/ber_sweep.py
              --ber $<TARGET_FILE:m75_ber> --jobs 3 --seeds 1:2)
  add_test(NAME m75_4415_awgn COMMAND ${_m75ber}
           --case "75L-9|--mode 75L --bytes 180 --snr -9" --case "75S-9|--mode 75S --bytes 180 --snr -9"
           --require "75L-9:max_ber=1e-4" --require "75S-9:max_ber=1e-4")
  add_test(NAME m75_4415_doppler COMMAND ${_m75ber}
           --case "d0.5|--mode 75L --bytes 180 --snr 0 --path 0:0:0.5 --path 10:0:0.5"
           --case "d50|--mode 75L --bytes 180 --snr -1 --path 0:0:50 --path 10:0:50"
           --require "d0.5:max_ber=1e-4" --require "d50:max_ber=1e-4"
           --require "d0.5:min_sync=2" --require "d50:min_sync=2")
  add_test(NAME m75_4415_interference COMMAND ${_m75ber}
           --case "tones4|--mode 75L --bytes 180 --snr 10 --tone 813:-40 --tone 1327:-40 --tone 1871:-40 --tone 2459:-40"
           --case "sweep|--mode 75L --bytes 180 --snr 10 --sweep -40"
           --case "fsk|--mode 75L --bytes 180 --snr 10 --fsk -40"
           --case "voice|--mode 75L --bytes 180 --snr 10 --voice -25"
           --case "cochannel|--mode 75L --bytes 180 --snr 10 --cochannel -6"
           --require "tones4:max_ber=1e-4" --require "sweep:max_ber=1e-4" --require "fsk:max_ber=1e-4"
           --require "voice:max_ber=1e-4" --require "cochannel:max_ber=1e-4")
  set_tests_properties(m75_4415_awgn m75_4415_doppler m75_4415_interference PROPERTIES
                       LABELS "m75;rx;4415" TIMEOUT 1800)
else()
  message(STATUS "python3 with numpy+scipy not found: I/Q receive tests skipped")
endif()

# ---- m110a_server: creates --output-dir; SENDBUFFER -> RXAUDIOINJECT through it ----
if(Python3_Interpreter_FOUND)
  add_test(NAME server_output_dir
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/server/test_output_dir.py
                   $<TARGET_FILE:m110a_server> ${CMAKE_BINARY_DIR}/server_work/output_dir)
  set_tests_properties(server_output_dir PROPERTIES LABELS "fast;server" TIMEOUT 120)
else()
  message(STATUS "python3 not found: server_output_dir test skipped")
endif()

# ---- Harris MELPe scorer (test/harris): packing, sync search, speech measure, proofs ----
# Needs no MELPe coder; the scorer's channel-audio check runs only when scipy is present.
if(Python3_Interpreter_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -I -c "import numpy"
                  RESULT_VARIABLE _numpy_rc OUTPUT_QUIET ERROR_QUIET)
  if(_numpy_rc EQUAL 0)
    add_test(NAME harris_melpe_scorer
             COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/harris/test_harris.py
                     ${CMAKE_BINARY_DIR}/harris_work)
    set_tests_properties(harris_melpe_scorer PROPERTIES LABELS "fast;unit")
  else()
    message(STATUS "python3 with numpy not found: Harris MELPe scorer test skipped")
  endif()
endif()

# ---- m110_lan: MIL-STD-188-110D Appendix A (docs/110d-appa-lan.md) ----
# Protocol layer: CRC, TCP/UDP framing, RS(GF16) erasure FEC and interleaver vs the appendix
add_executable(test_appa_proto test/lan/test_appa_proto.cpp)
add_test(NAME test_appa_proto COMMAND test_appa_proto)
set_tests_properties(test_appa_proto PROPERTIES LABELS "fast;unit;lan")
# A DTE over TCP and over UDP against the running server: handshake, second DTE refused,
# transmit to WAV, that WAV back as receive audio, every packet type checked byte by byte
if(Python3_Interpreter_FOUND)
  add_test(NAME appa_lan_dte
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/lan/appa_dte.py
                   --server $<TARGET_FILE:m110_lan> --work ${CMAKE_BINARY_DIR}/lan_work)
  set_tests_properties(appa_lan_dte PROPERTIES LABELS "lan" TIMEOUT 600 RUN_SERIAL TRUE)
  # The receiver against real-time stdin audio: nothing dropped during a long decode, carrier at
  # preamble sync, bursts found by preamble in noise, --rx-low-quality (needs numpy; 77 = skip)
  add_test(NAME appa_rx_stream
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/lan/appa_rx_stream.py
                   --server $<TARGET_FILE:m110_lan> --work ${CMAKE_BINARY_DIR}/lan_rx_work)
  set_tests_properties(appa_rx_stream PROPERTIES LABELS "lan" TIMEOUT 900 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
  # 75S and 75L both ways over the TCP data socket (transmit WAV through ModemRX and m110a_rx;
  # that WAV back in real time, clean and at -6 dB); the chunk framing of every mode
  add_test(NAME appa_lan_75
           COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/test/lan/appa_m75.py
                   --server $<TARGET_FILE:m110_lan> --rx $<TARGET_FILE:m110a_rx>
                   --wav-rx $<TARGET_FILE:m75_wav_rx> --work ${CMAKE_BINARY_DIR}/lan_75_work)
  set_tests_properties(appa_lan_75 PROPERTIES LABELS "lan" TIMEOUT 1200 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
else()
  message(STATUS "python3 not found: appa_lan_dte test skipped")
endif()
