#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/frame_meta.hpp"
#include "streamer/osd_annotator.hpp"
#include "streamer/rtsp_pusher.hpp"
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

// Records the ffmpeg command line and simulates its stdin. When `drain` is
// false every write reports a full pipe, which exercises bounded-queue drops.
// Drained writes are captured so tests can inspect the pushed RGB24 frame.
class FakeProcessSink final : public edge::ProcessSink {
 public:
  explicit FakeProcessSink(bool drain, bool fail_start = false)
      : drain_(drain), fail_start_(fail_start) {}

  bool Start(const std::vector<std::string>& command,
             std::string* error) override {
    command_ = command;
    ++start_attempts;
    if (fail_start_) {
      if (error != nullptr) {
        *error = "fake start failure";
      }
      return false;
    }
    ++start_calls;
    active.fetch_add(1);
    return true;
  }

  std::ptrdiff_t Write(const std::uint8_t* data, std::size_t size) override {
    if (!drain_) {
      return 0;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      received_.insert(received_.end(), data, data + size);
    }
    bytes_written.fetch_add(size);
    cv_.notify_all();
    return static_cast<std::ptrdiff_t>(size);
  }

  void CloseInput() override { ++close_calls; }

  int Wait() override {
    ++wait_calls;
    if (active.load() > 0) {
      active.fetch_sub(1);
    }
    return 0;
  }

  bool wait_for_bytes(std::size_t size, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout,
                        [this, size] { return received_.size() >= size; });
  }

  std::vector<std::uint8_t> received() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_;
  }

  std::vector<std::string> command_;
  const bool drain_;
  const bool fail_start_;
  int start_attempts = 0;
  int start_calls = 0;
  int close_calls = 0;
  int wait_calls = 0;
  std::atomic<int> active{0};
  std::atomic<std::size_t> bytes_written{0};

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::uint8_t> received_;
};

bool has_pair(const std::vector<std::string>& args, const std::string& flag,
              const std::string& value) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == flag && args[i + 1] == value) {
      return true;
    }
  }
  return false;
}

TEST(RtspPusher, MjpegModeDoesNotLaunchProcess) {
  auto sink = std::make_shared<FakeProcessSink>(true);
  FrameMeta frame = make_frame(32, 24, 0x10);
  edge::RtspPusher pusher("mjpeg", "rtsp://127.0.0.1:8554/edge", 32, 24, 15,
                          "ffmpeg", sink);

  EXPECT_FALSE(pusher.start());
  EXPECT_EQ(sink->start_calls, 0);
  EXPECT_EQ(sink->active.load(), 0);

  pusher.push(frame);
  EXPECT_EQ(pusher.dropped_frames(), 0u);

  pusher.stop();
  EXPECT_EQ(sink->wait_calls, 0);
}

TEST(RtspPusher, RtspCommandContainsRequiredArguments) {
  auto sink = std::make_shared<FakeProcessSink>(true);
  edge::RtspPusher pusher("rtsp", "rtsp://127.0.0.1:8554/edge", 320, 240, 15,
                          "/usr/bin/ffmpeg", sink);
  ASSERT_TRUE(pusher.start());
  pusher.push(make_frame(320, 240, 0));
  pusher.stop();

  const std::vector<std::string>& args = sink->command_;
  ASSERT_FALSE(args.empty());
  EXPECT_EQ(args.front(), "/usr/bin/ffmpeg");
  EXPECT_TRUE(has_pair(args, "-f", "rawvideo"));
  EXPECT_TRUE(has_pair(args, "-pix_fmt", "rgb24"));
  EXPECT_TRUE(has_pair(args, "-s", "320x240"));
  EXPECT_TRUE(has_pair(args, "-r", "15"));
  EXPECT_TRUE(has_pair(args, "-i", "-"));
  EXPECT_TRUE(has_pair(args, "-c:v", "libx264"));
  EXPECT_TRUE(has_pair(args, "-preset", "ultrafast"));
  EXPECT_TRUE(has_pair(args, "-tune", "zerolatency"));
  EXPECT_TRUE(has_pair(args, "-b:v", "500k"));
  EXPECT_TRUE(has_pair(args, "-pix_fmt", "yuv420p"));
  EXPECT_TRUE(has_pair(args, "-f", "rtsp"));
  EXPECT_TRUE(has_pair(args, "-rtsp_transport", "tcp"));
  EXPECT_EQ(args.back(), "rtsp://127.0.0.1:8554/edge");
  EXPECT_EQ(sink->wait_calls, 1);
  EXPECT_EQ(sink->active.load(), 0);
}

