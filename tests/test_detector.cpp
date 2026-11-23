#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/frame_meta.hpp"
#include "detector/detection_utils.hpp"
#include "detector/motion_node.hpp"
#include "detector/ncnn_detector_node.hpp"

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

edge::TargetBox make_box(int class_id, const std::string& label, float score,
                         float x, float y, float w, float h) {
  edge::TargetBox box;
  box.class_id = class_id;
  box.label = label;
  box.score = score;
  box.x = x;
  box.y = y;
  box.w = w;
  box.h = h;
  return box;
}

TEST(DetectionUtils, FilterDropsLowScoreAndUnlistedLabels) {
  const std::vector<edge::TargetBox> boxes = {
      make_box(0, "person", 0.90f, 0.1f, 0.1f, 0.2f, 0.3f),
      make_box(0, "person", 0.40f, 0.2f, 0.2f, 0.2f, 0.3f),
      make_box(2, "car", 0.80f, 0.5f, 0.5f, 0.2f, 0.2f),
  };

  const auto filtered =
      edge::detection::filter_detections(boxes, {"person"}, 0.5f);
  ASSERT_EQ(filtered.size(), 1u);
  EXPECT_EQ(filtered[0].label, "person");
  EXPECT_FLOAT_EQ(filtered[0].score, 0.90f);

  // Without a label list only the confidence threshold applies.
  const auto all_labels = edge::detection::filter_detections(boxes, {}, 0.5f);
  ASSERT_EQ(all_labels.size(), 2u);
  EXPECT_EQ(all_labels[0].label, "person");
  EXPECT_EQ(all_labels[1].label, "car");
}

TEST(DetectionUtils, NmsSuppressesOverlappingBoxes) {
  std::vector<edge::TargetBox> boxes = {
      make_box(0, "person", 0.90f, 0.10f, 0.10f, 0.40f, 0.40f),
      make_box(0, "person", 0.80f, 0.12f, 0.12f, 0.40f, 0.40f),
      make_box(1, "bicycle", 0.85f, 0.10f, 0.10f, 0.40f, 0.40f),
      make_box(0, "person", 0.70f, 0.70f, 0.70f, 0.20f, 0.20f),
  };

  const auto kept = edge::detection::nms(boxes, 0.45f);
  ASSERT_EQ(kept.size(), 3u);
  EXPECT_FLOAT_EQ(kept[0].score, 0.90f);
  // The bicycle overlaps the person but has a different class.
  EXPECT_FLOAT_EQ(kept[1].score, 0.85f);
  EXPECT_FLOAT_EQ(kept[2].score, 0.70f);
}

TEST(DetectionUtils, EmptyInputReturnsEmpty) {
  EXPECT_TRUE(edge::detection::filter_detections({}, {"person"}, 0.5f).empty());
  EXPECT_TRUE(edge::detection::nms({}, 0.45f).empty());
}

// Exposes process() so the unloaded-node path can be checked without threads.
class ExposedNcnnDetectorNode : public edge::NcnnDetectorNode {
 public:
  using edge::NcnnDetectorNode::NcnnDetectorNode;
  using edge::NcnnDetectorNode::process;
};

TEST(NcnnDetectorNode, MissingModelFailsAndPassesFrameThrough) {
  edge::NcnnDetectorNode::Options options;
  options.model_param = "models/does-not-exist.param";
  options.model_bin = "models/does-not-exist.bin";
  ExposedNcnnDetectorNode node(options);

  EXPECT_FALSE(node.load());
  EXPECT_FALSE(node.loaded());

  auto motion = make_frame(8, 8, 128);
  motion->has_motion = true;
  motion->infer_ms = 7.5;
  motion->targets.push_back(make_box(0, "person", 0.9f, 0.1f, 0.1f, 0.2f, 0.2f));

  EXPECT_EQ(node.process(motion), motion);
  EXPECT_EQ(motion->targets.size(), 1u);
  EXPECT_DOUBLE_EQ(motion->infer_ms, 7.5);

  auto still = make_frame(8, 8, 128);
  EXPECT_EQ(node.process(still), still);
  EXPECT_TRUE(still->targets.empty());
}

}  // namespace
