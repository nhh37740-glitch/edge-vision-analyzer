#include "transport/genai_worker.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <utility>

#include "httplib.h"
#include "nlohmann/json.hpp"
#include "common/metrics.hpp"
#include "transport/jpeg_base64.hpp"

namespace edge {
namespace {

struct Endpoint {
  std::string base;
  std::string path;
};

// Config endpoints carry the operation path, for example
// http://host:8080/v1/chat/completions. httplib::Client wants the base URL
// separately from the request path.
Endpoint split_endpoint(const std::string& endpoint) {
  const std::size_t scheme = endpoint.find("://");
  const std::size_t host_start = scheme == std::string::npos ? 0 : scheme + 3;
  const std::size_t path_start = endpoint.find('/', host_start);
  if (path_start == std::string::npos) {
    return {endpoint, "/"};
  }
  return {endpoint.substr(0, path_start), endpoint.substr(path_start)};
}

}  // namespace

GenAiWorker::GenAiWorker(LlmConfig llm, StorageConfig storage,
                         std::shared_ptr<Metrics> metrics)
    : GenAiWorker(std::move(llm), std::move(storage), LlmResources{},
                  std::move(metrics)) {}

GenAiWorker::GenAiWorker(LlmConfig llm, StorageConfig storage,
                         LlmResources llm_resources,
                         std::shared_ptr<Metrics> metrics)
    : llm_(std::move(llm)),
      storage_(std::move(storage)),
      llm_enabled_(llm_.enabled),
      storage_enabled_(storage_.enabled),
      selection_(select_llm_model(llm_, llm_resources)),
      metrics_(std::move(metrics)),
      resources_injected_(!llm_resources.available_models.empty()) {}

GenAiWorker::~GenAiWorker() { stop(); }

LlmResources GenAiWorker::probe_llm_resources() const {
  LlmResources resources;
  const Endpoint endpoint = split_endpoint(llm_.endpoint);
  httplib::Client client(endpoint.base);
  client.set_connection_timeout(llm_.connect_timeout_sec, 0);
  client.set_read_timeout(llm_.read_timeout_sec, 0);
  client.set_write_timeout(llm_.read_timeout_sec, 0);

  // llama-server exposes loaded aliases here; any failure leaves the list
  // unknown and selection falls back to the configured aliases.
  const auto response = client.Get("/v1/models");
  if (!response || response->status != 200) {
    return resources;
  }
  try {
    const nlohmann::json parsed = nlohmann::json::parse(response->body);
    const auto& models = parsed.at("data");
    if (!models.is_array()) {
      return resources;
    }
    for (const auto& model : models) {
      if (model.contains("id") && model.at("id").is_string()) {
        resources.available_models.push_back(model.at("id").get<std::string>());
      }
    }
  } catch (const std::exception&) {
    resources.available_models.clear();
  }
  return resources;
}

void GenAiWorker::start() {
  if (running_.exchange(true)) {
    return;
  }
  // Do not contact llama-server at boot while LLM delivery is off. If PATCH
  // enables it later, the constructor's fallback selection (the configured
  // alias) is used for the first request without a second startup probe.
  if (!resources_injected_ && llm_.enabled) {
    selection_ = select_llm_model(llm_, probe_llm_resources());
  }
  worker_ = std::thread([this] { run(); });
}

void GenAiWorker::stop() {
  running_.store(false);
  queue_.close();
  if (worker_.joinable()) {
    worker_.join();
  }
  // Drop anything still queued so its frame reference is released before the
  // worker object is destroyed.
  GenAiTask leftover;
  while (queue_.pop(leftover)) {
    dropped_.fetch_add(1);
    leftover = GenAiTask{};
  }
}

bool GenAiWorker::submit(GenAiTask task) {
  if (!running_.load()) {
    return false;
  }
  submitted_.fetch_add(1);
  if (queue_.push(std::move(task))) {
    // BoundedQueue keeps only the latest task when full; the replaced task
    // has already released its frame reference.
    dropped_.fetch_add(1);
  }
  return true;
}

GenAiStats GenAiWorker::stats() const {
  GenAiStats stats;
  stats.submitted = submitted_.load();
  stats.dropped = dropped_.load();
  stats.processed = processed_.load();
  stats.encoded = encoded_.load();
  stats.llm_requests = llm_requests_.load();
  stats.llm_failures = llm_failures_.load();
  stats.storage_requests = storage_requests_.load();
  stats.storage_failures = storage_failures_.load();
  return stats;
}

GenAiResult GenAiWorker::last_result() const {
  std::lock_guard<std::mutex> lock(result_mutex_);
  return last_result_;
}

void GenAiWorker::run() {
  GenAiTask task;
  while (queue_.pop(task)) {
    if (!running_.load()) {
      dropped_.fetch_add(1);
      return;
    }
    handle(std::move(task));
    task = GenAiTask{};
  }
}

void GenAiWorker::handle(GenAiTask task) {
  // Snapshot both switches once so a PATCH during one task cannot leave the
  // summary and storage branches using different generations.
  const bool llm_enabled = llm_enabled_.load();
  const bool storage_enabled = storage_enabled_.load();
  if (!llm_enabled && !storage_enabled) {
    // Both switches are off: skip JPEG/Base64 encoding and HTTP entirely.
    task.frame.reset();
    GenAiResult result;
    result.event_id = task.event_id;
    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      last_result_ = std::move(result);
    }
    processed_.fetch_add(1);
    return;
  }

