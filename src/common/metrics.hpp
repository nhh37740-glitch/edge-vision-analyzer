#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace edge {

class Metrics {
 public:
  Metrics();

  void on_capture_frame();
  void on_detect_frame();
  void on_motion_skip();
  void on_dropped_frame();
  void add_infer_ms(double milliseconds);
  void add_llm_latency_ms(double milliseconds);

  std::uint64_t capture_frames() const { return capture_frames_.load(); }
  std::uint64_t detect_frames() const { return detect_frames_.load(); }
  std::uint64_t motion_skips() const { return motion_skips_.load(); }
  std::uint64_t dropped_frames() const { return dropped_frames_.load(); }
  double uptime_sec() const;

  nlohmann::json to_json() const;

 private:
  std::chrono::steady_clock::time_point started_;
  std::atomic<std::uint64_t> capture_frames_{0};
  std::atomic<std::uint64_t> detect_frames_{0};
  std::atomic<std::uint64_t> motion_skips_{0};
  std::atomic<std::uint64_t> dropped_frames_{0};
  std::atomic<std::uint64_t> infer_us_sum_{0};
  std::atomic<std::uint64_t> infer_count_{0};
  std::atomic<std::uint64_t> llm_us_sum_{0};
  std::atomic<std::uint64_t> llm_count_{0};
};

}  // namespace edge
