#include "detector/model_selection.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace edge {
namespace {

constexpr std::size_t kInt8MemoryMb = 256;
constexpr std::size_t kFp16MemoryMb = 128;

const ModelCandidate* find_candidate(const DetectorConfig& config,
                                     const std::string& quantization) {
  const auto candidate = config.model_candidates.find(quantization);
  if (candidate == config.model_candidates.end()) {
    return nullptr;
  }
  if (candidate->second.param.empty() || candidate->second.bin.empty()) {
    return nullptr;
  }
  return &candidate->second;
}

ModelSelection base_selection(const DetectorConfig& config, bool fallback) {
  if (config.model_param.empty() || config.model_bin.empty()) {
    return {"", "", "fp32", true};
  }
  return {config.model_param, config.model_bin, "fp32", fallback};
}

}  // namespace

DeviceResources detect_device_resources() {
  DeviceResources resources;
#if defined(__linux__)
  std::ifstream meminfo("/proc/meminfo");
  std::string line;
  while (std::getline(meminfo, line)) {
    if (line.rfind("MemAvailable:", 0) != 0) {
      continue;
    }
    std::istringstream values(line);
    std::string label;
    std::string unit;
    unsigned long long value_kb = 0;
    values >> label >> value_kb >> unit;
    resources.available_memory_mb =
        static_cast<std::size_t>(value_kb / 1024);
    break;
  }

  // On aarch64, ncnn int8 inference is only worthwhile with the dot-product
  // extension. x86 currently reports false so auto stays on fp16 there.
  std::ifstream cpuinfo("/proc/cpuinfo");
  while (std::getline(cpuinfo, line)) {
    if (line.find("asimddp") != std::string::npos) {
      resources.int8_supported = true;
      break;
    }
  }
#endif
#if defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64) || \
    defined(_M_ARM64)
  resources.fp16_supported = true;
#endif
  return resources;
}

ModelSelection select_model(const DetectorConfig& config,
                            const DeviceResources& resources) {
  if (config.quantization == "fp32" || config.quantization == "fp16" ||
      config.quantization == "int8") {
    const ModelCandidate* candidate =
        find_candidate(config, config.quantization);
    if (candidate != nullptr) {
      return {candidate->param, candidate->bin, config.quantization, false};
    }
    return base_selection(config, true);
  }

  if (const ModelCandidate* int8 = find_candidate(config, "int8");
      int8 != nullptr && resources.int8_supported &&
      resources.available_memory_mb >= kInt8MemoryMb) {
    return {int8->param, int8->bin, "int8", false};
  }
  if (const ModelCandidate* fp16 = find_candidate(config, "fp16");
      fp16 != nullptr && resources.fp16_supported &&
      resources.available_memory_mb >= kFp16MemoryMb) {
    return {fp16->param, fp16->bin, "fp16", false};
  }
  return base_selection(config, true);
}

}  // namespace edge
