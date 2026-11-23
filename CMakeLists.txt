cmake_minimum_required(VERSION 3.20)
project(edge-vision-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Dependency boundaries are declared per target as modules land. Do not inject
# FFmpeg or HTTP headers globally, and do not glob sources into the executable.
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswscale)
find_path(STB_INCLUDE_DIR NAMES stb_image_write.h PATH_SUFFIXES stb REQUIRED)

add_library(edge_core STATIC
  src/core/base_node.cpp
  src/core/build_info.cpp
  src/core/source_kind.cpp
)
target_include_directories(edge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(edge_core PUBLIC Threads::Threads)

add_library(edge_common STATIC
  src/common/config.cpp
  src/common/metrics.cpp
)
target_include_directories(edge_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(edge_common PUBLIC nlohmann_json::nlohmann_json)

add_library(edge_capture STATIC
  src/capture/capture_node.cpp
  src/capture/frame_dumper.cpp
  src/capture/remux_recorder.cpp
)
target_include_directories(edge_capture PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(edge_capture PUBLIC edge_core edge_common PkgConfig::FFMPEG)

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

  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE edge_core edge_common GTest::gtest_main)
  target_compile_definitions(test_core PRIVATE
    EDGE_CONFIG_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/config/config.example.json")
  add_test(NAME test_core COMMAND test_core)
endif()

# The sole executable and its install rule are added when src/main.cpp lands.
# Cross builds use -DBUILD_TESTING=OFF; cmake/aarch64-toolchain.cmake selects
# the aarch64 cross compiler.
