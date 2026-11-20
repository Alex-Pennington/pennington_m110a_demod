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

# test_gui is Windows-only (test/test_gui/brain_client.h uses winsock without a
# POSIX fallback); build.ps1 builds it on Windows.

set_target_properties(m110a_server hfchansim PROPERTIES
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
