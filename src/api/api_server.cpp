#include "api/api_server.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

#include "core/base_node.hpp"
#include "core/build_info.hpp"
#include "core/node_state.hpp"
#include "core/source_control.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace edge {
namespace {

void set_json(httplib::Response& response, const nlohmann::json& body, int status = 200) {
  response.status = status;
  response.set_content(body.dump(), "application/json");
}

void set_error(httplib::Response& response, int status, const char* code,
               const std::string& message) {
  set_json(response, {{"error", {{"code", code}, {"message", message}}}}, status);
}

double round_tenth(double value) { return std::round(value * 10.0) / 10.0; }

constexpr std::chrono::milliseconds kFrameWait{2000};

// Shared with the chunked provider below. It owns its own copy of the
// callbacks so an in-flight response can outlive the ApiServer instance
// without touching this.
struct StreamState {
  StreamCallbacks callbacks;
  std::shared_ptr<const std::vector<std::uint8_t>> first_frame;
  std::uint64_t last_seq = 0;
  bool first_sent = false;
};

// The configured endpoints carry operation paths; health reports only the
// upstream base plus /health, matching the REST contract.
std::string health_endpoint(const std::string& endpoint) {
  if (endpoint.empty()) {
    return endpoint;
  }
  const std::size_t scheme = endpoint.find("://");
  const std::size_t path = endpoint.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  return (path == std::string::npos ? endpoint : endpoint.substr(0, path)) + "/health";
}

struct SplitEndpoint {
  std::string base;
  std::string path;
};

SplitEndpoint split_endpoint(const std::string& endpoint) {
  const std::size_t scheme = endpoint.find("://");
  const std::size_t host_start = scheme == std::string::npos ? 0 : scheme + 3;
  const std::size_t path_start = endpoint.find('/', host_start);
  if (path_start == std::string::npos) {
    return {endpoint, "/"};
  }
  return {endpoint.substr(0, path_start), endpoint.substr(path_start)};
}

// A failed probe only marks that upstream unreachable. It must never wait on
// real inference or storage writes, so the request is a plain GET /health
// capped well below the edge health endpoint's own latency budget.
constexpr std::chrono::milliseconds kProbeTimeout{500};
constexpr std::chrono::milliseconds kProbeInterval{2000};

std::string probe_upstream(const std::string& endpoint) {
  const SplitEndpoint split = split_endpoint(endpoint);
  httplib::Client client(split.base);
  client.set_connection_timeout(kProbeTimeout);
  client.set_read_timeout(kProbeTimeout);
  client.set_write_timeout(kProbeTimeout);
  const auto response = client.Get(split.path);
  return response && response->status == 200 ? "reachable" : "unreachable";
}

// Only these five leaves are hot-updatable. The parser rejects the whole
// request on any unknown key, wrong type or out-of-range value so the caller
// can leave the published config untouched.
struct ConfigPatch {
  std::optional<double> motion_mad_threshold;
  std::optional<double> conf_threshold;
  std::optional<bool> llm_enabled;
  std::optional<int> llm_cooldown_sec;
  std::optional<bool> storage_enabled;
};

constexpr const char* kPatchShapeError = "config patch must be a non-empty JSON object";
constexpr const char* kPatchUnknownFieldError = "config patch contains an unknown field";
constexpr const char* kPatchTypeError = "config patch field has an invalid type";
constexpr const char* kPatchRangeError = "config patch value is out of range";

bool parse_patch_number(const nlohmann::json& value, double minimum, double maximum,
                        std::optional<double>& target, const char*& error) {
  if (!value.is_number()) {
    error = kPatchTypeError;
    return false;
  }
  const double number = value.get<double>();
  if (number < minimum || number > maximum) {
    error = kPatchRangeError;
    return false;
  }
  target = number;
  return true;
}

bool parse_patch_bool(const nlohmann::json& value, std::optional<bool>& target,
                      const char*& error) {
  if (!value.is_boolean()) {
    error = kPatchTypeError;
    return false;
  }
  target = value.get<bool>();
  return true;
}

bool parse_patch_cooldown(const nlohmann::json& value, std::optional<int>& target,
                          const char*& error) {
  if (!value.is_number_integer()) {
    error = kPatchTypeError;
    return false;
  }
  if (value.is_number_unsigned()) {
    const std::uint64_t seconds = value.get<std::uint64_t>();
    if (seconds > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
      error = kPatchRangeError;
      return false;
    }
    target = static_cast<int>(seconds);
    return true;
  }
  const std::int64_t seconds = value.get<std::int64_t>();
  if (seconds < 0 || seconds > std::numeric_limits<int>::max()) {
    error = kPatchRangeError;
    return false;
  }
  target = static_cast<int>(seconds);
  return true;
}

bool parse_config_patch(const nlohmann::json& body, ConfigPatch& patch,
                        const char*& error) {
  if (!body.is_object() || body.empty()) {
    error = kPatchShapeError;
    return false;
  }

  for (auto group = body.begin(); group != body.end(); ++group) {
    if (!group.value().is_object() || group.value().empty()) {
      error = kPatchShapeError;
      return false;
    }

    for (auto leaf = group.value().begin(); leaf != group.value().end(); ++leaf) {
      if (group.key() == "detector" && leaf.key() == "motion_mad_threshold") {
        if (!parse_patch_number(leaf.value(), 0.0, 255.0,
                                patch.motion_mad_threshold, error)) {
          return false;
        }
      } else if (group.key() == "detector" && leaf.key() == "conf_threshold") {
        if (!parse_patch_number(leaf.value(), 0.0, 1.0, patch.conf_threshold,
                                error)) {
          return false;
        }
      } else if (group.key() == "llm" && leaf.key() == "enabled") {
        if (!parse_patch_bool(leaf.value(), patch.llm_enabled, error)) {
          return false;
        }
      } else if (group.key() == "llm" && leaf.key() == "cooldown_sec") {
        if (!parse_patch_cooldown(leaf.value(), patch.llm_cooldown_sec, error)) {
          return false;
        }
      } else if (group.key() == "storage" && leaf.key() == "enabled") {
        if (!parse_patch_bool(leaf.value(), patch.storage_enabled, error)) {
          return false;
        }
      } else {
        error = kPatchUnknownFieldError;
        return false;
      }
    }
  }
  return true;
}

}  // namespace

