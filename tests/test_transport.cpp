#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/config.hpp"
#include "core/frame_meta.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "transport/event_tracker_node.hpp"
#include "transport/genai_worker.hpp"

namespace {

using edge::EventOptions;
using edge::EventTrackerNode;
using edge::EventTrackerSnapshot;
using edge::FrameMeta;
using edge::FrameMetaPtr;
using edge::GenAiTask;
using edge::GenAiWorker;
using edge::LlmConfig;
using edge::StorageConfig;
using edge::TargetBox;

using namespace std::chrono_literals;

FrameMetaPtr make_frame(std::uint64_t frame_id, std::uint8_t seed) {
  auto frame = std::make_shared<FrameMeta>();
  frame->frame_id = frame_id;
  frame->timestamp_ms = 1795412000123;
  frame->width = 8;
  frame->height = 8;
  frame->rgb_data.resize(static_cast<std::size_t>(frame->width) * frame->height * 3);
  for (std::size_t i = 0; i < frame->rgb_data.size(); ++i) {
    frame->rgb_data[i] = static_cast<std::uint8_t>((i * 37 + seed * 11) & 0xFF);
  }
  return frame;
}

FrameMetaPtr make_detection_frame(std::uint64_t frame_id, std::int64_t timestamp_ms,
                                  float score) {
  FrameMetaPtr frame = make_frame(frame_id, static_cast<std::uint8_t>(frame_id));
  frame->timestamp_ms = timestamp_ms;
  TargetBox box;
  box.class_id = 0;
  box.label = "person";
  box.score = score;
  box.x = 0.2f;
  box.y = 0.1f;
  box.w = 0.3f;
  box.h = 0.6f;
  frame->targets.push_back(box);
  return frame;
}

FrameMetaPtr make_silent_frame(std::uint64_t frame_id, std::int64_t timestamp_ms) {
  FrameMetaPtr frame = make_frame(frame_id, static_cast<std::uint8_t>(frame_id));
  frame->timestamp_ms = timestamp_ms;
  return frame;
}

// Exposes the protected process() hook so tracker state can be driven without
// starting the node worker thread.
class FeedableEventTracker : public EventTrackerNode {
 public:
  explicit FeedableEventTracker(EventOptions options)
      : EventTrackerNode(std::move(options)) {}

  FrameMetaPtr feed(FrameMetaPtr meta) { return process(std::move(meta)); }
};

GenAiTask make_task(FrameMetaPtr frame, std::string event_id) {
  GenAiTask task;
  task.event_id = std::move(event_id);
  task.frame_id = frame->frame_id;
  task.timestamp_ms = frame->timestamp_ms;
  task.top_label = "person";
  task.top_score = 0.87f;
  TargetBox box;
  box.class_id = 0;
  box.label = "person";
  box.score = 0.87f;
  box.x = 0.21f;
  box.y = 0.15f;
  box.w = 0.30f;
  box.h = 0.68f;
  task.targets.push_back(box);
  task.frame = std::move(frame);
  return task;
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 3000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return predicate();
}

LlmConfig llm_config(int port, bool enabled) {
  LlmConfig llm;
  llm.enabled = enabled;
  llm.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/v1/chat/completions";
  llm.model = "vlm-local";
  llm.prompt = "用一句简短的中文描述画面中人物正在做什么。";
  llm.connect_timeout_sec = 2;
  llm.read_timeout_sec = 2;
  llm.jpeg_quality = 70;
  return llm;
}

StorageConfig storage_config(int port, bool enabled) {
  StorageConfig storage;
  storage.enabled = enabled;
  storage.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/api/v1/events";
  storage.timeout_sec = 2;
  return storage;
}

// Loopback-only mock server. Tests never reach an external host.
class MockServer {
 public:
  MockServer() : port_(server_.bind_to_any_port("127.0.0.1")) {}
  ~MockServer() { stop(); }

  MockServer(const MockServer&) = delete;
  MockServer& operator=(const MockServer&) = delete;

  int port() const { return port_; }
  httplib::Server& server() { return server_; }

