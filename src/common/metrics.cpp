#include "common/metrics.hpp"

#include <cmath>
#include <fstream>

#ifdef __linux__
#include <unistd.h>
#endif

namespace edge {
namespace {

double read_rss_mb() {
#ifdef __linux__
  std::ifstream statm("/proc/self/statm");
  std::uint64_t total_pages = 0;
  std::uint64_t resident_pages = 0;
  if (!(statm >> total_pages >> resident_pages)) {
    return 0.0;
  }
  (void)total_pages;
  const long page_size = sysconf(_SC_PAGESIZE);
  return static_cast<double>(resident_pages) * static_cast<double>(page_size) / (1024.0 * 1024.0);
#else
  return 0.0;
#endif
}

nlohmann::json read_soc_temp_c() {
#ifdef __linux__
  std::ifstream input("/sys/class/thermal/thermal_zone0/temp");
  long millidegrees = 0;
  if (input >> millidegrees) {
    return static_cast<double>(millidegrees) / 1000.0;
  }
#endif
  return nullptr;
}

double average_ms(std::uint64_t sum_us, std::uint64_t count) {
  if (count == 0) {
    return 0.0;
  }
  return static_cast<double>(sum_us) / static_cast<double>(count) / 1000.0;
}

double round_tenth(double value) { return std::round(value * 10.0) / 10.0; }

}  // namespace

Metrics::Metrics() : started_(std::chrono::steady_clock::now()) {}

void Metrics::on_capture_frame() { capture_frames_.fetch_add(1); }

void Metrics::on_detect_frame() { detect_frames_.fetch_add(1); }

void Metrics::on_motion_skip() { motion_skips_.fetch_add(1); }

void Metrics::on_dropped_frame() { dropped_frames_.fetch_add(1); }

void Metrics::add_infer_ms(double milliseconds) {
  if (milliseconds <= 0.0) {
    return;
  }
  infer_us_sum_.fetch_add(static_cast<std::uint64_t>(milliseconds * 1000.0));
  infer_count_.fetch_add(1);
}

void Metrics::add_llm_latency_ms(double milliseconds) {
  if (milliseconds <= 0.0) {
    return;
  }
  llm_us_sum_.fetch_add(static_cast<std::uint64_t>(milliseconds * 1000.0));
  llm_count_.fetch_add(1);
}

double Metrics::uptime_sec() const {
  const auto elapsed = std::chrono::steady_clock::now() - started_;
  return std::chrono::duration<double>(elapsed).count();
}

nlohmann::json Metrics::to_json() const {
  const double uptime = uptime_sec();
  const std::uint64_t capture_frames = capture_frames_.load();
  const std::uint64_t detect_frames = detect_frames_.load();
  const std::uint64_t motion_skips = motion_skips_.load();

  const double capture_fps =
      uptime > 0.0 ? static_cast<double>(capture_frames) / uptime : 0.0;
  const double detect_fps = uptime > 0.0 ? static_cast<double>(detect_frames) / uptime : 0.0;
  const std::uint64_t motion_total = motion_skips + detect_frames;
  const double motion_skip_ratio =
      motion_total > 0 ? static_cast<double>(motion_skips) / static_cast<double>(motion_total)
                       : 0.0;

  nlohmann::json json = {{"capture_fps", round_tenth(capture_fps)},
                         {"detect_fps", round_tenth(detect_fps)},
                         {"motion_skip_ratio", round_tenth(motion_skip_ratio)},
                         {"avg_infer_ms", round_tenth(average_ms(infer_us_sum_.load(),
                                                                 infer_count_.load()))},
                         {"avg_llm_latency_ms",
                          round_tenth(average_ms(llm_us_sum_.load(), llm_count_.load()))},
                         {"dropped_frames", dropped_frames_.load()},
                         {"rss_memory_mb", round_tenth(read_rss_mb())},
                         {"soc_temp_c", read_soc_temp_c()}};
  return json;
}

}  // namespace edge
