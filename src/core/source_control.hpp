#pragma once

#include <string>

#include "core/node_state.hpp"

namespace edge {

// Port used by the control plane to inspect and switch the capture source.
// Keeping it in edge_core prevents edge_api from depending on edge_capture.
class SourceControl {
 public:
  virtual ~SourceControl() = default;

  virtual bool switch_source(const std::string& source, bool loop, std::string& error) = 0;
  virtual NodeState state() const = 0;
  virtual const std::string& name() const = 0;
};

}  // namespace edge
