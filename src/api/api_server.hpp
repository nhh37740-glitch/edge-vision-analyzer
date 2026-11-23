#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/config.hpp"
#include "common/metrics.hpp"

namespace httplib {
class Server;
}

namespace edge {

class SourceControl;

// Adapter between the REST layer and the streamer node. The API module does
// not link edge_streamer; main injects these callbacks when the streamer is
// wired (later issue). Without them snapshot and stream answer 503.
struct StreamCallbacks {
  std::function<void()> subscribe;
  std::function<void()> unsubscribe;
  std::function<void()> request_snapshot;
  std::function<std::shared_ptr<const std::vector<std::uint8_t>>(
      std::uint64_t after_seq, std::chrono::milliseconds timeout)>
      wait_for_jpeg;
  std::function<std::uint64_t()> latest_jpeg_seq;
};

// Edge REST control plane. History queries belong to the PC cloud-storage
// service and are intentionally not proxied here.
class ApiServer {
 public:
  ApiServer(Config config, std::shared_ptr<SourceControl> source_control,
            std::shared_ptr<Metrics> metrics, std::string bind_address = "0.0.0.0",
            std::optional<StreamCallbacks> stream_callbacks = std::nullopt);
  ~ApiServer();

  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  bool start(std::string& error);
  void stop();
  int port() const;

 private:
  void configure_routes();
  Config config_snapshot() const;

  std::unique_ptr<httplib::Server> server_;
  std::thread thread_;
  std::atomic<int> bound_port_{0};
  mutable std::mutex config_mutex_;
  std::mutex switch_mutex_;
  Config config_;
  std::string bind_address_;
  std::shared_ptr<SourceControl> source_control_;
  std::shared_ptr<Metrics> metrics_;
  std::optional<StreamCallbacks> stream_callbacks_;
  std::chrono::steady_clock::time_point started_{};
};

}  // namespace edge
