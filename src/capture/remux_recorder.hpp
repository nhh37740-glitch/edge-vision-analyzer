#pragma once

#include <chrono>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

namespace edge {

// Remuxes the input's original video packets into a container without
// re-encoding. Recordings keep only the video track.
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
  int input_video_stream_ = -1;
  int output_video_stream_ = -1;
  AVRational input_time_base_{1, 1};
  std::chrono::steady_clock::time_point started_{};
  int duration_sec_ = 0;
  bool header_written_ = false;
  bool finished_ = false;
};

}  // namespace edge