  void start() {
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  void stop() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  httplib::Server server_;
  int port_ = 0;
  std::thread thread_;
};

TEST(Transport, BothSwitchesOffSkipsEncodingAndHttp) {
  MockServer mock;
  ASSERT_GT(mock.port(), 0);
  std::atomic<int> requests{0};
  mock.server().Post("/v1/chat/completions",
                     [&](const httplib::Request&, httplib::Response& response) {
                       requests.fetch_add(1);
                       response.set_content(
                           R"({"choices":[{"message":{"content":"unused"}}]})",
                           "application/json");
                     });
  mock.server().Post("/api/v1/events",
                     [&](const httplib::Request&, httplib::Response& response) {
                       requests.fetch_add(1);
                       response.status = 201;
                     });
  mock.start();

  GenAiWorker worker(llm_config(mock.port(), false), storage_config(mock.port(), false));
  worker.start();

  FrameMetaPtr frame = make_frame(1, 3);
  std::weak_ptr<FrameMeta> weak_frame = frame;
  ASSERT_TRUE(worker.submit(make_task(std::move(frame), "evt_off")));

  ASSERT_TRUE(wait_until([&] {
    return worker.stats().processed == 1 && weak_frame.expired();
  }));
  const edge::GenAiStats stats = worker.stats();
  EXPECT_EQ(stats.encoded, 0u);
  EXPECT_EQ(stats.llm_requests, 0u);
  EXPECT_EQ(stats.storage_requests, 0u);
  EXPECT_EQ(requests.load(), 0);
  worker.stop();
}

TEST(Transport, LlmOnlySendsOpenAiVisionRequestAndParsesSummary) {
  MockServer mock;
  ASSERT_GT(mock.port(), 0);
  std::mutex mutex;
  std::string request_body;
  std::atomic<int> requests{0};
  mock.server().Post("/v1/chat/completions",
                     [&](const httplib::Request& request, httplib::Response& response) {
                       {
                         std::lock_guard<std::mutex> lock(mutex);
                         request_body = request.body;
                       }
                       requests.fetch_add(1);
                       response.set_content(
                           R"({"choices":[{"message":{"content":"一名男子正在走动。"}}]})",
                           "application/json");
                     });
  mock.start();

  GenAiWorker worker(llm_config(mock.port(), true), storage_config(mock.port(), false));
  worker.start();
  ASSERT_TRUE(worker.submit(make_task(make_frame(2, 5), "evt_llm")));
  ASSERT_TRUE(wait_until([&] {
    return worker.stats().processed == 1 && requests.load() == 1;
  }));

  std::string body;
  {
    std::lock_guard<std::mutex> lock(mutex);
    body = request_body;
  }
  const nlohmann::json request = nlohmann::json::parse(body);
  EXPECT_EQ(request.at("model").get<std::string>(), "vlm-local");
  EXPECT_EQ(request.at("max_tokens").get<int>(), 128);
  EXPECT_DOUBLE_EQ(request.at("temperature").get<double>(), 0.2);
  ASSERT_EQ(request.at("messages").size(), 1u);
  const auto& content = request.at("messages").at(0).at("content");
  ASSERT_EQ(content.size(), 2u);
  EXPECT_EQ(content.at(0).at("type").get<std::string>(), "text");
  EXPECT_EQ(content.at(0).at("text").get<std::string>(),
            "用一句简短的中文描述画面中人物正在做什么。");
  EXPECT_EQ(content.at(1).at("type").get<std::string>(), "image_url");
  const std::string url = content.at(1).at("image_url").at("url").get<std::string>();
  const std::string prefix = "data:image/jpeg;base64,";
  ASSERT_EQ(url.rfind(prefix, 0), 0u);
  EXPECT_GT(url.size(), prefix.size());
  EXPECT_EQ(url.substr(prefix.size(), 4), "/9j/");

  const edge::GenAiStats stats = worker.stats();
  EXPECT_EQ(stats.encoded, 1u);
  EXPECT_EQ(stats.llm_requests, 1u);
  EXPECT_EQ(stats.llm_failures, 0u);
  const edge::GenAiResult result = worker.last_result();
  EXPECT_EQ(result.event_id, "evt_llm");
  EXPECT_EQ(result.llm_summary, "一名男子正在走动。");
  EXPECT_GE(result.llm_latency_ms, 0.0);
  worker.stop();
}

TEST(Transport, LlmRequestUsesSelectedModelAndFallsBackToBase) {
  MockServer mock;
  ASSERT_GT(mock.port(), 0);
  std::mutex mutex;
  std::vector<std::string> requested_models;
  std::atomic<int> requests{0};

  mock.server().Get("/v1/models", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(R"({"data":[{"id":"vlm-local"},{"id":"vlm-q5"}]})",
                         "application/json");
  });
  mock.server().Post("/v1/chat/completions",
                     [&](const httplib::Request& request, httplib::Response& response) {
                       {
                         std::lock_guard<std::mutex> lock(mutex);
                         requested_models.push_back(
                             nlohmann::json::parse(request.body).at("model").get<std::string>());
                       }
                       requests.fetch_add(1);
                       response.set_content(
                           R"({"choices":[{"message":{"content":"ok"}}]})",
                           "application/json");
                     });
  mock.start();

