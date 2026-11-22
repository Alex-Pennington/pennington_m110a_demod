# MELP parameter-domain synthesiser (docs/melp-synth.md)
add_library(melp_synth STATIC melp_synth.cpp melp1200.cpp)
target_include_directories(melp_synth PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(melp_synth_cli main.cpp)
set_target_properties(melp_synth_cli PROPERTIES OUTPUT_NAME melp_synth
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(melp_synth_cli PRIVATE melp_synth)

# Codebook-free fields of 1200 bit/s superframes (stanag4591_tables.h)
add_executable(melp1200_fields melp1200_main.cpp)
set_target_properties(melp1200_fields PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(melp1200_fields PRIVATE melp_synth)

# Fast checks on fixed parameter sequences; no private tool or data
add_executable(test_melp_synth ${CMAKE_SOURCE_DIR}/test/melp_synth/test_melp_synth.cpp)
target_link_libraries(test_melp_synth PRIVATE melp_synth)
add_test(NAME test_melp_synth COMMAND test_melp_synth)
add_test(NAME melp_synth_cli
         COMMAND melp_synth_cli ${CMAKE_SOURCE_DIR}/test/melp_synth/fixed.params
                 ${CMAKE_BINARY_DIR}/melp_synth_cli.wav)
add_executable(test_stanag4591_tables ${CMAKE_SOURCE_DIR}/test/melp_synth/test_stanag4591_tables.cpp)
target_link_libraries(test_stanag4591_tables PRIVATE melp_synth)
add_test(NAME test_stanag4591_tables COMMAND test_stanag4591_tables)
set_tests_properties(test_melp_synth melp_synth_cli test_stanag4591_tables
                     PROPERTIES LABELS "fast;unit")
