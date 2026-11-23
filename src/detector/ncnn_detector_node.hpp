#pragma once

#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/config.hpp"
#include "core/base_node.hpp"
#include "core/frame_meta.hpp"
#include "detector/model_selection.hpp"

namespace ncnn {
class Net;
}  // namespace ncnn

namespace edge {

class Metrics;

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
    // "auto" selects fp32/fp16/int8 from model_candidates using the detected
    // device resources; explicit values request one quantization.
    std::string quantization = "auto";
    std::map<std::string, ModelCandidate> model_candidates;
    // Bounds the per-frame candidate list and emitted targets on 2GB devices.
    std::size_t max_candidates = 512;
    std::size_t max_targets = 64;
  };

  explicit NcnnDetectorNode(Options options,
                            std::shared_ptr<Metrics> metrics = nullptr);
  ~NcnnDetectorNode() override;

  NcnnDetectorNode(const NcnnDetectorNode&) = delete;
  NcnnDetectorNode& operator=(const NcnnDetectorNode&) = delete;

  // Loads the model selected from Options::quantization and
  // Options::model_candidates. Returns false when the base model is unusable or
  // empty; the node then passes every frame through.
  bool load();
  bool loaded() const { return loaded_; }
  // Quantization that actually loaded; empty before a successful load().
  const std::string& effective_quantization() const {
    return effective_quantization_;
  }
  // Replaces the default probe for tests and deterministic callers.
  void set_device_resources(const DeviceResources& resources) {
    device_resources_ = resources;
    has_device_resources_ = true;
  }

  // Runtime update for PATCH /api/v1/config; read once per processed frame.
  void set_conf_threshold(float conf_threshold) {
    conf_threshold_.store(conf_threshold);
  }
  float conf_threshold() const { return conf_threshold_.load(); }

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override;

 private:
  bool load_net(const std::string& param, const std::string& bin,
                const std::string& quantization);

  Options options_;
  std::atomic<float> conf_threshold_;
  std::shared_ptr<Metrics> metrics_;
  std::unique_ptr<ncnn::Net> net_;
  std::string input_name_;
  std::vector<std::string> output_names_;
  std::string effective_quantization_;
  DeviceResources device_resources_;
  bool has_device_resources_ = false;
  bool loaded_ = false;
};

}  // namespace edge
