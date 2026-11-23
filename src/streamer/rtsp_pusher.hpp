#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/frame_meta.hpp"

namespace edge {

// Owns the input side of an external encoder process. Production code starts a
// real `ffmpeg` child; tests inject a fake that records the command line and
// simulates a pipe. Write() is non-blocking: it returns the number of bytes
// accepted, 0 when the pipe is full, and -1 when the pipe is broken.
class ProcessSink {
 public:
  virtual ~ProcessSink() = default;

  virtual bool Start(const std::vector<std::string>& command,
                     std::string* error) = 0;
  virtual std::ptrdiff_t Write(const std::uint8_t* data,
                               std::size_t size) = 0;
  virtual void CloseInput() = 0;
  virtual int Wait() = 0;
};

std::shared_ptr<ProcessSink> make_default_process_sink();

// Optional RTSP output. In `mjpeg` mode it never starts a process. In `rtsp`
// mode it spawns an external ffmpeg that reads rawvideo rgb24 from stdin and
// pushes H.264 to the configured RTSP URL. Frames are handed to a bounded
// writer queue; when the queue is full the frame is dropped and counted so the
// caller (a video pipeline thread) never blocks on the encoder.
class RtspPusher {
 public:
  static constexpr std::size_t kMaxQueuedFrames = 2;

  RtspPusher(std::string mode, std::string rtsp_url, int width, int height,
             int fps, std::string ffmpeg_path = "ffmpeg",
             std::shared_ptr<ProcessSink> sink = nullptr);
  ~RtspPusher();

  RtspPusher(const RtspPusher&) = delete;
  RtspPusher& operator=(const RtspPusher&) = delete;

  // Starts ffmpeg once for rtsp mode. Repeated calls while running are a
  // no-op. Returns false and stores last_error() when launching is impossible.
  bool start();
  void push(const FrameMeta& frame);
  // Closes stdin, joins the writer thread and reaps the child. Idempotent.
  void stop();

  bool running() const { return running_.load(); }
  std::uint64_t dropped_frames() const { return dropped_.load(); }
  std::string last_error() const;

 private:
  void writer_loop();
  bool write_frame(const std::vector<std::uint8_t>& data);

  static std::vector<std::string> build_command(
      const std::string& ffmpeg_path, int width, int height, int fps,
      const std::string& rtsp_url);

  std::string mode_;
  std::string rtsp_url_;
  std::string ffmpeg_path_;
  int width_;
  int height_;
  int fps_;

  std::shared_ptr<ProcessSink> injected_sink_;
  std::shared_ptr<ProcessSink> sink_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<std::uint8_t>> queue_;
  std::thread writer_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
  std::string last_error_;
};

}  // namespace edge
