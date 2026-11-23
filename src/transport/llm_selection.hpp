#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "common/config.hpp"

namespace edge {

// Resources observed for the llama-server endpoint. An empty available_models
// list means the model list is unknown and availability is not restricted.
struct LlmResources {
  std::vector<std::string> available_models;
  std::size_t vram_mb = 0;
  std::chrono::milliseconds measured_latency{0};
};

struct LlmSelection {
  std::string model_alias;
  std::string quantization;
  bool fallback = false;
  bool enabled = false;
};

// Picks the model alias sent in the chat completion request. Explicit tiers are
// selected when available and supported by VRAM; auto walks q8 -> q5 -> q4 and
// respects VRAM and the read timeout. Missing candidates fall back to
// LlmConfig::model with "none". vram_mb == 0 means unknown and imposes no limit.
LlmSelection select_llm_model(const LlmConfig& llm, const LlmResources& resources);

}  // namespace edge
