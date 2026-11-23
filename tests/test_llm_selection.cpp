#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "transport/llm_selection.hpp"

namespace {

edge::LlmConfig make_config(std::string quantization) {
  edge::LlmConfig llm;
  llm.enabled = true;
  llm.model = "vlm-base";
  llm.quantization = std::move(quantization);
  llm.model_candidates = {
      {"q8", "vlm-q8"},
      {"q5", "vlm-q5"},
      {"q4", "vlm-q4"},
  };
  llm.read_timeout_sec = 25;
  return llm;
}

}  // namespace

TEST(LlmSelection, ExplicitQ8UsesCandidateWhenAvailable) {
  const auto llm = make_config("q8");
  edge::LlmResources resources;
  resources.available_models = {"vlm-base", "vlm-q8", "vlm-q5"};

  const auto selection = edge::select_llm_model(llm, resources);

  EXPECT_TRUE(selection.enabled);
  EXPECT_FALSE(selection.fallback);
  EXPECT_EQ(selection.model_alias, "vlm-q8");
  EXPECT_EQ(selection.quantization, "q8");
}

TEST(LlmSelection, AutoSkipsUnavailableAndSlowTiers) {
  const auto llm = make_config("auto");

  edge::LlmResources available;
  available.available_models = {"vlm-base", "vlm-q5", "vlm-q4"};
  const auto selected = edge::select_llm_model(llm, available);
  EXPECT_FALSE(selected.fallback);
  EXPECT_EQ(selected.model_alias, "vlm-q5");
  EXPECT_EQ(selected.quantization, "q5");

  edge::LlmResources slow;
  slow.available_models = {"vlm-q8", "vlm-q5", "vlm-q4"};
  slow.measured_latency = std::chrono::milliseconds{25001};
  const auto timed_out = edge::select_llm_model(llm, slow);
  EXPECT_TRUE(timed_out.fallback);
  EXPECT_EQ(timed_out.model_alias, "vlm-base");
  EXPECT_EQ(timed_out.quantization, "none");
}

TEST(LlmSelection, EnforcesVramThresholds) {
  edge::LlmResources resources;
  resources.available_models = {"vlm-q8", "vlm-q5", "vlm-q4"};
  resources.vram_mb = 4096;

  const auto explicit_q8 = edge::select_llm_model(make_config("q8"), resources);
  EXPECT_TRUE(explicit_q8.fallback);
  EXPECT_EQ(explicit_q8.model_alias, "vlm-base");
  EXPECT_EQ(explicit_q8.quantization, "none");

  const auto auto_tier = edge::select_llm_model(make_config("auto"), resources);
  EXPECT_FALSE(auto_tier.fallback);
  EXPECT_EQ(auto_tier.model_alias, "vlm-q4");
  EXPECT_EQ(auto_tier.quantization, "q4");

  // Zero means the VRAM budget is unknown and imposes no limit.
  resources.vram_mb = 0;
  const auto unknown_vram = edge::select_llm_model(make_config("auto"), resources);
  EXPECT_EQ(unknown_vram.model_alias, "vlm-q8");
  EXPECT_EQ(unknown_vram.quantization, "q8");
}

TEST(LlmSelection, FallsBackToBaseModelWhenNoTierIsUsable) {
  const auto llm = make_config("auto");
  edge::LlmResources resources;
  resources.available_models = {"other-model"};

  const auto selection = edge::select_llm_model(llm, resources);

  EXPECT_TRUE(selection.enabled);
  EXPECT_TRUE(selection.fallback);
  EXPECT_EQ(selection.model_alias, "vlm-base");
  EXPECT_EQ(selection.quantization, "none");
}
