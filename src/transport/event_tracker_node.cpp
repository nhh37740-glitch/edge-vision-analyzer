#include "transport/event_tracker_node.hpp"

#include <algorithm>
#include <utility>

namespace edge {

EventTrackerNode::EventTrackerNode(EventOptions options)
    : BaseNode("event_tracker"),
      options_(std::move(options)),
      end_grace_ms_(static_cast<std::int64_t>(options_.end_grace_sec) * 1000),
      cooldown_ms_(static_cast<std::int64_t>(options_.cooldown_sec) * 1000) {}

FrameMetaPtr EventTrackerNode::process(FrameMetaPtr meta) {
  if (!meta) {
    return meta;
  }

  GenAiTask task;
  bool submit_task = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // CaptureNode always fills timestamp_ms, either from the stream PTS or
    // from the system clock. Tests construct frames with explicit values.
    const std::int64_t now_ms = meta->timestamp_ms;
    TargetBox best;
    const bool has_target = find_best_target(meta->targets, best);

    if (has_target) {
      if (!active_) {
        active_ = true;
        triggered_ = false;
        active_event_id_ = make_event_id(now_ms, meta->frame_id);
        started_ms_ = now_ms;
        last_active_ms_ = now_ms;
        best_target_ = best;

        // The cooldown gates task creation, not the active state. Targets that
        // appear during the cooldown still form an event, but no task is
        // queued for them and there is no implicit retry.
        if (!has_trigger_ || now_ms - last_trigger_ms_ >= cooldown_ms_.load()) {
          triggered_ = true;
          has_trigger_ = true;
          last_trigger_ms_ = now_ms;
          task.event_id = active_event_id_;
          task.frame_id = meta->frame_id;
          task.timestamp_ms = now_ms;
          task.top_label = best.label;
          task.top_score = best.score;
          task.targets = meta->targets;
          // The clean frame reference is transferred to the task, so the
          // tracker holds no frame of its own after this point.
          task.frame = meta;
          submit_task = true;
        }
      } else {
        last_active_ms_ = now_ms;
        if (best.score > best_target_.score) {
          best_target_ = best;
        }
      }
      meta->active_event_id = active_event_id_;
    } else {
      if (active_ && now_ms - last_active_ms_ >= end_grace_ms_) {
        end_active_event_locked();
      }
      meta->active_event_id = active_event_id_;
    }
  }

  // Called outside the state lock: the callback only enqueues and must never
  // block the tracker worker.
  if (submit_task && options_.submit) {
    options_.submit(std::move(task));
  }
  return meta;
}

EventTrackerSnapshot EventTrackerNode::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  EventTrackerSnapshot state;
  state.active = active_;
  state.triggered = triggered_;
  state.event_id = active_event_id_;
  state.best_target = best_target_;
  state.started_ms = started_ms_;
  state.last_active_ms = last_active_ms_;
  state.last_trigger_ms = last_trigger_ms_;
  return state;
}

std::string EventTrackerNode::make_event_id(std::int64_t timestamp_ms,
                                            std::uint64_t frame_id) {
  return "evt_" + std::to_string(timestamp_ms) + "_" + std::to_string(frame_id);
}

bool EventTrackerNode::find_best_target(const std::vector<TargetBox>& targets,
                                        TargetBox& best) {
  if (targets.empty()) {
    return false;
  }
  best = *std::max_element(
      targets.begin(), targets.end(),
      [](const TargetBox& left, const TargetBox& right) {
        return left.score < right.score;
      });
  return true;
}

void EventTrackerNode::end_active_event_locked() {
  // No history is written. Keep last_trigger_ms_/has_trigger_ so the cooldown
  // still spans the gap before the next event.
  active_ = false;
  triggered_ = false;
  active_event_id_.clear();
  best_target_ = TargetBox{};
  started_ms_ = 0;
  last_active_ms_ = 0;
}

}  // namespace edge
