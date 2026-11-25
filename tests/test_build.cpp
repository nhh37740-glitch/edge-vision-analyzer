#include <gtest/gtest.h>

#include <cstring>

#include "core/build_info.hpp"
#include "core/source_kind.hpp"

TEST(BuildInfo, ReportsInjectedVersion) {
  const char* reported = edge::version();
  ASSERT_NE(reported, nullptr);
  EXPECT_GT(std::strlen(reported), 0u);
  // EDGE_VERSION comes from CMake (PUBLIC on edge_core), so a tagged build
  // reports the Tag name instead of a hardcoded string.
  EXPECT_STREQ(reported, EDGE_VERSION);
}

TEST(SourceKind, RecognizesNetworkProtocols) {
  EXPECT_TRUE(edge::is_network_source("http://192.168.10.12:8080/video"));
  EXPECT_TRUE(edge::is_network_source("https://example.test/stream"));
  EXPECT_TRUE(edge::is_network_source("rtsp://192.168.10.12:8554/live"));
  EXPECT_TRUE(edge::is_network_source("rtmp://example.test/live"));
  EXPECT_TRUE(edge::is_network_source("tcp://example.test:9000"));
  EXPECT_TRUE(edge::is_network_source("udp://example.test:9000"));
}

TEST(SourceKind, TreatsLocalPathsAsFiles) {
  EXPECT_FALSE(edge::is_network_source("assets/demo_clip.mp4"));
  EXPECT_FALSE(edge::is_network_source("/opt/edge-vision-analyzer/assets/demo_clip.mp4"));
  EXPECT_EQ(edge::classify_source("assets/demo_clip.mp4"), edge::SourceKind::kFile);
}

TEST(SourceKind, RejectsEmptyLocation) {
  EXPECT_EQ(edge::classify_source(""), edge::SourceKind::kUnknown);
  EXPECT_FALSE(edge::is_network_source(""));
}
