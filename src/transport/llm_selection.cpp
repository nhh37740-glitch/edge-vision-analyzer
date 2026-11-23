#include "transport/llm_selection.hpp"

#include <algorithm>

namespace edge {
namespace {

bool candidate_available(const LlmConfig& llm, const std::string& tier,
                         const LlmResources& resources) {
  const auto candidate = llm.model_candidates.find(tier);
  if (candidate == llm.model_candidates.end() || candidate->second.empty()) {
    return false;
  }
  if (resources.available_models.empty()) {
    return true;
  }
  return std::find(resources.available_models.begin(), resources.available_models.end(),
                   candidate->second) != resources.available_models.end();
}

bool vram_supports(const LlmResources& resources, const std::string& tier) {
  if (resources.vram_mb == 0) {
    return true;
  }
  if (tier == "q8") {
    return resources.vram_mb >= 8192;
  }
  if (tier == "q5") {
    return resources.vram_mb >= 6144;
  }
  if (tier == "q4") {
    return resources.vram_mb >= 4096;
  }
  return true;
}

bool within_read_timeout(const LlmConfig& llm, const LlmResources& resources) {
  const auto timeout = std::chrono::milliseconds{static_cast<long long>(llm.read_timeout_sec) *
                                                 1000};
  return resources.measured_latency <= timeout;
}

}  // namespace

LlmSelection select_llm_model(const LlmConfig& llm, const LlmResources& resources) {
  LlmSelection selection;
  selection.model_alias = llm.model;
  selection.quantization = "none";
  selection.enabled = llm.enabled;
  if (!llm.enabled) {
    return selection;
  }

  const auto select_candidate = [&](const std::string& tier) {
    selection.model_alias = llm.model_candidates.at(tier);
    selection.quantization = tier;
    selection.fallback = false;
  };

  if (llm.quantization == "q8" || llm.quantization == "q5" || llm.quantization == "q4") {
    if (candidate_available(llm, llm.quantization, resources) &&
        vram_supports(resources, llm.quantization)) {
      select_candidate(llm.quantization);
    } else {
      selection.fallback = true;
    }
    return selection;
  }

  if (llm.quantization == "auto") {
    static const char* const kAutoTiers[] = {"q8", "q5", "q4"};
    for (const char* tier : kAutoTiers) {
      if (candidate_available(llm, tier, resources) && vram_supports(resources, tier) &&
          within_read_timeout(llm, resources)) {
        select_candidate(tier);
        return selection;
      }
    }
    selection.fallback = true;
    return selection;
  }

  if (llm.quantization == "none") {
    return selection;
  }

  // Unknown tiers are rejected by config validation; treat them as a fallback
  // so a stale runtime configuration still sends a usable alias.
  selection.fallback = true;
  return selection;
}

}  // namespace edge
