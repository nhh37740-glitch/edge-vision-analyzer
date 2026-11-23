#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/frame_meta.hpp"

namespace edge::detection {

// Offline post-processing helpers shared by NcnnDetectorNode. They operate on
// normalized TargetBox values only and never touch ncnn, so unit tests can
// exercise class/score filtering and NMS without loading a model.

inline float iou(const TargetBox& first, const TargetBox& second) {
  const float first_x2 = first.x + first.w;
  const float first_y2 = first.y + first.h;
  const float second_x2 = second.x + second.w;
  const float second_y2 = second.y + second.h;

  const float overlap_width =
      std::min(first_x2, second_x2) - std::max(first.x, second.x);
  const float overlap_height =
      std::min(first_y2, second_y2) - std::max(first.y, second.y);
  if (overlap_width <= 0.0f || overlap_height <= 0.0f) {
    return 0.0f;
  }

  const float intersection = overlap_width * overlap_height;
  const float first_area = first.w * first.h;
  const float second_area = second.w * second.h;
  const float union_area = first_area + second_area - intersection;
  return union_area > 0.0f ? intersection / union_area : 0.0f;
}

// Keeps boxes whose score reaches conf_threshold and whose label is listed in
// target_labels. An empty target_labels list means "all labels".
inline std::vector<TargetBox> filter_detections(
    const std::vector<TargetBox>& boxes,
    const std::vector<std::string>& target_labels,
    float conf_threshold) {
  std::vector<TargetBox> kept;
  kept.reserve(boxes.size());
  for (const TargetBox& box : boxes) {
    if (box.score < conf_threshold) {
      continue;
    }
    if (!target_labels.empty() &&
        std::find(target_labels.begin(), target_labels.end(), box.label) ==
            target_labels.end()) {
      continue;
    }
    kept.push_back(box);
  }
  return kept;
}

// Greedy class-aware non-maximum suppression. Boxes are visited in descending
// score order and a lower-score box is dropped when it overlaps the same class
// above iou_threshold. max_output == 0 keeps every surviving box.
inline std::vector<TargetBox> nms(std::vector<TargetBox> boxes,
                                  float iou_threshold,
                                  std::size_t max_output = 0) {
  std::stable_sort(boxes.begin(), boxes.end(),
                   [](const TargetBox& first, const TargetBox& second) {
                     return first.score > second.score;
                   });

  std::vector<bool> suppressed(boxes.size(), false);
  std::vector<TargetBox> kept;
  kept.reserve(max_output == 0 ? boxes.size() : max_output);

  for (std::size_t i = 0; i < boxes.size(); ++i) {
    if (suppressed[i]) {
      continue;
    }
    kept.push_back(boxes[i]);
    if (max_output != 0 && kept.size() >= max_output) {
      break;
    }
    for (std::size_t j = i + 1; j < boxes.size(); ++j) {
      if (suppressed[j] || boxes[i].class_id != boxes[j].class_id) {
        continue;
      }
      if (iou(boxes[i], boxes[j]) > iou_threshold) {
        suppressed[j] = true;
      }
    }
  }
  return kept;
}

}  // namespace edge::detection
