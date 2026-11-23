#include "common/config.hpp"

#include <fstream>
#include <set>
#include <stdexcept>

namespace edge {

Config Config::load(const std::string& path) {
  nlohmann::json json;
  try {
    std::ifstream input(path);
    if (!input) {
      throw std::runtime_error("cannot open config file: " + path);
    }
    input >> json;
  } catch (const std::exception& error) {
    throw std::runtime_error("cannot parse config file " + path + ": " + error.what());
  }

  try {
    Config config = json.get<Config>();
    validate(config);
    return config;
  } catch (const std::exception& error) {
    throw std::runtime_error("invalid config file " + path + ": " + error.what());
  }
}

void validate(const Config& config) {
  if (config.input.source.empty()) {
    throw std::runtime_error("input.source must not be empty");
  }
  if (config.input.width <= 0 || config.input.height <= 0) {
    throw std::runtime_error("input.width and input.height must be positive");
  }
  if (config.input.target_fps <= 0) {
    throw std::runtime_error("input.target_fps must be positive");
  }
  if (config.input.dump_interval_sec < 0) {
    throw std::runtime_error("input.dump_interval_sec must not be negative");
  }
  if (config.detector.motion_mad_threshold < 0.0 ||
      config.detector.motion_mad_threshold > 255.0) {
    throw std::runtime_error("detector.motion_mad_threshold must be between 0.0 and 255.0");
  }
  if (config.detector.conf_threshold < 0.0 || config.detector.conf_threshold > 1.0) {
    throw std::runtime_error("detector.conf_threshold must be between 0.0 and 1.0");
  }
  if (config.detector.input_size <= 0) {
    throw std::runtime_error("detector.input_size must be positive");
  }
  if (config.detector.num_threads <= 0) {
    throw std::runtime_error("detector.num_threads must be positive");
  }
  if (config.detector.quantization != "auto" &&
      config.detector.quantization != "fp32" &&
      config.detector.quantization != "fp16" &&
      config.detector.quantization != "int8") {
    throw std::runtime_error("detector.quantization must be auto, fp32, fp16 or int8");
  }
  for (const auto& entry : config.detector.model_candidates) {
    if (entry.first != "fp32" && entry.first != "fp16" && entry.first != "int8") {
      throw std::runtime_error(
          "detector.model_candidates keys must be fp32, fp16 or int8");
    }
    if (entry.second.param.empty() != entry.second.bin.empty()) {
      throw std::runtime_error("detector.model_candidates." + entry.first +
                               " param and bin must be set together");
    }
  }
  if (config.event.end_grace_sec < 0) {
    throw std::runtime_error("event.end_grace_sec must not be negative");
  }
  if (config.llm.cooldown_sec < 0) {
    throw std::runtime_error("llm.cooldown_sec must not be negative");
  }
  if (config.llm.connect_timeout_sec <= 0 || config.llm.read_timeout_sec <= 0) {
    throw std::runtime_error("llm timeouts must be positive");
  }
  if (config.llm.jpeg_quality < 1 || config.llm.jpeg_quality > 100) {
    throw std::runtime_error("llm.jpeg_quality must be between 1 and 100");
  }
  static const std::set<std::string> kQuantizationTiers = {"auto", "q8", "q5", "q4", "none"};
  if (kQuantizationTiers.count(config.llm.quantization) == 0) {
    throw std::runtime_error("llm.quantization must be one of auto, q8, q5, q4, none");
  }
  for (const auto& candidate : config.llm.model_candidates) {
    if (kQuantizationTiers.count(candidate.first) == 0) {
      throw std::runtime_error("llm.model_candidates contains an unknown quantization tier");
    }
    if (candidate.second.empty()) {
      throw std::runtime_error("llm.model_candidates aliases must not be empty");
    }
  }
  if (config.llm.enabled && config.llm.endpoint.empty()) {
    throw std::runtime_error("llm.endpoint must not be empty when llm is enabled");
  }
  if (config.storage.timeout_sec <= 0) {
    throw std::runtime_error("storage.timeout_sec must be positive");
  }
  if (config.storage.enabled && config.storage.endpoint.empty()) {
    throw std::runtime_error("storage.endpoint must not be empty when storage is enabled");
  }
  if (config.streamer.http_port < 1 || config.streamer.http_port > 65535) {
    throw std::runtime_error("streamer.http_port must be between 1 and 65535");
  }
  if (config.streamer.mode != "mjpeg" && config.streamer.mode != "rtsp") {
    throw std::runtime_error("streamer.mode must be mjpeg or rtsp");
  }
  if (config.streamer.mode == "rtsp" && config.streamer.rtsp_url.empty()) {
    throw std::runtime_error("streamer.rtsp_url must not be empty in rtsp mode");
  }
}

void from_json(const nlohmann::json& json, InputConfig& config) {
  config.source = json.at("source").get<std::string>();
  config.loop = json.at("loop").get<bool>();
  config.width = json.at("width").get<int>();
  config.height = json.at("height").get<int>();
  config.target_fps = json.at("target_fps").get<int>();
  config.dump_interval_sec = json.at("dump_interval_sec").get<int>();
}

void from_json(const nlohmann::json& json, ModelCandidate& config) {
  config.param = json.at("param").get<std::string>();
  config.bin = json.at("bin").get<std::string>();
}

void from_json(const nlohmann::json& json, DetectorConfig& config) {
  config.motion_mad_threshold = json.at("motion_mad_threshold").get<double>();
  config.model_param = json.at("model_param").get<std::string>();
  config.model_bin = json.at("model_bin").get<std::string>();
  config.input_size = json.at("input_size").get<int>();
  config.conf_threshold = json.at("conf_threshold").get<double>();
  config.num_threads = json.at("num_threads").get<int>();
  config.target_labels = json.at("target_labels").get<std::vector<std::string>>();
  config.quantization = json.value("quantization", std::string("auto"));
  config.model_candidates.clear();
  const auto candidates = json.find("model_candidates");
  if (candidates != json.end() && !candidates->is_null()) {
    config.model_candidates =
        candidates->get<std::map<std::string, ModelCandidate>>();
  }
}

void from_json(const nlohmann::json& json, EventConfig& config) {
  config.end_grace_sec = json.at("end_grace_sec").get<int>();
}

void from_json(const nlohmann::json& json, LlmConfig& config) {
  config.enabled = json.at("enabled").get<bool>();
  config.endpoint = json.at("endpoint").get<std::string>();
  config.model = json.at("model").get<std::string>();
  if (json.contains("quantization")) {
    config.quantization = json.at("quantization").get<std::string>();
  }
  if (json.contains("model_candidates")) {
    config.model_candidates =
        json.at("model_candidates").get<std::map<std::string, std::string>>();
  }
  config.prompt = json.at("prompt").get<std::string>();
  config.cooldown_sec = json.at("cooldown_sec").get<int>();
  config.connect_timeout_sec = json.at("connect_timeout_sec").get<int>();
  config.read_timeout_sec = json.at("read_timeout_sec").get<int>();
  config.jpeg_quality = json.at("jpeg_quality").get<int>();
}

void from_json(const nlohmann::json& json, StorageConfig& config) {
  config.enabled = json.at("enabled").get<bool>();
  config.endpoint = json.at("endpoint").get<std::string>();
  config.timeout_sec = json.at("timeout_sec").get<int>();
}

void from_json(const nlohmann::json& json, StreamerConfig& config) {
  config.mode = json.at("mode").get<std::string>();
  config.http_port = json.at("http_port").get<int>();
  config.rtsp_url = json.at("rtsp_url").get<std::string>();
}

void from_json(const nlohmann::json& json, Config& config) {
  config.input = json.at("input").get<InputConfig>();
  config.detector = json.at("detector").get<DetectorConfig>();
  config.event = json.at("event").get<EventConfig>();
  config.llm = json.at("llm").get<LlmConfig>();
  config.storage = json.at("storage").get<StorageConfig>();
  config.streamer = json.at("streamer").get<StreamerConfig>();
}

void to_json(nlohmann::json& json, const InputConfig& config) {
  json = nlohmann::json{{"source", config.source},
                        {"loop", config.loop},
                        {"width", config.width},
                        {"height", config.height},
                        {"target_fps", config.target_fps},
                        {"dump_interval_sec", config.dump_interval_sec}};
}

void to_json(nlohmann::json& json, const ModelCandidate& config) {
  json = nlohmann::json{{"param", config.param}, {"bin", config.bin}};
}

void to_json(nlohmann::json& json, const DetectorConfig& config) {
  json = nlohmann::json{{"motion_mad_threshold", config.motion_mad_threshold},
                        {"model_param", config.model_param},
                        {"model_bin", config.model_bin},
                        {"input_size", config.input_size},
                        {"conf_threshold", config.conf_threshold},
                        {"num_threads", config.num_threads},
                        {"target_labels", config.target_labels},
                        {"quantization", config.quantization}};
  if (!config.model_candidates.empty()) {
    json["model_candidates"] = config.model_candidates;
  }
}

void to_json(nlohmann::json& json, const EventConfig& config) {
  json = nlohmann::json{{"end_grace_sec", config.end_grace_sec}};
}

void to_json(nlohmann::json& json, const LlmConfig& config) {
  json = nlohmann::json{{"enabled", config.enabled},
                        {"endpoint", config.endpoint},
                        {"model", config.model},
                        {"quantization", config.quantization},
                        {"model_candidates", config.model_candidates},
                        {"prompt", config.prompt},
                        {"cooldown_sec", config.cooldown_sec},
                        {"connect_timeout_sec", config.connect_timeout_sec},
                        {"read_timeout_sec", config.read_timeout_sec},
                        {"jpeg_quality", config.jpeg_quality}};
}

void to_json(nlohmann::json& json, const StorageConfig& config) {
  json = nlohmann::json{{"enabled", config.enabled},
                        {"endpoint", config.endpoint},
                        {"timeout_sec", config.timeout_sec}};
}

void to_json(nlohmann::json& json, const StreamerConfig& config) {
  json = nlohmann::json{
      {"mode", config.mode}, {"http_port", config.http_port}, {"rtsp_url", config.rtsp_url}};
}

void to_json(nlohmann::json& json, const Config& config) {
  json = nlohmann::json{{"input", config.input},
                        {"detector", config.detector},
                        {"event", config.event},
                        {"llm", config.llm},
                        {"storage", config.storage},
                        {"streamer", config.streamer}};
}

}  // namespace edge
