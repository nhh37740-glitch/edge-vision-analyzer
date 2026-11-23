#pragma once

#include <cstddef>
#include <string>

#include "common/config.hpp"

namespace edge {

// Resources used to choose a quantized model. Tests inject this struct instead
// of reading the real machine so selection stays deterministic.
struct DeviceResources {
  std::size_t available_memory_mb = 0;
  bool fp16_supported = false;
  bool int8_supported = false;
};

struct ModelSelection {
  std::string param;
  std::string bin;
  std::string quantization;
  bool fallback = false;
};

// Probes the running device: on Linux the MemAvailable value from /proc/meminfo
// converted to MB, 0 elsewhere. fp16_supported is conservatively true on
// aarch64/x86_64; int8_supported requires the aarch64 dotprod flag (asimddp)
// from /proc/cpuinfo. Both can be tightened once real hardware is measured.
DeviceResources detect_device_resources();

// Pure selection logic. Explicit fp32/fp16/int8 requests use that candidate when
// its param and bin are both present, otherwise the base model with
// quantization "fp32" and fallback true. "auto" prefers int8 when a candidate
// exists, the CPU supports int8 and at least 256 MB is available, then fp16 when
// supported and at least 128 MB is available, and otherwise falls back to the
// base model.
ModelSelection select_model(const DetectorConfig& config,
                            const DeviceResources& resources);

}  // namespace edge
