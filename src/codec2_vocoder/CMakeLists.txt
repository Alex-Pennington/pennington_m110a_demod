cmake_minimum_required(VERSION 3.13)
project(codec2_vocoder)

# Build codec2 library
set(CODEC2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/codec2)
add_subdirectory(${CODEC2_DIR} ${CMAKE_BINARY_DIR}/codec2_build EXCLUDE_FROM_ALL)

# Build our vocoder CLI
add_executable(codec2_vocoder main.cpp)

# Include both source and build directories (version.h is generated in build)
target_include_directories(codec2_vocoder PRIVATE 
    ${CODEC2_DIR}/src
    ${CMAKE_BINARY_DIR}/codec2_build
)
target_link_libraries(codec2_vocoder codec2)

# Static linking on Windows
if(WIN32)
    target_link_options(codec2_vocoder PRIVATE -static)
endif()
