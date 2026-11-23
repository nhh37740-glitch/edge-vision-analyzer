#include "core/base_node.hpp"

#include <utility>

namespace edge {

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
  input_queue_.push(std::move(meta));
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
