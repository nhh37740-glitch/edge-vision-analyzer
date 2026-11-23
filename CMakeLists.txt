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
pkg_check_modules(HTTPLIB QUIET IMPORTED_TARGET cpp-httplib)
if(NOT HTTPLIB_FOUND)
  find_path(HTTPLIB_INCLUDE_DIR NAMES httplib.h REQUIRED)
endif()

# NCNN for edge_detector (Issue #42), pinned to a released tag. Never track
# main/master. Tooling, examples, tests, Python bindings and Vulkan stay off.
include(FetchContent)
set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
  # ncnn still declares a CMake <3.5 compatibility floor, which CMake 4 rejects.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()
FetchContent_Declare(ncnn
  URL https://github.com/Tencent/ncnn/archive/refs/tags/20261020.tar.gz
  URL_HASH SHA256=968d8d8e65ccadc803322f8238736176e0f9c929fcf484895f58a63e6b227127
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ncnn)

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

add_library(edge_detector STATIC
  src/detector/model_selection.cpp
  src/detector/motion_node.cpp
  src/detector/ncnn_detector_node.cpp
)
target_link_libraries(edge_detector PUBLIC edge_core edge_common PRIVATE ncnn)

add_library(edge_capture STATIC
  src/capture/capture_node.cpp
  src/capture/frame_dumper.cpp
  src/capture/remux_recorder.cpp
)
target_include_directories(edge_capture PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(edge_capture PUBLIC edge_core edge_common PkgConfig::FFMPEG)

add_library(edge_api STATIC
  src/api/api_server.cpp
)
if(HTTPLIB_FOUND)
  # Ubuntu ships cpp-httplib as a compiled library plus a pkg-config file.
  target_link_libraries(edge_api PUBLIC edge_core edge_common PkgConfig::HTTPLIB)
else()
  target_include_directories(edge_api PUBLIC ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(edge_api PUBLIC edge_core edge_common)
endif()
if(WIN32)
  target_link_libraries(edge_api PUBLIC ws2_32)
endif()

add_library(edge_streamer STATIC
  src/streamer/osd_annotator.cpp
  src/streamer/stream_out_node.cpp
  src/streamer/rtsp_pusher.cpp
)
target_include_directories(edge_streamer PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(edge_streamer PUBLIC edge_core edge_common)

add_library(edge_transport STATIC
  src/transport/jpeg_base64.cpp
  src/transport/genai_worker.cpp
  src/transport/event_tracker_node.cpp
  src/transport/llm_selection.cpp
)
target_include_directories(edge_transport PRIVATE ${STB_INCLUDE_DIR})
if(HTTPLIB_FOUND)
  target_link_libraries(edge_transport PUBLIC edge_core edge_common PkgConfig::HTTPLIB)
else()
  target_include_directories(edge_transport PUBLIC ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(edge_transport PUBLIC edge_core edge_common)
endif()
if(WIN32)
  target_link_libraries(edge_transport PUBLIC ws2_32)
endif()

add_executable(edge-vision-analyzer src/main.cpp)
target_link_libraries(edge-vision-analyzer PRIVATE
  edge_core edge_common edge_capture edge_api)
install(TARGETS edge-vision-analyzer RUNTIME DESTINATION bin)

# Remaining libraries keep their own sources and direct dependencies.
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

  add_executable(test_capture tests/test_capture.cpp)
  target_link_libraries(test_capture PRIVATE
    edge_core edge_common edge_capture GTest::gtest_main)
  add_test(NAME test_capture COMMAND test_capture)
  set_tests_properties(test_capture PROPERTIES TIMEOUT 5)

  add_executable(test_detector tests/test_detector.cpp)
  target_link_libraries(test_detector PRIVATE
    edge_core edge_common edge_detector GTest::gtest_main)
  add_test(NAME test_detector COMMAND test_detector)
  set_tests_properties(test_detector PROPERTIES TIMEOUT 5)

  add_executable(test_llm_selection tests/test_llm_selection.cpp)
  target_link_libraries(test_llm_selection PRIVATE
    edge_core edge_common edge_transport GTest::gtest_main)
  add_test(NAME test_llm_selection COMMAND test_llm_selection)
  set_tests_properties(test_llm_selection PROPERTIES TIMEOUT 5)

  add_executable(test_api tests/test_api.cpp)
  target_link_libraries(test_api PRIVATE
    edge_core edge_common edge_api GTest::gtest_main)
  add_test(NAME test_api COMMAND test_api)
  set_tests_properties(test_api PROPERTIES TIMEOUT 5)

  add_executable(test_streamer tests/test_streamer.cpp)
  target_link_libraries(test_streamer PRIVATE
    edge_core edge_common edge_streamer GTest::gtest_main)
  add_test(NAME test_streamer COMMAND test_streamer)
  set_tests_properties(test_streamer PROPERTIES TIMEOUT 5)

  add_executable(test_transport tests/test_transport.cpp)
  target_link_libraries(test_transport PRIVATE
    edge_core edge_common edge_transport GTest::gtest_main)
  add_test(NAME test_transport COMMAND test_transport)
  set_tests_properties(test_transport PROPERTIES TIMEOUT 5)
endif()

# Cross builds use -DBUILD_TESTING=OFF; cmake/aarch64-toolchain.cmake selects
# the aarch64 cross compiler.
