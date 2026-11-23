#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/frame_meta.hpp"
#include "streamer/osd_annotator.hpp"
#include "streamer/stream_out_node.hpp"

namespace {

using edge::FrameMeta;
using edge::TargetBox;

constexpr std::uint8_t kBackground = 0;

// Exposes the protected process() hook so the encoding state machine can be
// driven synchronously without starting the worker thread.
class TestStreamOutNode : public edge::StreamOutNode {
 public:
  using edge::StreamOutNode::StreamOutNode;
  using edge::StreamOutNode::process;
};

FrameMeta make_frame(int width, int height, std::uint8_t fill) {
  FrameMeta frame;
  frame.width = width;
  frame.height = height;
  frame.rgb_data.assign(static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) * 3,
                        fill);
  return frame;
}

std::uint8_t channel_at(const std::vector<std::uint8_t>& data, int width, int x,
                        int y, int channel) {
  return data[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)) *
                  3 +
              static_cast<std::size_t>(channel)];
}

bool is_box_pixel(const std::vector<std::uint8_t>& data, int width, int x,
                  int y) {
  return channel_at(data, width, x, y, 0) == 0 &&
         channel_at(data, width, x, y, 1) == 255 &&
         channel_at(data, width, x, y, 2) == 0;
}

bool is_label_pixel(const std::vector<std::uint8_t>& data, int width, int x,
                    int y) {
  return channel_at(data, width, x, y, 0) == 255 &&
         channel_at(data, width, x, y, 1) == 255 &&
         channel_at(data, width, x, y, 2) == 0;
}

TEST(OsdAnnotator, ClipsBoxesToFrameBounds) {
  FrameMeta src = make_frame(32, 32, kBackground);
  const std::vector<std::uint8_t> original = src.rgb_data;
  std::vector<std::uint8_t> dst = src.rgb_data;

  // A box entirely beyond the right edge must not modify the buffer.
  TargetBox outside;
  outside.label = "x";
  outside.score = 0.5f;
  outside.x = 2.0f;
  outside.y = 0.25f;
  outside.w = 0.5f;
  outside.h = 0.5f;
  edge::draw_osd(src, {outside}, dst);
  EXPECT_EQ(dst, original);

  // A box crossing the right and bottom edges is clipped to 24..31.
  TargetBox clipped;
  clipped.label = "x";
  clipped.score = 0.5f;
  clipped.x = 0.75f;
  clipped.y = 0.75f;
  clipped.w = 0.5f;
  clipped.h = 0.5f;
  edge::draw_osd(src, {clipped}, dst);

  EXPECT_EQ(dst.size(), src.rgb_data.size());
  for (int i = 24; i < 32; ++i) {
    EXPECT_TRUE(is_box_pixel(dst, src.width, i, 31)) << "bottom edge at x=" << i;
    EXPECT_TRUE(is_box_pixel(dst, src.width, 31, i)) << "right edge at y=" << i;
    EXPECT_TRUE(is_box_pixel(dst, src.width, i, 24)) << "top edge at x=" << i;
    EXPECT_TRUE(is_box_pixel(dst, src.width, 24, i)) << "left edge at y=" << i;
  }
  EXPECT_FALSE(is_box_pixel(dst, src.width, 29, 29));  // interior untouched
  EXPECT_EQ(channel_at(dst, src.width, 23, 25, 0), kBackground);
  EXPECT_EQ(channel_at(dst, src.width, 25, 23, 0), kBackground);
  EXPECT_EQ(channel_at(dst, src.width, 0, 0, 0), kBackground);

  // A box starting above and left of the frame is clipped from 0..15.
  TargetBox negative;
  negative.label = "x";
  negative.score = 0.5f;
  negative.x = -0.5f;
  negative.y = -0.5f;
  negative.w = 1.0f;
  negative.h = 1.0f;
  edge::draw_osd(src, {negative}, dst);

  EXPECT_TRUE(is_box_pixel(dst, src.width, 14, 14));
  EXPECT_TRUE(is_box_pixel(dst, src.width, 15, 15));
  EXPECT_FALSE(is_box_pixel(dst, src.width, 8, 8));
  EXPECT_EQ(channel_at(dst, src.width, 16, 16, 0), kBackground);
  EXPECT_EQ(channel_at(dst, src.width, 0, 16, 0), kBackground);
  EXPECT_EQ(channel_at(dst, src.width, 16, 0, 0), kBackground);

  EXPECT_EQ(src.rgb_data, original);
}