TEST(RtspPusher, StopReapsProcessAndFullPipeDropsFrames) {
  auto sink = std::make_shared<FakeProcessSink>(false);
  edge::RtspPusher pusher("rtsp", "rtsp://127.0.0.1:8554/edge", 4, 4, 10,
                          "ffmpeg", sink);
  ASSERT_TRUE(pusher.start());
  EXPECT_EQ(sink->active.load(), 1);

  EXPECT_TRUE(pusher.start());  // repeated start must not spawn again
  EXPECT_EQ(sink->start_calls, 1);

  FrameMeta frame = make_frame(4, 4, 0x7f);
  for (int i = 0; i < 20; ++i) {
    pusher.push(frame);
  }
  EXPECT_GT(pusher.dropped_frames(), 0u);

  pusher.stop();
  EXPECT_EQ(sink->close_calls, 1);
  EXPECT_EQ(sink->wait_calls, 1);
  EXPECT_EQ(sink->active.load(), 0);

  pusher.stop();  // idempotent
  EXPECT_EQ(sink->wait_calls, 1);
  EXPECT_EQ(sink->close_calls, 1);
}

TEST(StreamOutNodeRtsp, PushesOsdFrameAndLeavesSourceUntouched) {
  auto sink = std::make_shared<FakeProcessSink>(true);
  auto pusher = std::make_shared<edge::RtspPusher>(
      "rtsp", "rtsp://127.0.0.1:8554/edge", 32, 32, 10, "ffmpeg", sink);
  ASSERT_TRUE(pusher->start());

  TestStreamOutNode node(2, pusher);
  auto frame = std::make_shared<FrameMeta>(make_frame(32, 32, 0x20));
  TargetBox target;
  target.label = "person";
  target.score = 0.87f;
  target.x = 0.25f;
  target.y = 0.25f;
  target.w = 0.5f;
  target.h = 0.5f;
  frame->targets.push_back(target);
  const std::vector<std::uint8_t> original = frame->rgb_data;

  node.process(frame);

  const std::size_t expected = 32U * 32U * 3U;
  ASSERT_TRUE(sink->wait_for_bytes(expected, std::chrono::seconds(2)));
  const std::vector<std::uint8_t> pushed = sink->received();
  ASSERT_EQ(pushed.size(), expected);
  // The OSD box starts at x=8; only the pushed copy may contain it.
  EXPECT_TRUE(is_box_pixel(pushed, 32, 8, 16));
  EXPECT_FALSE(is_box_pixel(frame->rgb_data, 32, 8, 16));
  EXPECT_EQ(frame->rgb_data, original);
  EXPECT_EQ(node.encoded_frames(), 0U);  // no subscriber: no MJPEG encode

  node.stop();
  pusher->stop();
  EXPECT_EQ(sink->wait_calls, 1);
}

TEST(StreamOutNodeRtsp, StartFailureFallsBackToMjpeg) {
  auto sink = std::make_shared<FakeProcessSink>(true, true);
  auto pusher = std::make_shared<edge::RtspPusher>(
      "rtsp", "rtsp://127.0.0.1:8554/edge", 16, 16, 10, "ffmpeg", sink);
  ASSERT_FALSE(pusher->start());
  EXPECT_FALSE(pusher->running());

  TestStreamOutNode node(2, pusher);
  node.subscribe();
  node.process(std::make_shared<FrameMeta>(make_frame(16, 16, 0x30)));

  EXPECT_EQ(sink->start_attempts, 1);
  EXPECT_EQ(sink->bytes_written.load(), 0U);
  EXPECT_EQ(node.encoded_frames(), 1U);
  ASSERT_TRUE(node.latest_jpeg());

  node.stop();
  pusher->stop();
}

}  // namespace
