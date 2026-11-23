cmake_minimum_required(VERSION 3.20)
project(edge-vision-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Dependency boundaries are declared per target as modules land. Do not inject
# FFmpeg or HTTP headers globally, and do not glob sources into the executable.
find_package(Threads REQUIRED)

add_library(edge_core STATIC
  src/core/build_info.cpp
)
target_include_directories(edge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(edge_core PUBLIC Threads::Threads)

# Future libraries keep their own sources and direct dependencies:
#   edge_common    -> nlohmann_json::nlohmann_json
#   edge_capture   -> edge_core, edge_common, FFmpeg
#   edge_detector  -> edge_core, edge_common, NCNN
#   edge_transport -> edge_core, edge_common, cpp-httplib, stb
#   edge_streamer  -> edge_core, edge_common, stb
#   edge_api       -> edge_core, edge_common, cpp-httplib
# FFmpeg and other non-header-only third-party runtime libraries stay dynamic.

if(BUILD_TESTING)
  find_package(GTest REQUIRED)
  add_executable(test_build tests/test_build.cpp)
  target_link_libraries(test_build PRIVATE edge_core GTest::gtest_main)
  add_test(NAME test_build COMMAND test_build)
endif()

# The sole executable and its install rule are added when src/main.cpp lands.
# Cross builds use -DBUILD_TESTING=OFF; cmake/aarch64-toolchain.cmake selects
# the aarch64 cross compiler.
