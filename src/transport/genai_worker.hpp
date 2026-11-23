#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/config.hpp"
#include "core/bounded_queue.hpp"
#include "core/frame_meta.hpp"

namespace edge {

// Snapshot of the event metadata taken when EventTrackerNode triggers. The
// frame is the clean, un-OSD-marked RGB buffer captured at trigger time.
struct GenAiTask {
  std::string event_id;
  std::uint64_t frame_id = 0;
  std::int64_t timestamp_ms = 0;
  std::string top_label;
  float top_score = 0.0f;
  std::vector<TargetBox> targets;
  FrameMetaPtr frame;
};

struct GenAiResult {
  std::string event_id;
  std::string llm_summary;
  double llm_latency_ms = 0.0;
  std::size_t jpeg_bytes = 0;
  bool storage_delivered = false;
};

struct GenAiStats {
  std::uint64_t submitted = 0;
  std::uint64_t dropped = 0;
  std::uint64_t processed = 0;
  std::uint64_t encoded = 0;
  std::uint64_t llm_requests = 0;
  std::uint64_t llm_failures = 0;
  std::uint64_t storage_requests = 0;
  std::uint64_t storage_failures = 0;
};

// Single background thread with a capacity-1 task queue. A newly submitted
// task replaces the pending one, and the replaced task's frame reference is
// released immediately. All HTTP waits happen on the worker thread only.
class GenAiWorker {
 public:
  GenAiWorker(LlmConfig llm, StorageConfig storage);
  ~GenAiWorker();

  GenAiWorker(const GenAiWorker&) = delete;
  GenAiWorker& operator=(const GenAiWorker&) = delete;

  void start();
  void stop();

  // Returns false when the worker is not running. A full queue keeps only the
  // latest task and releases the old task's frame reference.
  bool submit(GenAiTask task);

  bool running() const { return running_.load(); }
  GenAiStats stats() const;
  GenAiResult last_result() const;

 private:
  void run();
  void handle(GenAiTask task);
  bool request_llm(const std::string& image_base64, std::string& summary);
  bool send_storage(const GenAiTask& task, const std::string& image_base64,
                    const std::string& summary, double latency_ms) const;

  LlmConfig llm_;
  StorageConfig storage_;
  BoundedQueue<GenAiTask> queue_{1};
  std::thread worker_;
  std::atomic<bool> running_{false};

  std::atomic<std::uint64_t> submitted_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> processed_{0};
  std::atomic<std::uint64_t> encoded_{0};
  std::atomic<std::uint64_t> llm_requests_{0};
  std::atomic<std::uint64_t> llm_failures_{0};
  std::atomic<std::uint64_t> storage_requests_{0};
  std::atomic<std::uint64_t> storage_failures_{0};

  mutable std::mutex result_mutex_;
  GenAiResult last_result_;
};

}  // namespace edge
