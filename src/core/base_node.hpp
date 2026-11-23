#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/bounded_queue.hpp"
#include "core/frame_meta.hpp"
#include "core/node_state.hpp"

namespace edge {

// Base class for every frame-processing stage. Each node owns one worker
// thread and one fixed-capacity input queue. Nodes are chained with
// attach_next() before start(); a node never spawns ad-hoc threads.
// A node has one lifecycle: start() after stop() is a no-op because the
// input queue stays closed.
class BaseNode {
 public:
  explicit BaseNode(std::string name, std::size_t queue_capacity = 2);
  virtual ~BaseNode();

  BaseNode(const BaseNode&) = delete;
  BaseNode& operator=(const BaseNode&) = delete;

  void attach_next(std::shared_ptr<BaseNode> next);
  void push(FrameMetaPtr meta);
  void start();
  void stop();

  bool running() const { return running_.load(); }
  NodeState state() const { return state_.load(); }
  const std::string& name() const { return name_; }
  std::uint64_t dropped_frames() const { return dropped_frames_.load(); }

  // Cumulative drops across every node in this process. The stats endpoint
  // adds this to its Metrics snapshot.
  static std::uint64_t total_dropped_frames();

 protected:
  virtual FrameMetaPtr process(FrameMetaPtr meta);
  virtual void run();
  void emit(FrameMetaPtr meta);
  bool stop_requested() const { return !running_.load(); }
  void set_state(NodeState state) { state_.store(state); }

 private:
  void record_dropped_frame();

  std::string name_;
  BoundedQueue<FrameMetaPtr> input_queue_;
  std::atomic<std::uint64_t> dropped_frames_{0};
  std::vector<std::shared_ptr<BaseNode>> next_nodes_;
  std::mutex next_mutex_;
  std::mutex lifecycle_mutex_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<NodeState> state_{NodeState::kStopped};
  bool stopped_once_ = false;
};

}  // namespace edge