TEST(OsdAnnotator, DrawsLabelWithExpectedPixels) {
  FrameMeta src = make_frame(64, 64, kBackground);
  const std::vector<std::uint8_t> original = src.rgb_data;
  std::vector<std::uint8_t> dst = src.rgb_data;

  // Box occupies 32..47. The "person 0.87" label is 88px wide, so it is
  // shifted to x=0 and clipped at the right frame edge.
  TargetBox target;
  target.label = "person";
  target.score = 0.87f;
  target.x = 0.5f;
  target.y = 0.5f;
  target.w = 0.25f;
  target.h = 0.25f;
  edge::draw_osd(src, {target}, dst);

  // Label baseline is 24; the first glyph is 'p'. Its row 2 has bit 0 set,
  // so (0, 26) must contain the label colour while rows 0 and 1 stay blank.
  EXPECT_TRUE(is_label_pixel(dst, src.width, 0, 26));
  EXPECT_EQ(channel_at(dst, src.width, 0, 24, 0), kBackground);
  EXPECT_EQ(channel_at(dst, src.width, 0, 25, 0), kBackground);

  EXPECT_TRUE(is_box_pixel(dst, src.width, 32, 32));
  EXPECT_TRUE(is_box_pixel(dst, src.width, 47, 47));
  EXPECT_FALSE(is_box_pixel(dst, src.width, 40, 40));
  EXPECT_EQ(channel_at(dst, src.width, 0, 0, 0), kBackground);

  EXPECT_EQ(src.rgb_data, original);
}

TEST(OsdAnnotator, EmptyTargetsLeaveBufferUntouched) {
  FrameMeta src = make_frame(8, 8, 0x40);
  const std::vector<std::uint8_t> original = src.rgb_data;
  std::vector<std::uint8_t> dst(original.size(), 0xAB);
  const std::vector<std::uint8_t> sentinel = dst;

  edge::draw_osd(src, {}, dst);

  EXPECT_EQ(dst, sentinel);
  EXPECT_EQ(src.rgb_data, original);
}

TEST(StreamOutNode, SkipsEncodingWithoutDemand) {
  TestStreamOutNode node;
  auto frame = std::make_shared<FrameMeta>(make_frame(32, 32, 0x20));

  auto output = node.process(frame);

  EXPECT_EQ(output.get(), frame.get());
  EXPECT_EQ(node.encoded_frames(), 0U);
  EXPECT_FALSE(node.latest_jpeg());
}

TEST(StreamOutNode, PublishesJpegForSubscriberUntilUnsubscribe) {
  TestStreamOutNode node;
  node.subscribe();

  auto frame = std::make_shared<FrameMeta>(make_frame(32, 32, 0x20));
  TargetBox target;
  target.label = "person";
  target.score = 0.87f;
  target.x = 0.25f;
  target.y = 0.25f;
  target.w = 0.5f;
  target.h = 0.5f;
  frame->targets.push_back(target);

  auto output = node.process(frame);

  EXPECT_NE(output, nullptr);
  EXPECT_EQ(node.encoded_frames(), 1U);
  auto jpeg = node.latest_jpeg();
  ASSERT_TRUE(jpeg);
  ASSERT_GE(jpeg->size(), 3U);
  EXPECT_EQ((*jpeg)[0], 0xFF);
  EXPECT_EQ((*jpeg)[1], 0xD8);
  EXPECT_EQ((*jpeg)[2], 0xFF);

  node.unsubscribe();
  node.process(std::make_shared<FrameMeta>(make_frame(32, 32, 0x30)));
  EXPECT_EQ(node.encoded_frames(), 1U);
}

TEST(StreamOutNode, ConsumesSnapshotRequestOnlyOnce) {
  TestStreamOutNode node;
  node.request_snapshot();
  EXPECT_TRUE(node.snapshot_pending());

  node.process(std::make_shared<FrameMeta>(make_frame(16, 16, 0x10)));
  EXPECT_FALSE(node.snapshot_pending());
  EXPECT_EQ(node.encoded_frames(), 1U);
  auto first_jpeg = node.latest_jpeg();
  ASSERT_TRUE(first_jpeg);

  // With no subscribers left and the request consumed, this frame is passed
  // through without a second encode or a new JPEG.
  node.process(std::make_shared<FrameMeta>(make_frame(16, 16, 0x30)));
  EXPECT_EQ(node.encoded_frames(), 1U);
  EXPECT_EQ(node.latest_jpeg(), first_jpeg);
}

}  // namespace
