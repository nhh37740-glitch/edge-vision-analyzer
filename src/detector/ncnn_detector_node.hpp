#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/base_node.hpp"
#include "core/frame_meta.hpp"

namespace ncnn {
class Net;
}  // namespace ncnn

namespace edge {

// Runs YOLO-Fastest-v2 style ncnn detection only on frames flagged with
// has_motion; other frames pass through untouched. Input frames are letterboxed
// to 320x320, decoded to normalized TargetBox values, filtered by label and
// confidence, then reduced with class-aware NMS. No frame history is retained.
class NcnnDetectorNode : public BaseNode {
 public:
  struct Options {
    std::string model_param;
    std::string model_bin;
    std::vector<std::string> target_labels;
    // Empty means COCO-80, matching the YOLO-Fastest-v2 model.
    std::vector<std::string> class_names;
    float conf_threshold = 0.5f;
    float nms_threshold = 0.45f;
    int input_size = 320;
    int num_threads = 2;
    // Bounds the per-frame candidate list and emitted targets on 2GB devices.
    std::size_t max_candidates = 512;
    std::size_t max_targets = 64;
  };

  explicit NcnnDetectorNode(Options options);
  ~NcnnDetectorNode() override;

  NcnnDetectorNode(const NcnnDetectorNode&) = delete;
  NcnnDetectorNode& operator=(const NcnnDetectorNode&) = delete;

  // Loads .param/.bin. Returns false when paths are empty, files are missing or
  // ncnn rejects the model; the node then passes every frame through.
  bool load();
  bool loaded() const { return loaded_; }

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override;

 private:
  Options options_;
  std::unique_ptr<ncnn::Net> net_;
  std::string input_name_;
  std::vector<std::string> output_names_;
  bool loaded_ = false;
};

}  // namespace edge
