#include "core/base_node.hpp"

#include <utility>

namespace edge {
namespace {

// Process-wide drop total shared by all nodes. The API process is a single
// long-running instance, so a global atomic keeps aggregation trivial while
// staying lock-free on the enqueue path.
std::atomic<std::uint64_t> g_total_dropped_frames{0};

}  // namespace

BaseNode::BaseNode(std::string name, std::size_t queue_capacity)
    : name_(std::move(name)), input_queue_(queue_capacity) {}

BaseNode::~BaseNode() { stop(); }

void BaseNode::attach_next(std::shared_ptr<BaseNode> next) {
  if (next) {
    std::lock_guard<std::mutex> lock(next_mutex_);
    next_nodes_.push_back(std::move(next));
  }
}

void BaseNode::push(FrameMetaPtr meta) {
  if (!running_.load() || !meta) {
    return;
  }
  if (input_queue_.push(std::move(meta))) {
    record_dropped_frame();
  }
}

void BaseNode::record_dropped_frame() {
  dropped_frames_.fetch_add(1, std::memory_order_relaxed);
  g_total_dropped_frames.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t BaseNode::total_dropped_frames() {
  return g_total_dropped_frames.load(std::memory_order_relaxed);
}

void BaseNode::start() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (running_.load() || stopped_once_) {
    return;
  }
  state_.store(NodeState::kStarting);
  running_.store(true);
  worker_ = std::thread(&BaseNode::run, this);
  state_.store(NodeState::kRunning);
}

void BaseNode::stop() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  stopped_once_ = true;
  running_.store(false);
  input_queue_.close();
  if (worker_.joinable()) {
    state_.store(NodeState::kStopping);
    worker_.join();
  }
  state_.store(NodeState::kStopped);
}

FrameMetaPtr BaseNode::process(FrameMetaPtr meta) { return meta; }

void BaseNode::run() {
  FrameMetaPtr meta;
  bool failed = false;
  // pop() keeps returning queued frames after close() so that stop() drains
  // the queue instead of retaining frame buffers until destruction.
  while (input_queue_.pop(meta)) {
    if (!running_.load()) {
      // Frames still queued when the node stops are discarded without being
      // processed, so they count as runtime drops too.
      record_dropped_frame();
      continue;
    }
    try {
      FrameMetaPtr output = process(std::move(meta));
      if (output && running_.load()) {
        emit(std::move(output));
      }
    } catch (...) {
      failed = true;
      running_.store(false);
      input_queue_.close();
      break;
    }
  }
  state_.store(failed ? NodeState::kFailed : NodeState::kStopped);
}

void BaseNode::emit(FrameMetaPtr meta) {
  std::lock_guard<std::mutex> lock(next_mutex_);
  for (const auto& next : next_nodes_) {
    next->push(meta);
  }
}

}  // namespace edge
