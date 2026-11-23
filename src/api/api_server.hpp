#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "common/config.hpp"
#include "common/metrics.hpp"

namespace httplib {
class Server;
}

namespace edge {

class SourceControl;

// Edge REST control plane. History queries belong to the PC cloud-storage
// service and are intentionally not proxied here.
class ApiServer {
 public:
  ApiServer(Config config, std::shared_ptr<SourceControl> source_control,
            std::shared_ptr<Metrics> metrics, std::string bind_address = "0.0.0.0");
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
  std::chrono::steady_clock::time_point started_{};
};

}  // namespace edge
