#include "detector/motion_node.hpp"

#include <utility>

#include "common/metrics.hpp"

namespace edge {

MotionNode::MotionNode(double mad_threshold, std::shared_ptr<Metrics> metrics)
    : BaseNode("motion"), threshold_(mad_threshold), metrics_(std::move(metrics)) {}

bool MotionNode::downsample(const FrameMeta& meta,
                            std::array<std::uint8_t, kSampleCount>& gray) const {
  if (meta.width <= 0 || meta.height <= 0) {
    return false;
  }
  const std::size_t required = static_cast<std::size_t>(meta.width) *
                               static_cast<std::size_t>(meta.height) * 3;
  if (meta.rgb_data.size() < required) {
    return false;
  }

  // Strided sampling keeps the cost independent of the captured resolution.
  for (int y = 0; y < kHeight; ++y) {
    const std::size_t source_y = static_cast<std::size_t>(y) *
                                 static_cast<std::size_t>(meta.height) /
                                 static_cast<std::size_t>(kHeight);
    for (int x = 0; x < kWidth; ++x) {
      const std::size_t source_x = static_cast<std::size_t>(x) *
                                   static_cast<std::size_t>(meta.width) /
                                   static_cast<std::size_t>(kWidth);
      const std::size_t offset =
          (source_y * static_cast<std::size_t>(meta.width) + source_x) * 3;
      const unsigned int r = meta.rgb_data[offset];
      const unsigned int g = meta.rgb_data[offset + 1];
      const unsigned int b = meta.rgb_data[offset + 2];
      gray[static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>((77u * r + 150u * g + 29u * b) >> 8);
    }
  }
  return true;
}

FrameMetaPtr MotionNode::process(FrameMetaPtr meta) {
  if (!meta) {
    return meta;
  }
  meta->has_motion = false;
  meta->motion_score = 0.0f;

  std::array<std::uint8_t, kSampleCount> gray{};
  if (!downsample(*meta, gray)) {
    // The frame cannot reach detection, so it belongs on the skipped side of
    // the ratio.
    if (metrics_) {
      metrics_->on_motion_skip();
    }
    return meta;
  }

  if (!has_previous_) {
    // No baseline exists yet; store it and treat the frame as motion so the
    // first detection stage sees a real frame.
    previous_ = gray;
    has_previous_ = true;
    meta->has_motion = true;
    if (metrics_) {
      metrics_->on_detect_frame();
    }
    return meta;
  }

  std::uint32_t total = 0;
  for (std::size_t i = 0; i < kSampleCount; ++i) {
    const int diff = static_cast<int>(gray[i]) - static_cast<int>(previous_[i]);
    total += static_cast<std::uint32_t>(diff < 0 ? -diff : diff);
  }
  const double mad = static_cast<double>(total) / static_cast<double>(kSampleCount);
  previous_ = gray;

  meta->motion_score = static_cast<float>(mad);
  meta->has_motion = mad >= threshold_.load();
  if (metrics_) {
    // Every non-null frame lands in exactly one bucket, keeping
    // motion_skip_ratio = skips / (skips + detects) consistent.
    if (meta->has_motion) {
      metrics_->on_detect_frame();
    } else {
      metrics_->on_motion_skip();
    }
  }
  return meta;
}

}  // namespace edge
