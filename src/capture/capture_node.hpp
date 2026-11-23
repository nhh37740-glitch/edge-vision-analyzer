#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "common/metrics.hpp"
#include "core/base_node.hpp"

struct AVPacket;

namespace edge {

class FrameDumper;
class RemuxRecorder;

// Pulls one HTTP/RTSP/local source with FFmpeg, converts frames to RGB24 and
// emits them downstream. Source switching happens by fully opening the new
// source before atomically replacing the current one.
class CaptureNode : public BaseNode {
 public:
  struct Options {
    std::string source;
    bool loop = false;
    int width = 640;
    int height = 480;
    int target_fps = 15;
    int dump_interval_sec = 0;
    std::string dump_dir = "snapshots";
    std::string record_path;
    int record_duration_sec = 0;
  };

  CaptureNode(Options options, std::shared_ptr<Metrics> metrics);
  ~CaptureNode() override;

  bool switch_source(const std::string& source, bool loop, std::string& error);
  std::string source() const;
  bool loop() const;
  bool recording_finished() const { return recording_finished_.load(); }
  std::uint64_t frames_captured() const { return frames_captured_.load(); }

 protected:
  void run() override;

 private:
  class Source;

  std::shared_ptr<Source> open_source(const std::string& location, bool loop, std::string& error);
  std::shared_ptr<Source> current_source() const;
  std::shared_ptr<Source> install_reconnected_source(const std::shared_ptr<Source>& candidate,
                                                     const std::string& location, bool loop);
  void mark_source_failed(const std::shared_ptr<Source>& source);
  void handle_packet(const std::shared_ptr<Source>& source, const AVPacket* packet);
  bool sleep_interruptible(std::chrono::milliseconds duration) const;

  Options options_;
  std::shared_ptr<Metrics> metrics_;
  mutable std::mutex source_mutex_;
  std::shared_ptr<Source> source_;
  std::string source_location_;
  bool loop_ = false;
  std::unique_ptr<FrameDumper> dumper_;
  std::unique_ptr<RemuxRecorder> recorder_;
  bool record_attempted_ = false;
  std::atomic<std::uint64_t> frames_captured_{0};
  std::atomic<bool> recording_active_{false};
  std::atomic<bool> recording_finished_{false};
};

}  // namespace edge
