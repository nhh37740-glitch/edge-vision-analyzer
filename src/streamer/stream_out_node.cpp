#include "streamer/stream_out_node.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Keep the stb implementation local to this translation unit. edge_capture
// defines the non-static implementation for frame dumping; STB_IMAGE_WRITE_STATIC
// gives this file internal-linkage copies so both libraries can be linked into
// the same executable without duplicate symbols.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "streamer/osd_annotator.hpp"
#include "streamer/rtsp_pusher.hpp"

namespace edge {
namespace {

constexpr int kJpegQuality = 80;

void append_jpeg_bytes(void* context, void* data, int size) {
  auto* output = static_cast<std::vector<std::uint8_t>*>(context);
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  output->insert(output->end(), bytes, bytes + size);
}

}  // namespace

StreamOutNode::StreamOutNode(std::size_t queue_capacity,
                             std::shared_ptr<RtspPusher> rtsp_pusher)
    : BaseNode("StreamOutNode", queue_capacity),
      rtsp_pusher_(std::move(rtsp_pusher)) {}

StreamOutNode::~StreamOutNode() { stop(); }

void StreamOutNode::subscribe() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++subscribers_;
}

void StreamOutNode::unsubscribe() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (subscribers_ > 0) {
    --subscribers_;
  }
}

void StreamOutNode::request_snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_pending_ = true;
}

std::shared_ptr<const std::vector<std::uint8_t>> StreamOutNode::latest_jpeg()
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_jpeg_;
}

std::uint64_t StreamOutNode::latest_jpeg_seq() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_jpeg_seq_;
}

std::shared_ptr<const std::vector<std::uint8_t>>
StreamOutNode::wait_for_jpeg(std::uint64_t after_seq,
                             std::chrono::milliseconds timeout) const {
  std::unique_lock<std::mutex> lock(mutex_);
  jpeg_cv_.wait_for(lock, timeout, [this, after_seq] {
    return latest_jpeg_seq_ > after_seq || !running();
  });
  if (latest_jpeg_seq_ > after_seq) {
    return latest_jpeg_;
  }
  return nullptr;
}

bool StreamOutNode::snapshot_pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_pending_;
}

void StreamOutNode::stop() {
  BaseNode::stop();
  jpeg_cv_.notify_all();
}

FrameMetaPtr StreamOutNode::process(FrameMetaPtr meta) {
  if (!meta) {
    return meta;
  }

  // An injected pusher only counts as active once its writer is running. If
  // the ffmpeg launch failed, fall back to the on-demand MJPEG path instead of
  // rendering OSD that nobody consumes.
  const bool rtsp = rtsp_pusher_ != nullptr && rtsp_pusher_->running();

  bool demanded = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    demanded = subscribers_ > 0 || snapshot_pending_;
  }
  if (!rtsp && !demanded) {
    return meta;
  }

  if (!prepare_osd(*meta)) {
    return meta;
  }

  if (rtsp) {
    // push() copies into the pusher's bounded queue, so the shared frame stays
    // read-only and the node thread never waits on the encoder.
    rtsp_pusher_->push(osd_buffer_);
  }
  if (demanded) {
    publish_jpeg(*meta);
  }
  return meta;
}

bool StreamOutNode::prepare_osd(const FrameMeta& meta) {
  if (meta.width <= 0 || meta.height <= 0) {
    return false;
  }
  const std::size_t expected =
      static_cast<std::size_t>(meta.width) * static_cast<std::size_t>(meta.height) * 3;
  if (meta.rgb_data.size() != expected) {
    return false;
  }

  // Reuse the worker-owned annotation buffer so a steady frame size performs
  // no per-frame RGB allocation.
  if (osd_buffer_.size() != meta.rgb_data.size()) {
    osd_buffer_.resize(meta.rgb_data.size());
  }
  std::copy(meta.rgb_data.begin(), meta.rgb_data.end(), osd_buffer_.begin());
  draw_osd(meta, meta.targets, osd_buffer_);
  return true;
}

void StreamOutNode::publish_jpeg(const FrameMeta& meta) {
  std::vector<std::uint8_t> jpeg;
  if (stbi_write_jpg_to_func(&append_jpeg_bytes, &jpeg, meta.width, meta.height,
                             3, osd_buffer_.data(), kJpegQuality) == 0) {
    return;
  }

  encoded_frames_.fetch_add(1, std::memory_order_relaxed);
  auto published =
      std::make_shared<const std::vector<std::uint8_t>>(std::move(jpeg));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_jpeg_ = std::move(published);
    ++latest_jpeg_seq_;
    // Subscribers and snapshot callers share this single encode.
    snapshot_pending_ = false;
  }
  jpeg_cv_.notify_all();
}

}  // namespace edge
