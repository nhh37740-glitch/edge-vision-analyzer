#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/base_node.hpp"
#include "core/frame_meta.hpp"

namespace edge {

class RtspPusher;

// Encodes an OSD-annotated JPEG only while somebody is watching or a snapshot
// was requested. When an RtspPusher is injected (config.streamer.mode=rtsp),
// every frame is also rendered with OSD into a worker-owned buffer and pushed
// as RGB24; the shared source frame is never modified. Viewers and snapshot
// callers come from API threads while process() runs on the node worker
// thread, so the demand state and the latest JPEG pointer are guarded by one
// mutex. Slow clients only read the newest image; the node never keeps
// per-client queues.
class StreamOutNode : public BaseNode {
 public:
  explicit StreamOutNode(std::size_t queue_capacity = 2,
                         std::shared_ptr<RtspPusher> rtsp_pusher = nullptr);
  ~StreamOutNode() override;

  // Called by the MJPEG endpoint for each attached client.
  void subscribe();
  void unsubscribe();

  // One-shot request consumed by the next encoded frame.
  void request_snapshot();

  std::shared_ptr<const std::vector<std::uint8_t>> latest_jpeg() const;
  std::uint64_t latest_jpeg_seq() const;

  // Waits until a JPEG newer than after_seq is published, the timeout elapses,
  // or the node stops. Returns nullptr on timeout or shutdown so callers never
  // block forever.
  std::shared_ptr<const std::vector<std::uint8_t>> wait_for_jpeg(
      std::uint64_t after_seq, std::chrono::milliseconds timeout) const;

  // Successful encodes so far; used by tests and metrics.
  std::uint64_t encoded_frames() const {
    return encoded_frames_.load(std::memory_order_relaxed);
  }

  bool snapshot_pending() const;

  // Hides BaseNode::stop() so pending wait_for_jpeg() callers wake up.
  void stop();

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override;

 private:
  bool prepare_osd(const FrameMeta& meta);
  void publish_jpeg(const FrameMeta& meta);

  mutable std::mutex mutex_;
  mutable std::condition_variable jpeg_cv_;
  std::size_t subscribers_ = 0;
  bool snapshot_pending_ = false;
  std::shared_ptr<const std::vector<std::uint8_t>> latest_jpeg_;
  std::uint64_t latest_jpeg_seq_ = 0;
  std::atomic<std::uint64_t> encoded_frames_{0};
  std::shared_ptr<RtspPusher> rtsp_pusher_;
  std::vector<std::uint8_t> osd_buffer_;  // Worker thread only; reused per frame.
};

}  // namespace edge
