#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/base_node.hpp"

namespace edge {

// Frame-difference funnel: downsamples RGB24 to a 64x48 grayscale sample and
// compares it with the previous frame using mean absolute difference. Static
// frames still flow downstream but carry has_motion=false so the detector can
// be skipped. The first valid frame has no baseline and is marked as motion.
class MotionNode : public BaseNode {
 public:
  static constexpr int kWidth = 64;
  static constexpr int kHeight = 48;

  explicit MotionNode(double mad_threshold = 8.0);

  // Runtime update for PATCH /api/v1/config; safe from another thread.
  void set_threshold(double mad_threshold) { threshold_.store(mad_threshold); }
  double threshold() const { return threshold_.load(); }

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override;

 private:
  static constexpr std::size_t kSampleCount =
      static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

  bool downsample(const FrameMeta& meta,
                  std::array<std::uint8_t, kSampleCount>& gray) const;

  std::atomic<double> threshold_;
  std::array<std::uint8_t, kSampleCount> previous_{};
  bool has_previous_ = false;
};

}  // namespace edge
