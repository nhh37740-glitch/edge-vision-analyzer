#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>

#include "common/config.hpp"
#include "core/base_node.hpp"
#include "core/bounded_queue.hpp"
#include "core/frame_meta.hpp"

namespace {

using edge::BaseNode;
using edge::FrameMeta;
using edge::FrameMetaPtr;

class CountingNode : public BaseNode {
 public:
  CountingNode(std::string name, std::promise<void>* done)
      : BaseNode(std::move(name)), done_(done) {}

  std::atomic<int> processed{0};
  std::atomic<std::uint64_t> last_frame_id{0};

 protected:
  FrameMetaPtr process(FrameMetaPtr meta) override {
    last_frame_id.store(meta->frame_id);
    processed.fetch_add(1);
    if (done_) {
      done_->set_value();
      done_ = nullptr;
    }
    return meta;
  }

 private:
  std::promise<void>* done_;
};

TEST(BoundedQueue, DropsOldestWhenFull) {
  edge::BoundedQueue<int> queue(2);
  EXPECT_FALSE(queue.push(1));
  EXPECT_FALSE(queue.push(2));
  EXPECT_TRUE(queue.push(3));

  int value = 0;
  ASSERT_TRUE(queue.pop(value));
  EXPECT_EQ(value, 2);
  ASSERT_TRUE(queue.pop(value));
  EXPECT_EQ(value, 3);
  EXPECT_EQ(queue.dropped(), 1u);
}

TEST(BaseNode, ChainsFramesWithOneWorkerPerNode) {
  auto first = std::make_shared<CountingNode>("first", nullptr);
  std::promise<void> done;
  auto second = std::make_shared<CountingNode>("second", &done);
  first->attach_next(second);

  second->start();
  first->start();

  auto meta = std::make_shared<FrameMeta>();
  meta->frame_id = 42;
  first->push(meta);

  ASSERT_EQ(done.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_EQ(second->last_frame_id.load(), 42u);
  EXPECT_EQ(first->processed.load(), 1);
  EXPECT_EQ(second->processed.load(), 1);

  first->stop();
  second->stop();
}

TEST(BaseNode, StopJoinsWorker) {
  auto node = std::make_shared<CountingNode>("node", nullptr);
  node->start();
  EXPECT_TRUE(node->running());
  node->stop();
  EXPECT_FALSE(node->running());
  EXPECT_EQ(node->state(), edge::NodeState::kStopped);
}

TEST(BaseNode, StartAfterStopIsNoOp) {
  auto node = std::make_shared<CountingNode>("node", nullptr);
  node->start();
  node->stop();
  node->start();
  EXPECT_FALSE(node->running());
  EXPECT_EQ(node->state(), edge::NodeState::kStopped);
}

TEST(Config, LoadsExampleAndSerializesEverySection) {
  const auto config = edge::Config::load(EDGE_CONFIG_EXAMPLE);
  EXPECT_EQ(config.input.width, 640);
  EXPECT_EQ(config.input.height, 480);
  EXPECT_EQ(config.input.target_fps, 15);
  EXPECT_EQ(config.detector.target_labels.at(0), "person");
  EXPECT_EQ(config.streamer.http_port, 8081);

  const nlohmann::json json = config;
  EXPECT_EQ(json.at("llm").at("endpoint"), config.llm.endpoint);
  EXPECT_EQ(json.at("storage").at("timeout_sec"), config.storage.timeout_sec);
  EXPECT_EQ(json.at("event").at("end_grace_sec"), config.event.end_grace_sec);
}

}  // namespace
