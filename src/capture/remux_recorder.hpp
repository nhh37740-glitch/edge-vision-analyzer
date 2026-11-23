#pragma once

#include <chrono>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

namespace edge {

// Remuxes the input's original packets into a container without re-encoding.
// It is used only by the explicit --record-clip path.
class RemuxRecorder {
 public:
  RemuxRecorder() = default;
  ~RemuxRecorder();

  RemuxRecorder(const RemuxRecorder&) = delete;
  RemuxRecorder& operator=(const RemuxRecorder&) = delete;

  bool open(const std::string& path, AVFormatContext* input, int duration_sec,
            std::string& error);
  void write_packet(const AVPacket* packet);
  void close();

  bool finished() const { return finished_; }

 private:
  AVFormatContext* output_ = nullptr;
  std::vector<int> stream_map_;
  std::vector<AVRational> input_time_bases_;
  std::chrono::steady_clock::time_point started_{};
  int duration_sec_ = 0;
  bool header_written_ = false;
  bool finished_ = false;
};

}  // namespace edge