  if (!task.frame) {
    processed_.fetch_add(1);
    return;
  }

  const EncodedImage image = encode_jpeg_base64(*task.frame, llm_.jpeg_quality);
  // The RGB buffer is no longer needed once the JPEG exists.
  task.frame.reset();
  if (image.jpeg.empty() || image.base64.empty()) {
    std::fprintf(stderr, "GenAiWorker: cannot encode frame for %s\n", task.event_id.c_str());
    processed_.fetch_add(1);
    return;
  }
  encoded_.fetch_add(1);

  std::string summary;
  double latency_ms = 0.0;
  if (llm_enabled) {
    llm_requests_.fetch_add(1);
    const auto started = std::chrono::steady_clock::now();
    const bool ok = request_llm(image.base64, summary);
    latency_ms = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - started)
                     .count();
    if (metrics_) {
      // Record both success and failure so the average reflects real latency.
      metrics_->add_llm_latency_ms(latency_ms);
    }
    if (!ok) {
      summary.clear();
      llm_failures_.fetch_add(1);
      std::fprintf(stderr, "GenAiWorker: llm request failed for %s\n", task.event_id.c_str());
    }
  }

  bool delivered = false;
  if (storage_enabled) {
    storage_requests_.fetch_add(1);
    delivered = send_storage(task, image.base64, summary, latency_ms);
    if (!delivered) {
      storage_failures_.fetch_add(1);
      std::fprintf(stderr, "GenAiWorker: storage request failed for %s\n",
                   task.event_id.c_str());
    }
  }

  GenAiResult result;
  result.event_id = task.event_id;
  result.llm_summary = std::move(summary);
  result.llm_latency_ms = latency_ms;
  result.jpeg_bytes = image.jpeg.size();
  result.storage_delivered = delivered;
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    last_result_ = std::move(result);
  }
  processed_.fetch_add(1);
  // image.jpeg, image.base64 and the task snapshot are released here.
}

bool GenAiWorker::request_llm(const std::string& image_base64, std::string& summary) {
  const Endpoint endpoint = split_endpoint(llm_.endpoint);
  httplib::Client client(endpoint.base);
  client.set_connection_timeout(llm_.connect_timeout_sec, 0);
  client.set_read_timeout(llm_.read_timeout_sec, 0);
  client.set_write_timeout(llm_.read_timeout_sec, 0);

  nlohmann::json content = nlohmann::json::array();
  content.push_back({{"type", "text"}, {"text", llm_.prompt}});
  content.push_back(
      {{"type", "image_url"},
       {"image_url", {{"url", "data:image/jpeg;base64," + image_base64}}}});
  nlohmann::json body = {
      {"model", selection_.model_alias},
      {"max_tokens", 128},
      {"temperature", 0.2},
      {"messages", nlohmann::json::array(
                       {{{"role", "user"}, {"content", std::move(content)}}})}};

  const auto response = client.Post(endpoint.path, body.dump(), "application/json");
  if (!response || response->status < 200 || response->status >= 300) {
    return false;
  }
  try {
    const nlohmann::json parsed = nlohmann::json::parse(response->body);
    const auto& choices = parsed.at("choices");
    if (!choices.is_array() || choices.empty()) {
      return false;
    }
    const auto& content_value = choices.at(0).at("message").at("content");
    if (!content_value.is_string()) {
      return false;
    }
    summary = content_value.get<std::string>();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool GenAiWorker::send_storage(const GenAiTask& task, const std::string& image_base64,
                               const std::string& summary, double latency_ms) const {
  nlohmann::json targets = nlohmann::json::array();
  for (const auto& target : task.targets) {
    nlohmann::json box = {{"label", target.label}, {"score", target.score},
                          {"x", target.x},         {"y", target.y},
                          {"w", target.w},         {"h", target.h}};
    if (target.class_id >= 0) {
      box["class_id"] = target.class_id;
    }
    targets.push_back(std::move(box));
  }

  const nlohmann::json body = {
      {"event_id", task.event_id},
      {"frame_id", task.frame_id},
      {"timestamp_ms", task.timestamp_ms},
      {"top_label", task.top_label},
      {"top_score", task.top_score},
      {"targets", std::move(targets)},
      {"llm_summary", summary},
      {"llm_latency_ms", latency_ms},
      {"image_base64", image_base64},
  };

  const Endpoint endpoint = split_endpoint(storage_.endpoint);
  httplib::Client client(endpoint.base);
  client.set_connection_timeout(storage_.timeout_sec, 0);
  client.set_read_timeout(storage_.timeout_sec, 0);
  client.set_write_timeout(storage_.timeout_sec, 0);

  const auto response = client.Post(endpoint.path, body.dump(), "application/json");
  return response && response->status >= 200 && response->status < 300;
}

}  // namespace edge
