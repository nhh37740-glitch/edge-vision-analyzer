#pragma once

namespace edge {

enum class NodeState {
  kStarting,
  kRunning,
  kStopping,
  kStopped,
  kFailed,
  kNotEnabled,
};

inline const char* to_string(NodeState state) {
  switch (state) {
    case NodeState::kStarting:
      return "starting";
    case NodeState::kRunning:
      return "running";
    case NodeState::kStopping:
      return "stopping";
    case NodeState::kStopped:
      return "stopped";
    case NodeState::kFailed:
      return "failed";
    case NodeState::kNotEnabled:
      return "not_enabled";
  }
  return "unknown";
}

}  // namespace edge
