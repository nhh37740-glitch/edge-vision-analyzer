#pragma once

#include <chrono>
#include <string>

#include "core/frame_meta.hpp"

namespace edge {

// Saves a JPEG every interval_sec while the pipeline is running. The output
// directory is created on demand and is expected to be git-ignored.
class FrameDumper {
 public:
  FrameDumper(std::string directory, int interval_sec);

  void maybe_dump(const FrameMeta& frame);

 private:
  std::string directory_;
  int interval_sec_;
  std::chrono::steady_clock::time_point last_dump_{};
  bool has_dumped_ = false;
};

}  // namespace edge
