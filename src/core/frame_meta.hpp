#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edge {

struct TargetBox {
  int class_id = -1;
  std::string label;
  float score = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct FrameMeta {
  std::uint64_t frame_id = 0;
  std::int64_t timestamp_ms = 0;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb_data;  // Filled by CaptureNode; read-only downstream.
  bool has_motion = false;             // Filled by MotionNode.
  float motion_score = 0.0f;
  std::vector<TargetBox> targets;  // Filled by NcnnDetectorNode.
  double infer_ms = 0.0;
  std::string active_event_id;  // Filled by EventTrackerNode.
};

using FrameMetaPtr = std::shared_ptr<FrameMeta>;

}  // namespace edge
