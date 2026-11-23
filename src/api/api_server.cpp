#include "api/api_server.hpp"

#include <cmath>
#include <cstdio>
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

}  // namespace

ApiServer::ApiServer(Config config, std::shared_ptr<SourceControl> source_control,
                     std::shared_ptr<Metrics> metrics, std::string bind_address,
                     std::optional<StreamCallbacks> stream_callbacks)
    : server_(std::make_unique<httplib::Server>()),
      config_(std::move(config)),
      bind_address_(std::move(bind_address)),
      source_control_(std::move(source_control)),
      metrics_(std::move(metrics)),
      stream_callbacks_(std::move(stream_callbacks)) {
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
    set_json(response,
             {{"status", status},
              {"version", version()},
              {"uptime_sec", round_tenth(uptime)},
              {"nodes", nodes},
              {"llm",
               {{"status", config.llm.enabled ? "unknown" : "disabled"},
                {"endpoint", health_endpoint(config.llm.endpoint)}}},
              {"storage",
               {{"status", config.storage.enabled ? "unknown" : "disabled"},
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
  server_->stop();
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace edge
