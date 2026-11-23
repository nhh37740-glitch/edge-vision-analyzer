#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/base_node.hpp"
#include "core/frame_meta.hpp"
#include "transport/genai_worker.hpp"

namespace edge {

// Instantaneous event state only. The tracker keeps no history and never
// writes to disk; the submit callback normally forwards to GenAiWorker.
struct EventOptions {
  int end_grace_sec = 5;
  int cooldown_sec = 15;
  std::function<bool(GenAiTask)> submit;
};

// Read-only view of the tracker state for diagnostics and tests. process()
// runs on the node worker thread, so this accessor locks the same mutex.
struct EventTrackerSnapshot {
  bool active = false;
  bool triggered = false;
  std::string event_id;
  TargetBox best_target;
  std::int64_t started_ms = 0;
  std::int64_t last_active_ms = 0;
  std::int64_t last_trigger_ms = 0;
};

// Aggregates detection results into at most one active event and throttles
// GenAiTask submission with a trigger cooldown. Continuous targets only
// refresh the active window; a gap longer than end_grace_sec releases the
// state. Submission is synchronous and non-blocking by contract.
class EventTrackerNode : public BaseNode {
 public:
  explicit EventTrackerNode(EventOptions options);

  // Runtime update for PATCH /api/v1/config; the next trigger uses it.
  void set_cooldown_sec(int cooldown_sec) {
    cooldown_ms_.store(static_cast<std::int64_t>(cooldown_sec) * 1000);
  }

  EventTrackerSnapshot snapshot() const;

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override;

 private:
  static std::string make_event_id(std::int64_t timestamp_ms,
                                   std::uint64_t frame_id);
  static bool find_best_target(const std::vector<TargetBox>& targets,
                               TargetBox& best);
  void end_active_event_locked();

  EventOptions options_;
  const std::int64_t end_grace_ms_;
  std::atomic<std::int64_t> cooldown_ms_;

  mutable std::mutex mutex_;
  bool active_ = false;
  bool triggered_ = false;
  bool has_trigger_ = false;
  std::string active_event_id_;
  TargetBox best_target_;
  std::int64_t started_ms_ = 0;
  std::int64_t last_active_ms_ = 0;
  std::int64_t last_trigger_ms_ = 0;
};

}  // namespace edge