  LlmConfig llm = llm_config(mock.port(), true);
  llm.quantization = "auto";
  llm.model_candidates = {{"q8", "vlm-q8"}, {"q5", "vlm-q5"}};

  // The startup probe sees only vlm-q5, so auto skips q8 and sends q5.
  {
    GenAiWorker worker(llm, storage_config(mock.port(), false));
    worker.start();
    EXPECT_EQ(worker.selected_model(), "vlm-q5");
    EXPECT_EQ(worker.selected_quantization(), "q5");
    ASSERT_TRUE(worker.submit(make_task(make_frame(20, 4), "evt_selected")));
    ASSERT_TRUE(wait_until([&] {
      return worker.stats().processed == 1 && requests.load() == 1;
    }));
    worker.stop();
  }

  // An explicit q8 request whose alias is absent falls back to the base model.
  llm.quantization = "q8";
  edge::LlmResources resources;
  resources.available_models = {"vlm-local"};
  GenAiWorker fallback(llm, storage_config(mock.port(), false), resources);
  EXPECT_EQ(fallback.selected_model(), "vlm-local");
  EXPECT_EQ(fallback.selected_quantization(), "none");
  fallback.start();
  ASSERT_TRUE(fallback.submit(make_task(make_frame(21, 5), "evt_fallback")));
  ASSERT_TRUE(wait_until([&] {
    return fallback.stats().processed == 1 && requests.load() == 2;
  }));
  fallback.stop();

  std::lock_guard<std::mutex> lock(mutex);
  ASSERT_EQ(requested_models.size(), 2u);
  EXPECT_EQ(requested_models[0], "vlm-q5");
  EXPECT_EQ(requested_models[1], "vlm-local");
}

TEST(Transport, LlmFailureSendsEmptySummaryToStorageAfterLlm) {
  MockServer mock;
  ASSERT_GT(mock.port(), 0);
  std::mutex mutex;
  std::vector<std::string> order;
  std::string storage_body;
  std::atomic<int> llm_requests{0};
  std::atomic<int> storage_requests{0};

  mock.server().Post("/v1/chat/completions",
                     [&](const httplib::Request&, httplib::Response& response) {
                       {
                         std::lock_guard<std::mutex> lock(mutex);
                         order.push_back("llm");
                       }
                       llm_requests.fetch_add(1);
                       response.status = 500;
                       response.set_content(R"({"error":"inference failed"})",
                                            "application/json");
                     });
  mock.server().Post("/api/v1/events",
                     [&](const httplib::Request& request, httplib::Response& response) {
                       {
                         std::lock_guard<std::mutex> lock(mutex);
                         order.push_back("storage");
                         storage_body = request.body;
                       }
                       storage_requests.fetch_add(1);
                       response.status = 201;
                     });
  mock.start();

  GenAiWorker worker(llm_config(mock.port(), true), storage_config(mock.port(), true));
  worker.start();
  ASSERT_TRUE(worker.submit(make_task(make_frame(42, 7), "evt_fail")));
  ASSERT_TRUE(wait_until([&] {
    return worker.stats().processed == 1 && storage_requests.load() == 1;
  }));

  {
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "llm");
    EXPECT_EQ(order[1], "storage");
  }
  EXPECT_EQ(llm_requests.load(), 1);

