cmake_minimum_required(VERSION 3.20)
project(edge-vision-analyzer LANGUAGES NONE)

# PATCH-005: module/link contract scaffold only; no C++ source targets exist yet.
# Enable CXX in project(...) when the first approved source module lands.
# Then resolve dependencies as imported targets and add only targets with actual,
# explicitly listed sources. Do not glob every src/*.cpp into an executable.

# Future STATIC libraries and their direct dependency boundaries:
#   edge_core      -> C++ standard library, Threads::Threads
#   edge_common    -> nlohmann_json::nlohmann_json
#   edge_capture   -> edge_core, edge_common, FFmpeg
#   edge_detector  -> edge_core, edge_common, NCNN
#   edge_transport -> edge_core, edge_common, cpp-httplib, stb
#   edge_streamer  -> edge_core, edge_common, stb
#   edge_api       -> edge_core, edge_common, cpp-httplib
# Each edge_* target owns only its matching src/<module>/ implementation.
# Use target-specific include/link declarations; do not inject capture or FFmpeg
# into core/common or every target through global link_libraries/include paths.
# FFmpeg and other non-header-only third-party runtime libraries remain dynamic;
# project STATIC module libraries do not imply a fully static executable.
# Register real imported dependency target names when versions are selected.
# Source-based dependencies must pin a concrete release tag, never main/master.

# Future sole edge executable:
#   edge-vision-analyzer: src/main.cpp only as its application entry point;
#   links edge_core, edge_common, edge_capture, edge_detector, edge_transport,
#         edge_streamer and edge_api. Implementation sources stay in their libs.

# Future native/offline tests: include(CTest) after CXX and real targets exist.
# Under if(BUILD_TESTING), each test executable links only:
#   test_capture   -> edge_core, edge_common, edge_capture, GTest::gtest_main
#   test_detector  -> edge_core, edge_common, edge_detector, GTest::gtest_main
#   test_transport -> edge_core, edge_common, edge_transport, GTest::gtest_main
#   test_streamer  -> edge_core, edge_common, edge_streamer, GTest::gtest_main
# Keep each module's transitive dependencies within the map above. In particular,
# test_detector must not link edge_capture, the app executable, or FFmpeg.
# Cross builds use -DBUILD_TESTING=OFF; toolchain implementation is deferred to #1.

# Add the following install rule only after the real executable target exists:
# install(TARGETS edge-vision-analyzer RUNTIME DESTINATION bin)
# Release packaging will consume this install layout. Do not add an empty binary
# target, FetchContent downloads, tests, toolchain contents or build actions here.
