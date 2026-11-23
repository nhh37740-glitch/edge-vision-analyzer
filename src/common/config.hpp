#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace edge {

struct InputConfig {
  std::string source;
  bool loop = false;
  int width = 640;
  int height = 480;
  int target_fps = 15;
  int dump_interval_sec = 2;
};

struct DetectorConfig {
  double motion_mad_threshold = 8.0;
  std::string model_param;
  std::string model_bin;
  int input_size = 320;
  double conf_threshold = 0.5;
  int num_threads = 2;
  std::vector<std::string> target_labels;
};

struct EventConfig {
  int end_grace_sec = 5;
};

struct LlmConfig {
  bool enabled = false;
  std::string endpoint;
  // Base alias used when quantization is "none" or a tier falls back.
  std::string model;
  // Tier preference: auto, q8, q5, q4 or none.
  std::string quantization = "auto";
  // Optional tier -> llama-server model alias mapping.
  std::map<std::string, std::string> model_candidates;
  std::string prompt;
  int cooldown_sec = 15;
  int connect_timeout_sec = 3;
  int read_timeout_sec = 25;
  int jpeg_quality = 70;
};

struct StorageConfig {
  bool enabled = false;
  std::string endpoint;
  int timeout_sec = 5;
};

struct StreamerConfig {
  std::string mode = "mjpeg";
  int http_port = 8081;
  std::string rtsp_url;
};

struct Config {
  InputConfig input;
  DetectorConfig detector;
  EventConfig event;
  LlmConfig llm;
  StorageConfig storage;
  StreamerConfig streamer;

  // Throws std::runtime_error when the file cannot be read or validated.
  static Config load(const std::string& path);
};

void validate(const Config& config);

void from_json(const nlohmann::json& json, InputConfig& config);
void from_json(const nlohmann::json& json, DetectorConfig& config);
void from_json(const nlohmann::json& json, EventConfig& config);
void from_json(const nlohmann::json& json, LlmConfig& config);
void from_json(const nlohmann::json& json, StorageConfig& config);
void from_json(const nlohmann::json& json, StreamerConfig& config);
void from_json(const nlohmann::json& json, Config& config);

void to_json(nlohmann::json& json, const InputConfig& config);
void to_json(nlohmann::json& json, const DetectorConfig& config);
void to_json(nlohmann::json& json, const EventConfig& config);
void to_json(nlohmann::json& json, const LlmConfig& config);
void to_json(nlohmann::json& json, const StorageConfig& config);
void to_json(nlohmann::json& json, const StreamerConfig& config);
void to_json(nlohmann::json& json, const Config& config);

}  // namespace edge