  const nlohmann::json event = nlohmann::json::parse(storage_body);
  EXPECT_EQ(event.at("event_id").get<std::string>(), "evt_fail");
  EXPECT_EQ(event.at("frame_id").get<std::uint64_t>(), 42u);
  EXPECT_EQ(event.at("timestamp_ms").get<std::int64_t>(), 1795412000123);
  EXPECT_EQ(event.at("top_label").get<std::string>(), "person");
  EXPECT_NEAR(event.at("top_score").get<double>(), 0.87, 1e-6);
  ASSERT_EQ(event.at("targets").size(), 1u);
  const auto& box = event.at("targets").at(0);
  EXPECT_EQ(box.at("label").get<std::string>(), "person");
  EXPECT_EQ(box.at("class_id").get<int>(), 0);
  EXPECT_NEAR(box.at("score").get<double>(), 0.87, 1e-6);
  EXPECT_NEAR(box.at("x").get<double>(), 0.21, 1e-6);
  EXPECT_NEAR(box.at("y").get<double>(), 0.15, 1e-6);
  EXPECT_NEAR(box.at("w").get<double>(), 0.30, 1e-6);
  EXPECT_NEAR(box.at("h").get<double>(), 0.68, 1e-6);
  EXPECT_EQ(event.at("llm_summary").get<std::string>(), "");
  EXPECT_GE(event.at("llm_latency_ms").get<double>(), 0.0);
  const std::string image = event.at("image_base64").get<std::string>();
  EXPECT_EQ(image.rfind("data:image/jpeg;base64,", 0), std::string::npos);
  EXPECT_EQ(image.substr(0, 4), "/9j/");

  const edge::GenAiStats stats = worker.stats();
  EXPECT_EQ(stats.encoded, 1u);
  EXPECT_EQ(stats.llm_failures, 1u);
  EXPECT_EQ(stats.storage_requests, 1u);
  EXPECT_EQ(stats.storage_failures, 0u);
  EXPECT_TRUE(worker.last_result().storage_delivered);
  worker.stop();
}

TEST(Transport, QueueKeepsLatestTaskAndReleasesDroppedFrame) {
  MockServer mock;
  ASSERT_GT(mock.port(), 0);
  std::mutex mutex;
  std::condition_variable cv;
  bool llm_started = false;
  bool release_llm = false;
  std::atomic<int> llm_requests{0};

  mock.server().Post("/v1/chat/completions",
                     [&](const httplib::Request&, httplib::Response& response) {
                       std::unique_lock<std::mutex> lock(mutex);
                       llm_started = true;
                       cv.notify_all();
                       cv.wait(lock, [&] { return release_llm; });
                       llm_requests.fetch_add(1);
                       response.set_content(
                           R"({"choices":[{"message":{"content":"ok"}}]})",
                           "application/json");
                     });
  mock.start();

  LlmConfig llm = llm_config(mock.port(), true);
  llm.read_timeout_sec = 5;
  GenAiWorker worker(llm, storage_config(mock.port(), false));
  worker.start();

  FrameMetaPtr first = make_frame(10, 1);
  std::weak_ptr<FrameMeta> weak_first = first;
  ASSERT_TRUE(worker.submit(make_task(std::move(first), "evt_first")));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, 3000ms, [&] { return llm_started; }));
  }

  FrameMetaPtr second = make_frame(11, 2);
  std::weak_ptr<FrameMeta> weak_second = second;
  ASSERT_TRUE(worker.submit(make_task(std::move(second), "evt_second")));
  second.reset();

  FrameMetaPtr third = make_frame(12, 3);
  std::weak_ptr<FrameMeta> weak_third = third;
  ASSERT_TRUE(worker.submit(make_task(std::move(third), "evt_third")));
  third.reset();

  // The capacity-1 queue replaced the second task and released its frame.
  EXPECT_TRUE(weak_second.expired());
  EXPECT_FALSE(weak_third.expired());

  {
    std::lock_guard<std::mutex> lock(mutex);
    release_llm = true;
  }
  cv.notify_all();

  ASSERT_TRUE(wait_until([&] { return worker.stats().processed == 2; }));
  const edge::GenAiStats stats = worker.stats();
  EXPECT_EQ(stats.dropped, 1u);
  EXPECT_EQ(stats.encoded, 2u);
  EXPECT_EQ(stats.llm_requests, 2u);
  worker.stop();
  EXPECT_TRUE(weak_first.expired());
  EXPECT_TRUE(weak_third.expired());
}

