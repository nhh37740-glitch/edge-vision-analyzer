#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
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

// Adapter from the hot-updatable config leaves to the running nodes. main
// injects these when the pipeline exists; tests inject recording lambdas.
// Without an applier the API still publishes the new snapshot but the runtime
// node keeps its previous value.
struct ConfigAppliers {
  std::function<void(double)> set_motion_mad_threshold;
  std::function<void(double)> set_conf_threshold;
  std::function<void(bool)> set_llm_enabled;
  std::function<void(int)> set_llm_cooldown_sec;
  std::function<void(bool)> set_storage_enabled;
};

// Edge REST control plane. History queries belong to the PC cloud-storage
// service and are intentionally not proxied here.
class ApiServer {
 public:
  ApiServer(Config config, std::shared_ptr<SourceControl> source_control,
            std::shared_ptr<Metrics> metrics, std::string bind_address = "0.0.0.0",
            std::optional<StreamCallbacks> stream_callbacks = std::nullopt,
            ConfigAppliers config_appliers = {});
  ~ApiServer();

  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  bool start(std::string& error);
  void stop();
  int port() const;

 private:
  void configure_routes();
  Config config_snapshot() const;
  void run_probe_loop();
  void probe_upstreams();

  std::unique_ptr<httplib::Server> server_;
  std::thread thread_;
  std::thread probe_thread_;
  std::atomic<int> bound_port_{0};
  std::mutex probe_mutex_;
  std::condition_variable probe_cv_;
  bool probe_stop_ = false;
  std::string llm_status_ = "disabled";
  std::string storage_status_ = "disabled";
  mutable std::mutex config_mutex_;
  std::mutex switch_mutex_;
  Config config_;
  std::string bind_address_;
  std::shared_ptr<SourceControl> source_control_;
  std::shared_ptr<Metrics> metrics_;
  std::optional<StreamCallbacks> stream_callbacks_;
  ConfigAppliers config_appliers_;
  std::chrono::steady_clock::time_point started_{};
};

}  // namespace edge
