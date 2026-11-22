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

# MIL-STD-188-110D Appendix D (3 kHz) receiver: WAV -> WID, interleaver, text
add_executable(m110d_rx m110d_rx/main.cpp)

# test_gui is Windows-only (test/test_gui/brain_client.h uses winsock without a
# POSIX fallback); build.ps1 builds it on Windows.

set_target_properties(m110a_server hfchansim m110a_rx m110d_rx PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- tests ----
enable_testing()

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
else()
  message(STATUS "python3 with numpy+scipy not found: I/Q receive tests skipped")
endif()