ApiServer::ApiServer(Config config, std::shared_ptr<SourceControl> source_control,
                     std::shared_ptr<Metrics> metrics, std::string bind_address,
                     std::optional<StreamCallbacks> stream_callbacks,
                     ConfigAppliers config_appliers)
    : server_(std::make_unique<httplib::Server>()),
      config_(std::move(config)),
      bind_address_(std::move(bind_address)),
      source_control_(std::move(source_control)),
      metrics_(std::move(metrics)),
      stream_callbacks_(std::move(stream_callbacks)),
      config_appliers_(std::move(config_appliers)) {
  configure_routes();
}

ApiServer::~ApiServer() { stop(); }

Config ApiServer::config_snapshot() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return config_;
}

int ApiServer::port() const {
  const int bound = bound_port_.load();
  return bound > 0 ? bound : config_snapshot().streamer.http_port;
}

void ApiServer::configure_routes() {
  server_->Get("/api/v1/health", [this](const httplib::Request&, httplib::Response& response) {
    const Config config = config_snapshot();
    const NodeState state = source_control_->state();
    nlohmann::json nodes = nlohmann::json::array();
    nodes.push_back({{"name", source_control_->name()}, {"state", to_string(state)}});

    const double uptime =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    const char* status = state == NodeState::kFailed ? "degraded" : "ok";
    std::string llm_status;
    std::string storage_status;
    {
      std::lock_guard<std::mutex> lock(probe_mutex_);
      llm_status = llm_status_;
      storage_status = storage_status_;
    }
    set_json(response,
             {{"status", status},
              {"version", version()},
              {"uptime_sec", round_tenth(uptime)},
              {"nodes", nodes},
              {"llm",
               {{"status", llm_status},
                {"endpoint", health_endpoint(config.llm.endpoint)}}},
              {"storage",
               {{"status", storage_status},
                {"endpoint", health_endpoint(config.storage.endpoint)}}}});
  });

  server_->Get("/api/v1/stats",
               [this](const httplib::Request&, httplib::Response& response) {
                 nlohmann::json stats = metrics_->to_json();
                 stats["dropped_frames"] =
                     metrics_->dropped_frames() + BaseNode::total_dropped_frames();
                 set_json(response, stats);
               });

  server_->Get("/api/v1/config",
               [this](const httplib::Request&, httplib::Response& response) {
                 set_json(response, nlohmann::json(config_snapshot()));
               });

  server_->Patch("/api/v1/config",
                 [this](const httplib::Request& request, httplib::Response& response) {
                   ConfigPatch patch;
                   const char* error = nullptr;
                   const nlohmann::json body =
                       nlohmann::json::parse(request.body, nullptr, false);
                   if (!parse_config_patch(body, patch, error)) {
                     set_error(response, 400, "INVALID_PARAMETER", error);
                     return;
                   }

                   // Validate the whole request first, then swap the complete
                   // snapshot under the lock so a reader never observes a
                   // partially applied patch. The running nodes are notified
                   // only after the new snapshot is published.
                   {
                     std::lock_guard<std::mutex> lock(config_mutex_);
                     Config updated = config_;
                     if (patch.motion_mad_threshold) {
                       updated.detector.motion_mad_threshold =
                           *patch.motion_mad_threshold;
                     }
                     if (patch.conf_threshold) {
                       updated.detector.conf_threshold = *patch.conf_threshold;
                     }
                     if (patch.llm_enabled) {
                       updated.llm.enabled = *patch.llm_enabled;
                     }
                     if (patch.llm_cooldown_sec) {
                       updated.llm.cooldown_sec = *patch.llm_cooldown_sec;
                     }
                     if (patch.storage_enabled) {
                       updated.storage.enabled = *patch.storage_enabled;
                     }
                     config_ = std::move(updated);
                   }

                   if (patch.motion_mad_threshold &&
                       config_appliers_.set_motion_mad_threshold) {
                     config_appliers_.set_motion_mad_threshold(
                         *patch.motion_mad_threshold);
                   }
                   if (patch.conf_threshold && config_appliers_.set_conf_threshold) {
                     config_appliers_.set_conf_threshold(*patch.conf_threshold);
                   }
                   if (patch.llm_enabled && config_appliers_.set_llm_enabled) {
                     config_appliers_.set_llm_enabled(*patch.llm_enabled);
                   }
                   if (patch.llm_cooldown_sec &&
                       config_appliers_.set_llm_cooldown_sec) {
                     config_appliers_.set_llm_cooldown_sec(*patch.llm_cooldown_sec);
                   }
                   if (patch.storage_enabled &&
                       config_appliers_.set_storage_enabled) {
                     config_appliers_.set_storage_enabled(*patch.storage_enabled);
                   }

                   set_json(response, nlohmann::json(config_snapshot()));
                 });

  server_->Post("/api/v1/source/switch",
                [this](const httplib::Request& request, httplib::Response& response) {
                  const nlohmann::json body =
                      nlohmann::json::parse(request.body, nullptr, false);
                  if (body.is_discarded() || !body.is_object() || !body.contains("source") ||
                      !body["source"].is_string() || !body.contains("loop") ||
                      !body["loop"].is_boolean()) {
                    set_error(response, 400, "INVALID_PARAMETER",
                              "source must be a string and loop must be boolean");
                    return;
                  }

                  const std::string source = body["source"].get<std::string>();
                  const bool loop = body["loop"].get<bool>();
                  if (source.empty()) {
                    set_error(response, 400, "INVALID_PARAMETER",
                              "source must not be empty");
                    return;
                  }

                  // Serialize switches so the committed capture source and
                  // config_ cannot be interleaved by two concurrent requests.
                  std::lock_guard<std::mutex> switch_lock(switch_mutex_);
                  std::string error;
                  if (!source_control_->switch_source(source, loop, error)) {
                    std::fprintf(stderr, "ApiServer: source switch failed: %s\n", error.c_str());
                    set_error(response, 400, "INVALID_SOURCE",
                              "cannot open the requested source; the original source is kept");
                    return;
                  }

                  {
                    std::lock_guard<std::mutex> lock(config_mutex_);
                    config_.input.source = source;
                    config_.input.loop = loop;
                  }
                  set_json(response, {{"status", "switched"}, {"source", source}, {"loop", loop}});
                });

  server_->Get("/api/v1/snapshot.jpg",
               [this](const httplib::Request&, httplib::Response& response) {
                 const auto callbacks = stream_callbacks_;
                 if (!callbacks) {
                   set_error(response, 503, "FRAME_UNAVAILABLE",
                             "snapshot output is not configured");
                   return;
                 }

                 // Take the sequence before requesting a frame so a snapshot
                 // never returns the JPEG that was already current.
                 const std::uint64_t after = callbacks->latest_jpeg_seq();
                 callbacks->request_snapshot();
                 const auto jpeg = callbacks->wait_for_jpeg(after, kFrameWait);
                 if (!jpeg) {
                   set_error(response, 503, "FRAME_UNAVAILABLE",
                             "no JPEG frame is available");
                   return;
                 }
                 response.set_content(reinterpret_cast<const char*>(jpeg->data()),
                                      jpeg->size(), "image/jpeg");
               });

  server_->Get("/api/v1/stream",
               [this](const httplib::Request&, httplib::Response& response) {
                 const auto callbacks = stream_callbacks_;
                 if (!callbacks) {
                   set_error(response, 503, "FRAME_UNAVAILABLE",
                             "stream output is not configured");
                   return;
                 }

                 // Confirm a first frame before committing to a multipart
                 // response; after this point only raw parts may be sent.
                 callbacks->subscribe();
                 auto first_frame = callbacks->wait_for_jpeg(0, kFrameWait);
                 if (!first_frame) {
                   callbacks->unsubscribe();
                   set_error(response, 503, "FRAME_UNAVAILABLE",
                             "no JPEG frame is available");
                   return;
                 }

                 auto state = std::make_shared<StreamState>();
                 state->callbacks = *callbacks;
                 state->first_frame = std::move(first_frame);
                 state->last_seq = callbacks->latest_jpeg_seq();

                 response.set_header("Cache-Control", "no-cache");
                 response.set_chunked_content_provider(
                     "multipart/x-mixed-replace; boundary=frame",
                     [state](std::size_t, httplib::DataSink& sink) {
                       // The provider blocks this httplib worker thread only;
                       // capture keeps running on its own node thread.
                       if (!sink.is_writable()) {
                         return false;
                       }

                       std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
                       if (!state->first_sent) {
                         jpeg = state->first_frame;
                         state->first_sent = true;
                       } else {
                         jpeg = state->callbacks.wait_for_jpeg(state->last_seq, kFrameWait);
                         if (!jpeg) {
                           return false;
                         }
                         state->last_seq = state->callbacks.latest_jpeg_seq();
                       }

                       char header[128];
                       const int header_len = std::snprintf(
                           header, sizeof(header),
                           "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                           jpeg->size());
                       if (header_len <= 0 ||
                           !sink.write(header, static_cast<std::size_t>(header_len))) {
                         return false;
                       }
                       if (!jpeg->empty() &&
                           !sink.write(reinterpret_cast<const char*>(jpeg->data()),
                                       jpeg->size())) {
                         return false;
                       }
                       return sink.write("\r\n", 2);
                     },
                     // Covers both normal completion and client disconnect.
                     [state](bool) { state->callbacks.unsubscribe(); });
               });
}

bool ApiServer::start(std::string& error) {
  const int requested = config_snapshot().streamer.http_port;
  int bound = requested;
  if (requested == 0) {
    bound = server_->bind_to_any_port(bind_address_);
    if (bound <= 0) {
      error = "cannot bind an ephemeral edge API port";
      return false;
    }
  } else if (!server_->bind_to_port(bind_address_, requested)) {
    error = "cannot bind edge API port " + std::to_string(requested);
    return false;
  }
  bound_port_.store(bound);
  started_ = std::chrono::steady_clock::now();

  const Config config = config_snapshot();
  {
    std::lock_guard<std::mutex> lock(probe_mutex_);
    probe_stop_ = false;
    llm_status_ = config.llm.enabled ? "unknown" : "disabled";
    storage_status_ = config.storage.enabled ? "unknown" : "disabled";
  }
  // Only enabled upstreams need a probe thread; disabled ones stay disabled
  // without waking the network at all.
  if (config.llm.enabled || config.storage.enabled) {
    probe_thread_ = std::thread([this] { run_probe_loop(); });
  }

  thread_ = std::thread([this] { server_->listen_after_bind(); });
  // stop() is a no-op until the listen thread marks the server running; wait
  // so an immediate start()/stop() pair cannot leave join() blocked forever.
  server_->wait_until_ready();
  return true;
}

void ApiServer::stop() {
  if (!server_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(probe_mutex_);
    probe_stop_ = true;
  }
  probe_cv_.notify_all();
  if (probe_thread_.joinable()) {
    probe_thread_.join();
  }
  server_->stop();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ApiServer::run_probe_loop() {
  while (true) {
    probe_upstreams();
    std::unique_lock<std::mutex> lock(probe_mutex_);
    if (probe_cv_.wait_for(lock, kProbeInterval, [this] { return probe_stop_; })) {
      return;
    }
  }
}

void ApiServer::probe_upstreams() {
  const Config config = config_snapshot();
  const std::string llm_endpoint = health_endpoint(config.llm.endpoint);
  const std::string storage_endpoint = health_endpoint(config.storage.endpoint);
  std::string llm = config.llm.enabled ? probe_upstream(llm_endpoint) : "disabled";
  std::string storage =
      config.storage.enabled ? probe_upstream(storage_endpoint) : "disabled";
  std::lock_guard<std::mutex> lock(probe_mutex_);
  llm_status_ = std::move(llm);
  storage_status_ = std::move(storage);
}

}  // namespace edge
