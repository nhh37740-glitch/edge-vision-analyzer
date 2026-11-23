#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/frame_meta.hpp"
#include "detector/motion_node.hpp"

namespace {

using edge::FrameMeta;
using edge::FrameMetaPtr;

// Exposes the protected process() hook so the funnel can be tested without
// worker threads or downstream nodes.
class ExposedMotionNode : public edge::MotionNode {
 public:
  using edge::MotionNode::MotionNode;
  using edge::MotionNode::process;
};

FrameMetaPtr make_frame(int width, int height, std::uint8_t value) {
  auto meta = std::make_shared<FrameMeta>();
  meta->width = width;
  meta->height = height;
  meta->rgb_data.assign(static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) * 3,
                        value);
  return meta;
}

TEST(MotionNode, StaticFrameIsBlockedAndPassedThrough) {
  ExposedMotionNode node;

  auto first = make_frame(64, 48, 40);
  EXPECT_EQ(node.process(first), first);
  EXPECT_TRUE(first->has_motion);
  EXPECT_FLOAT_EQ(first->motion_score, 0.0f);

  auto second = make_frame(64, 48, 40);
  EXPECT_EQ(node.process(second), second);
  EXPECT_FALSE(second->has_motion);
  EXPECT_FLOAT_EQ(second->motion_score, 0.0f);
}

TEST(MotionNode, ChangedFramePassesThreshold) {
  ExposedMotionNode node;

  auto dark = make_frame(64, 48, 0);
  node.process(dark);

  auto bright = make_frame(64, 48, 255);
  node.process(bright);
  EXPECT_TRUE(bright->has_motion);
  EXPECT_NEAR(bright->motion_score, 255.0f, 0.01f);

  // Runtime threshold updates take effect without rebuilding the node.
  node.set_threshold(300.0);
  EXPECT_DOUBLE_EQ(node.threshold(), 300.0);
  auto dark_again = make_frame(64, 48, 0);
  node.process(dark_again);
  EXPECT_FALSE(dark_again->has_motion);
}

TEST(MotionNode, EmptyOrShortFrameIsSafe) {
  ExposedMotionNode node;

  auto empty = std::make_shared<FrameMeta>();
  empty->width = 64;
  empty->height = 48;
  EXPECT_EQ(node.process(empty), empty);
  EXPECT_FALSE(empty->has_motion);
  EXPECT_FLOAT_EQ(empty->motion_score, 0.0f);

  auto short_frame = std::make_shared<FrameMeta>();
  short_frame->width = 64;
  short_frame->height = 48;
  short_frame->rgb_data = {0, 0, 0};
  EXPECT_EQ(node.process(short_frame), short_frame);
  EXPECT_FALSE(short_frame->has_motion);

  auto zero_size = std::make_shared<FrameMeta>();
  EXPECT_EQ(node.process(zero_size), zero_size);
  EXPECT_FALSE(zero_size->has_motion);

  // An invalid frame must not consume the first-frame baseline.
  auto valid = make_frame(64, 48, 10);
  node.process(valid);
  EXPECT_TRUE(valid->has_motion);
}

}  // namespace
