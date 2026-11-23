#include "detector/ncnn_detector_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

#include "common/metrics.hpp"
#include "detector/detection_utils.hpp"
#include "mat.h"
#include "net.h"

namespace edge {
namespace {

constexpr int kAnchorsPerOutput = 3;
constexpr int kOutputSets = 2;
constexpr int kInputChannels = 3;
constexpr float kPadValue = 114.0f;

// Anchor pairs used by the YOLO-Fastest-v2 COCO model. The first three pairs
// belong to the stride-16 output and the last three to stride-32.
constexpr float kAnchors[kAnchorsPerOutput * kOutputSets][2] = {
    {12.64f, 19.39f}, {37.88f, 51.48f}, {55.71f, 138.31f},
    {126.91f, 78.23f}, {131.57f, 214.55f}, {279.92f, 258.87f}};

constexpr const char* kCocoLabels[] = {
    "person",        "bicycle",      "car",           "motorcycle",
    "airplane",      "bus",          "train",         "truck",
    "boat",          "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench",        "bird",          "cat",
    "dog",           "horse",        "sheep",         "cow",
    "elephant",      "bear",         "zebra",         "giraffe",
    "backpack",      "umbrella",     "handbag",       "tie",
    "suitcase",      "frisbee",      "skis",          "snowboard",
    "sports ball",   "kite",         "baseball bat",  "baseball glove",
    "skateboard",    "surfboard",    "tennis racket", "bottle",
    "wine glass",    "cup",          "fork",          "knife",
    "spoon",         "bowl",         "banana",        "apple",
    "sandwich",      "orange",       "broccoli",      "carrot",
    "hot dog",       "pizza",        "donut",         "cake",
    "chair",         "couch",        "potted plant",  "bed",
    "dining table",  "toilet",       "tv",            "laptop",
    "mouse",         "remote",       "keyboard",      "cell phone",
    "microwave",     "oven",         "toaster",       "sink",
    "refrigerator",  "book",         "clock",         "vase",
    "scissors",      "teddy bear",   "hair drier",    "toothbrush"};

// Decodes one raw YOLO-Fastest-v2 output tensor, whose layout is
// width=channels, height=grid width, depth=grid height. Coordinates are mapped
// back through the letterbox transform and normalized to the original frame.
void decode_output(const ncnn::Mat& out, int input_size, float scale, int pad_x,
                   int pad_y, int original_width, int original_height,
                   float conf_threshold, const std::vector<std::string>& class_names,
                   std::size_t max_candidates, std::vector<TargetBox>& boxes) {
  const int channels = out.w;
  const int grid_width = out.h;
  const int grid_height = out.c;
  const int class_count = channels - 4 * kAnchorsPerOutput - kAnchorsPerOutput;
  if (class_count <= 0 || grid_width <= 0 || grid_height <= 0) {
    return;
  }

  const int stride = input_size / grid_height;
  int anchor_base = -1;
  if (stride == 16) {
    anchor_base = 0;
  } else if (stride == 32) {
    anchor_base = kAnchorsPerOutput;
  } else {
    return;
  }

  for (int gy = 0; gy < grid_height; ++gy) {
    const float* values = out.channel(gy);
    for (int gx = 0; gx < grid_width; ++gx) {
      for (int anchor = 0; anchor < kAnchorsPerOutput; ++anchor) {
        const float objectness = values[4 * kAnchorsPerOutput + anchor];
        int best_class = -1;
        float best_score = 0.0f;
        for (int c = 0; c < class_count; ++c) {
          const float score =
              values[4 * kAnchorsPerOutput + kAnchorsPerOutput + c] * objectness;
          if (score > best_score) {
            best_score = score;
            best_class = c;
          }
        }

        if (best_score >= conf_threshold) {
          if (boxes.size() >= max_candidates) {
            return;
          }

          const float* anchor_size =
              kAnchors[anchor_base + anchor];
          const float center_x =
              (values[anchor * 4 + 0] * 2.0f - 0.5f + static_cast<float>(gx)) *
              static_cast<float>(stride);
          const float center_y =
              (values[anchor * 4 + 1] * 2.0f - 0.5f + static_cast<float>(gy)) *
              static_cast<float>(stride);
          const float box_width =
              std::pow(values[anchor * 4 + 2] * 2.0f, 2.0f) * anchor_size[0];
          const float box_height =
              std::pow(values[anchor * 4 + 3] * 2.0f, 2.0f) * anchor_size[1];

          // Undo letterbox padding and scaling, then clamp to the frame.
          float x1 = (center_x - 0.5f * box_width - pad_x) / scale;
          float y1 = (center_y - 0.5f * box_height - pad_y) / scale;
          float x2 = (center_x + 0.5f * box_width - pad_x) / scale;
          float y2 = (center_y + 0.5f * box_height - pad_y) / scale;
          x1 = std::clamp(x1, 0.0f, static_cast<float>(original_width));
          y1 = std::clamp(y1, 0.0f, static_cast<float>(original_height));
          x2 = std::clamp(x2, 0.0f, static_cast<float>(original_width));
          y2 = std::clamp(y2, 0.0f, static_cast<float>(original_height));
          if (x2 <= x1 || y2 <= y1) {
            continue;
          }

          TargetBox box;
          box.class_id = best_class;
          box.label = best_class < static_cast<int>(class_names.size())
                          ? class_names[static_cast<std::size_t>(best_class)]
                          : "class_" + std::to_string(best_class);
          box.score = best_score;
          box.x = x1 / static_cast<float>(original_width);
          box.y = y1 / static_cast<float>(original_height);
          box.w = (x2 - x1) / static_cast<float>(original_width);
          box.h = (y2 - y1) / static_cast<float>(original_height);
          boxes.push_back(std::move(box));
        }
      }
      values += channels;
    }
  }
}

}  // namespace

NcnnDetectorNode::NcnnDetectorNode(Options options, std::shared_ptr<Metrics> metrics)
    : BaseNode("ncnn_detector"), options_(std::move(options)),
      conf_threshold_(options_.conf_threshold), metrics_(std::move(metrics)) {
  if (options_.class_names.empty()) {
    options_.class_names.assign(std::begin(kCocoLabels), std::end(kCocoLabels));
  }
  if (options_.input_size <= 0) {
    options_.input_size = 320;
  }
  if (options_.num_threads <= 0) {
    options_.num_threads = 2;
  }
  if (options_.quantization.empty()) {
    options_.quantization = "auto";
  }
  if (options_.max_candidates == 0) {
    options_.max_candidates = 512;
  }
  if (options_.max_targets == 0) {
    options_.max_targets = 64;
  }
}

NcnnDetectorNode::~NcnnDetectorNode() {
  // Join the worker before ncnn::Net is destroyed; a queued frame may still be
  // inside process() while this object is being torn down.
  stop();
}

bool NcnnDetectorNode::load() {
  loaded_ = false;
  effective_quantization_.clear();
  output_names_.clear();
  net_.reset();

  DetectorConfig config;
  config.model_param = options_.model_param;
  config.model_bin = options_.model_bin;
  config.quantization = options_.quantization;
  config.model_candidates = options_.model_candidates;

  const DeviceResources resources =
      has_device_resources_ ? device_resources_ : detect_device_resources();
  const ModelSelection selection = select_model(config, resources);

  if (selection.param.empty() || selection.bin.empty()) {
    return false;
  }
  if (load_net(selection.param, selection.bin, selection.quantization)) {
    effective_quantization_ = selection.quantization;
    return true;
  }

  // A quantized candidate may be missing on disk or rejected by ncnn. Keep the
  // pipeline usable by retrying the base model when it differs from the
  // selection.
  const bool is_base_model = selection.param == options_.model_param &&
                             selection.bin == options_.model_bin;
  if (!is_base_model &&
      load_net(options_.model_param, options_.model_bin, "fp32")) {
    effective_quantization_ = "fp32";
    return true;
  }
  return false;
}

bool NcnnDetectorNode::load_net(const std::string& param,
                                const std::string& bin,
                                const std::string& quantization) {
  loaded_ = false;
  output_names_.clear();
  net_ = std::make_unique<ncnn::Net>();

  if (param.empty() || bin.empty()) {
    return false;
  }
  std::ifstream param_file(param, std::ios::binary);
  std::ifstream bin_file(bin, std::ios::binary);
  if (!param_file.good() || !bin_file.good()) {
    return false;
  }

  // fp16 arithmetic is only enabled for an fp16 model. int8 inference uses
  // ncnn's own switch, and fp32 keeps the fp16 paths off.
  const bool use_fp16 = quantization == "fp16";
  net_->opt.use_vulkan_compute = false;
  net_->opt.use_fp16_arithmetic = use_fp16;
  net_->opt.use_fp16_packed = use_fp16;
  net_->opt.use_fp16_storage = use_fp16;
  net_->opt.use_int8_inference = quantization == "int8";
  net_->opt.num_threads = options_.num_threads;

  if (net_->load_param(param.c_str()) != 0) {
    return false;
  }
  if (net_->load_model(bin.c_str()) != 0) {
    net_->clear();
    return false;
  }

  input_name_ = net_->input_names().empty() ? "input.1" : net_->input_names().front();
  for (const char* name : net_->output_names()) {
    output_names_.emplace_back(name);
  }
  if (output_names_.empty()) {
    output_names_ = {"794", "796"};
  }
  loaded_ = true;
  return true;
}

FrameMetaPtr NcnnDetectorNode::process(FrameMetaPtr meta) {
  if (!meta || !meta->has_motion || !loaded_) {
    return meta;
  }
  if (meta->width <= 0 || meta->height <= 0) {
    return meta;
  }
  const std::size_t required = static_cast<std::size_t>(meta->width) *
                               static_cast<std::size_t>(meta->height) *
                               static_cast<std::size_t>(kInputChannels);
  if (meta->rgb_data.size() < required) {
    return meta;
  }

  const auto start = std::chrono::steady_clock::now();
  const int input_size = options_.input_size;
  // One read per frame keeps the threshold consistent across both the decode
  // filter and the post-filter while a PATCH may update it concurrently.
  const float conf_threshold = conf_threshold_.load();
  const float scale =
      std::min(static_cast<float>(input_size) / static_cast<float>(meta->width),
               static_cast<float>(input_size) / static_cast<float>(meta->height));
  const int resized_width = std::max(
      1, static_cast<int>(std::lround(static_cast<float>(meta->width) * scale)));
  const int resized_height = std::max(
      1, static_cast<int>(std::lround(static_cast<float>(meta->height) * scale)));
  const int pad_x = (input_size - resized_width) / 2;
  const int pad_y = (input_size - resized_height) / 2;

  ncnn::Mat resized = ncnn::Mat::from_pixels_resize(
      meta->rgb_data.data(), ncnn::Mat::PIXEL_RGB, meta->width, meta->height,
      resized_width, resized_height);
  if (resized.empty()) {
    return meta;
  }
  ncnn::Mat letterboxed;
  ncnn::copy_make_border(resized, letterboxed, pad_y,
                         input_size - resized_height - pad_y, pad_x,
                         input_size - resized_width - pad_x,
                         ncnn::BORDER_CONSTANT, kPadValue);
  if (letterboxed.empty()) {
    return meta;
  }
  const float mean_vals[kInputChannels] = {0.0f, 0.0f, 0.0f};
  const float norm_vals[kInputChannels] = {1.0f / 255.0f, 1.0f / 255.0f,
                                           1.0f / 255.0f};
  letterboxed.substract_mean_normalize(mean_vals, norm_vals);

  std::vector<TargetBox> candidates;
  candidates.reserve(std::min<std::size_t>(options_.max_candidates, 256));

  ncnn::Extractor extractor = net_->create_extractor();
  if (extractor.input(input_name_.c_str(), letterboxed) == 0) {
    for (const std::string& output_name : output_names_) {
      ncnn::Mat out;
      if (extractor.extract(output_name.c_str(), out) != 0 || out.empty()) {
        continue;
      }
      decode_output(out, input_size, scale, pad_x, pad_y, meta->width,
                    meta->height, conf_threshold, options_.class_names,
                    options_.max_candidates, candidates);
    }
  }

  const std::vector<TargetBox> filtered = detection::filter_detections(
      candidates, options_.target_labels, conf_threshold);
  meta->targets =
      detection::nms(filtered, options_.nms_threshold, options_.max_targets);

  const auto end = std::chrono::steady_clock::now();
  meta->infer_ms =
      std::chrono::duration<double, std::milli>(end - start).count();
  if (metrics_) {
    // Only frames that actually ran inference reach this point; unloaded or
    // static frames never inflate the reported average.
    metrics_->add_infer_ms(meta->infer_ms);
  }
  return meta;
}

}  // namespace edge
