#include "core/base_node.hpp"

#include <utility>

namespace edge {

BaseNode::BaseNode(std::string name, std::size_t queue_capacity)
    : name_(std::move(name)), input_queue_(queue_capacity) {}

BaseNode::~BaseNode() { stop(); }

void BaseNode::attach_next(std::shared_ptr<BaseNode> next) {
  if (next) {
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
  if (running_.exchange(true)) {
    return;
  }
  state_.store(NodeState::kStarting);
  worker_ = std::thread(&BaseNode::run, this);
  state_.store(NodeState::kRunning);
}

void BaseNode::stop() {
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
  while (running_.load() && input_queue_.pop(meta)) {
    if (!running_.load()) {
      break;
    }
    FrameMetaPtr output = process(std::move(meta));
    if (output && running_.load()) {
      emit(std::move(output));
    }
  }
  state_.store(NodeState::kStopped);
}

void BaseNode::emit(FrameMetaPtr meta) {
  for (const auto& next : next_nodes_) {
    next->push(meta);
  }
}

}  // namespace edge