TEST(Transport, EventTrackerSubmitsOneTaskPerEvent) {
  std::vector<GenAiTask> tasks;
  EventOptions options;
  options.end_grace_sec = 5;
  options.cooldown_sec = 15;
  options.submit = [&tasks](GenAiTask task) {
    tasks.push_back(std::move(task));
    return true;
  };
  FeedableEventTracker tracker(options);

  tracker.feed(make_detection_frame(1, 1000, 0.60f));
  tracker.feed(make_detection_frame(2, 1200, 0.92f));
  FrameMetaPtr output = tracker.feed(make_detection_frame(3, 1400, 0.75f));

  ASSERT_EQ(tasks.size(), 1u);
  EXPECT_EQ(tasks[0].event_id, "evt_1000_1");
  EXPECT_EQ(tasks[0].frame_id, 1u);
  EXPECT_EQ(tasks[0].timestamp_ms, 1000);
  EXPECT_EQ(tasks[0].top_label, "person");
  EXPECT_NEAR(tasks[0].top_score, 0.60, 1e-6);
  ASSERT_EQ(tasks[0].targets.size(), 1u);
  ASSERT_TRUE(tasks[0].frame != nullptr);
  EXPECT_EQ(tasks[0].frame->frame_id, 1u);
  EXPECT_EQ(output->active_event_id, "evt_1000_1");

  const EventTrackerSnapshot state = tracker.snapshot();
  EXPECT_TRUE(state.active);
  EXPECT_TRUE(state.triggered);
  EXPECT_EQ(state.event_id, "evt_1000_1");
  EXPECT_NEAR(state.best_target.score, 0.92, 1e-6);
  EXPECT_EQ(state.started_ms, 1000);
  EXPECT_EQ(state.last_active_ms, 1400);
  EXPECT_EQ(state.last_trigger_ms, 1000);
}

TEST(Transport, EventTrackerCooldownBlocksTaskButKeepsActiveState) {
  std::vector<GenAiTask> tasks;
  EventOptions options;
  options.end_grace_sec = 2;
  options.cooldown_sec = 15;
  options.submit = [&tasks](GenAiTask task) {
    tasks.push_back(std::move(task));
    return true;
  };
  FeedableEventTracker tracker(options);

  tracker.feed(make_detection_frame(10, 1000, 0.50f));
  ASSERT_EQ(tasks.size(), 1u);

  // The event ends after the 2 second grace window.
  tracker.feed(make_silent_frame(11, 4000));
  ASSERT_FALSE(tracker.snapshot().active);

  // Cooldown still blocks the task, but the active state is established.
  FrameMetaPtr output = tracker.feed(make_detection_frame(12, 5000, 0.60f));
  EXPECT_EQ(tasks.size(), 1u);
  EventTrackerSnapshot state = tracker.snapshot();
  EXPECT_TRUE(state.active);
  EXPECT_FALSE(state.triggered);
  EXPECT_EQ(state.event_id, "evt_5000_12");
  EXPECT_EQ(output->active_event_id, "evt_5000_12");

  // Continuous targets refresh the active window and best target only.
  tracker.feed(make_detection_frame(13, 6000, 0.97f));
  state = tracker.snapshot();
  EXPECT_TRUE(state.active);
  EXPECT_FALSE(state.triggered);
  EXPECT_NEAR(state.best_target.score, 0.97, 1e-6);
  EXPECT_EQ(state.last_active_ms, 6000);
  EXPECT_EQ(state.last_trigger_ms, 1000);
  EXPECT_EQ(tasks.size(), 1u);
}

TEST(Transport, EventTrackerGraceReleaseAllowsNextEvent) {
  std::vector<GenAiTask> tasks;
  EventOptions options;
  options.end_grace_sec = 2;
  options.cooldown_sec = 2;
  options.submit = [&tasks](GenAiTask task) {
    tasks.push_back(std::move(task));
    return true;
  };
  FeedableEventTracker tracker(options);

  tracker.feed(make_detection_frame(20, 1000, 0.80f));
  ASSERT_EQ(tasks.size(), 1u);

  // Still inside the grace window.
  tracker.feed(make_silent_frame(21, 2500));
  EXPECT_TRUE(tracker.snapshot().active);
  EXPECT_EQ(tracker.snapshot().event_id, "evt_1000_20");

  // Grace expires: active state and its event id are released.
  FrameMetaPtr output = tracker.feed(make_silent_frame(22, 3000));
  const EventTrackerSnapshot released = tracker.snapshot();
  EXPECT_FALSE(released.active);
  EXPECT_FALSE(released.triggered);
  EXPECT_TRUE(released.event_id.empty());
  EXPECT_TRUE(output->active_event_id.empty());

  // Cooldown also elapsed, so the next event triggers normally.
  output = tracker.feed(make_detection_frame(23, 3200, 0.70f));
  ASSERT_EQ(tasks.size(), 2u);
  EXPECT_EQ(tasks[1].event_id, "evt_3200_23");
  EXPECT_NE(tasks[0].event_id, tasks[1].event_id);
  EXPECT_TRUE(tracker.snapshot().active);
  EXPECT_EQ(output->active_event_id, "evt_3200_23");
}

}  // namespace
