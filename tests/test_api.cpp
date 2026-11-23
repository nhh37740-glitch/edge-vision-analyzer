#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "api/api_server.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "core/build_info.hpp"
#include "core/node_state.hpp"
#include "core/source_control.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace {

using edge::ApiServer;
using edge::Config;
using edge::Metrics;
using edge::NodeState;

// The API layer only needs the SourceControl port; a fake keeps this test
// independent of FFmpeg and the capture module.
class FakeSourceControl : public edge::SourceControl {
 public:
  bool switch_source(const std::string& source, bool loop, std::string& error) override {
    if (source == "invalid") {
      error = "cannot open " + source;
      return false;
    }
    source_ = source;
    loop_ = loop;
    return true;
  }

  NodeState state() const override { return NodeState::kRunning; }
  const std::string& name() const override { return name_; }

 private:
  std::string name_ = "CaptureNode";
  std::string source_ = "original";
  bool loop_ = true;
};

// Minimal streamer stand-in: one tiny JPEG that is always available. The
// bytes only need the JPEG SOI marker; clients never decode this test image.
class FakeStreamCallbacks {
 public:
  FakeStreamCallbacks()
      : jpeg_(std::make_shared<const std::vector<std::uint8_t>>(
            std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x01})) {}

  edge::StreamCallbacks as_callbacks() {
    edge::StreamCallbacks callbacks;
    callbacks.subscribe = [this] { subscriptions.fetch_add(1); };
    callbacks.unsubscribe = [this] { subscriptions.fetch_sub(1); };
    callbacks.request_snapshot = [this] {
      snapshot_requests.fetch_add(1);
      seq.fetch_add(1);
    };
    callbacks.wait_for_jpeg = [this](std::uint64_t after_seq, std::chrono::milliseconds) {
      std::uint64_t current = seq.load();
      if (current <= after_seq) {
        seq.store(after_seq + 1);
      }
      // A live capture node produces frames; pace the fake so the stream
      // provider does not spin while the test client is still attached.
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      return jpeg_;
    };
    callbacks.latest_jpeg_seq = [this] { return seq.load(); };
    return callbacks;
  }

  std::atomic<int> subscriptions{0};
  std::atomic<int> snapshot_requests{0};

 private:
  std::shared_ptr<const std::vector<std::uint8_t>> jpeg_;
  std::atomic<std::uint64_t> seq{0};
};

Config api_config() {
  Config config;
  config.input.source = "original";
  config.input.loop = true;
  config.streamer.http_port = 0;
  config.llm.endpoint = "http://127.0.0.1:8080/v1/chat/completions";
  config.storage.endpoint = "http://127.0.0.1:8090/api/v1/events";
  return config;
}

std::string upstream_endpoint(int port, const std::string& path) {
  return "http://127.0.0.1:" + std::to_string(port) + path;
}

// Loopback stand-in for llama-server/cloud-storage health endpoints. Binding an
// ephemeral loopback port keeps the test offline and avoids the Windows
// firewall prompt a wildcard listener would raise.
class MockUpstream {
 public:
  explicit MockUpstream(int status = 200, int delay_ms = 0)
      : status_(status), delay_ms_(delay_ms) {
    server_.Get("/health", [this](const httplib::Request&, httplib::Response& response) {
      if (delay_ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
      }
      response.status = status_;
      response.set_content("{}", "application/json");
    });
  }

  ~MockUpstream() { stop(); }

  bool start() {
    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      return false;
    }
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
    return true;
  }

  void stop() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const { return port_; }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  int status_ = 200;
  int delay_ms_ = 0;
};

// The probe runs in the background; poll the cached health response until the
// first cycle has published a result.
bool wait_for_upstream_status(httplib::Client& client, const char* upstream,
                              const std::string& expected) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto response = client.Get("/api/v1/health");
    if (response && response->status == 200) {
      const nlohmann::json body = nlohmann::json::parse(response->body, nullptr, false);
      if (!body.is_discarded() && body.contains(upstream) &&
          body[upstream].value("status", "") == expected) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

TEST(Api, HealthReportsCaptureStateAndDerivedEndpoints) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(api_config(), source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  ASSERT_GT(api.port(), 0);

  httplib::Client client("127.0.0.1", api.port());
  const auto response = client.Get("/api/v1/health");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);

  const nlohmann::json body = nlohmann::json::parse(response->body);
  EXPECT_EQ(body.value("status", ""), "ok");
  EXPECT_EQ(body.value("version", ""), edge::version());
  ASSERT_EQ(body["nodes"].size(), 1u);
  EXPECT_EQ(body["nodes"][0].value("name", ""), "CaptureNode");
  EXPECT_EQ(body["nodes"][0].value("state", ""), "running");
  EXPECT_EQ(body["llm"].value("status", ""), "disabled");
  EXPECT_EQ(body["llm"].value("endpoint", ""), "http://127.0.0.1:8080/health");
  EXPECT_EQ(body["storage"].value("status", ""), "disabled");
  EXPECT_EQ(body["storage"].value("endpoint", ""), "http://127.0.0.1:8090/health");
}

TEST(Api, HealthProbeReportsReachableUpstreams) {
  MockUpstream upstream;
  ASSERT_TRUE(upstream.start()) << "cannot bind the mock upstream";

  Config config = api_config();
  config.llm.enabled = true;
  config.llm.endpoint = upstream_endpoint(upstream.port(), "/v1/chat/completions");
  config.storage.enabled = true;
  config.storage.endpoint = upstream_endpoint(upstream.port(), "/api/v1/events");

  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(config, source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());
  client.set_read_timeout(1, 0);

  EXPECT_TRUE(wait_for_upstream_status(client, "llm", "reachable"));
  EXPECT_TRUE(wait_for_upstream_status(client, "storage", "reachable"));

  const auto response = client.Get("/api/v1/health");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  const nlohmann::json body = nlohmann::json::parse(response->body);
  EXPECT_EQ(body.value("status", ""), "ok");
  EXPECT_EQ(body["llm"].value("status", ""), "reachable");
  EXPECT_EQ(body["storage"].value("status", ""), "reachable");
}

TEST(Api, HealthProbeReportsUnreachableAndClosedUpstreams) {
  MockUpstream unavailable(503);
  ASSERT_TRUE(unavailable.start()) << "cannot bind the 503 mock upstream";

  int closed_port = 0;
  {
    MockUpstream closed;
    ASSERT_TRUE(closed.start()) << "cannot bind the mock upstream";
    closed_port = closed.port();
  }  // Listener stopped, so later connections are refused.

  Config config = api_config();
  config.llm.enabled = true;
  config.llm.endpoint = upstream_endpoint(closed_port, "/v1/chat/completions");
  config.storage.enabled = true;
  config.storage.endpoint = upstream_endpoint(unavailable.port(), "/api/v1/events");

  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(config, source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());
  client.set_read_timeout(1, 0);

  EXPECT_TRUE(wait_for_upstream_status(client, "llm", "unreachable"));
  EXPECT_TRUE(wait_for_upstream_status(client, "storage", "unreachable"));

  const auto response = client.Get("/api/v1/health");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  const nlohmann::json body = nlohmann::json::parse(response->body);
  EXPECT_EQ(body.value("status", ""), "ok");
  EXPECT_EQ(body["llm"].value("status", ""), "unreachable");
  EXPECT_EQ(body["storage"].value("status", ""), "unreachable");
}

TEST(Api, SlowUpstreamProbeDoesNotBlockHealth) {
  MockUpstream slow(200, 800);
  ASSERT_TRUE(slow.start()) << "cannot bind the slow mock upstream";

  Config config = api_config();
  config.llm.enabled = true;
  config.llm.endpoint = upstream_endpoint(slow.port(), "/v1/chat/completions");

  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(config, source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  // Let the background probe enter the slow upstream request.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto begin = std::chrono::steady_clock::now();
  const auto response = client.Get("/api/v1/health");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_LT(elapsed.count(), 300) << "health waited on the slow upstream probe";

  // The 500ms probe timeout marks the upstream unreachable while the edge
  // itself stays healthy.
  EXPECT_TRUE(wait_for_upstream_status(client, "llm", "unreachable"));
  const auto after = client.Get("/api/v1/health");
  ASSERT_TRUE(after);
  EXPECT_EQ(after->status, 200);
  const nlohmann::json body = nlohmann::json::parse(after->body);
  EXPECT_EQ(body.value("status", ""), "ok");
  EXPECT_EQ(body["llm"].value("status", ""), "unreachable");
  EXPECT_EQ(body["storage"].value("status", ""), "disabled");
}

TEST(Api, SwitchRejectsInvalidRequestsAndUpdatesConfigOnSuccess) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(api_config(), source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto malformed = client.Post("/api/v1/source/switch", R"({"source":"x"})",
                                     "application/json");
  ASSERT_TRUE(malformed);
  EXPECT_EQ(malformed->status, 400);
  EXPECT_EQ(nlohmann::json::parse(malformed->body)["error"]["code"].get<std::string>(),
            "INVALID_PARAMETER");

  const auto failed = client.Post("/api/v1/source/switch",
                                  nlohmann::json{{"source", "invalid"}, {"loop", true}}.dump(),
                                  "application/json");
  ASSERT_TRUE(failed);
  EXPECT_EQ(failed->status, 400);
  EXPECT_EQ(nlohmann::json::parse(failed->body)["error"]["code"].get<std::string>(),
            "INVALID_SOURCE");

  const auto unchanged = client.Get("/api/v1/config");
  ASSERT_TRUE(unchanged);
  const nlohmann::json before = nlohmann::json::parse(unchanged->body);
  EXPECT_EQ(before["input"]["source"], "original");
  EXPECT_TRUE(before["input"]["loop"].get<bool>());

  const auto switched = client.Post(
      "/api/v1/source/switch",
      nlohmann::json{{"source", "rtsp://example/live"}, {"loop", false}}.dump(),
      "application/json");
  ASSERT_TRUE(switched);
  EXPECT_EQ(switched->status, 200);

  const auto updated = client.Get("/api/v1/config");
  ASSERT_TRUE(updated);
  const nlohmann::json after = nlohmann::json::parse(updated->body);
  EXPECT_EQ(after["input"]["source"], "rtsp://example/live");
  EXPECT_FALSE(after["input"]["loop"].get<bool>());
}

TEST(Api, ImmediateStopAfterStartDoesNotHang) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  ApiServer api(api_config(), source_control, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  EXPECT_GT(api.port(), 0);
  api.stop();
}

TEST(Api, SnapshotServesJpegAndRejectsWithoutCallbacks) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  auto streamer = std::make_shared<FakeStreamCallbacks>();

  {
    ApiServer api(api_config(), source_control, metrics, "127.0.0.1",
                  streamer->as_callbacks());
    std::string error;
    ASSERT_TRUE(api.start(error)) << error;

    httplib::Client client("127.0.0.1", api.port());
    client.set_read_timeout(2, 0);
    const auto response = client.Get("/api/v1/snapshot.jpg");
    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(response->get_header_value("Content-Type"), "image/jpeg");
    ASSERT_GE(response->body.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(response->body[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(response->body[1]), 0xD8);
    EXPECT_EQ(static_cast<unsigned char>(response->body[2]), 0xFF);
    EXPECT_EQ(streamer->snapshot_requests.load(), 1);
  }

  // Without injected callbacks both endpoints report the unified 503 body.
  ApiServer api(api_config(), source_control, metrics, "127.0.0.1");
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());
  client.set_read_timeout(2, 0);

  const auto snapshot = client.Get("/api/v1/snapshot.jpg");
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->status, 503);
  EXPECT_EQ(nlohmann::json::parse(snapshot->body)["error"]["code"].get<std::string>(),
            "FRAME_UNAVAILABLE");

  const auto stream = client.Get("/api/v1/stream");
  ASSERT_TRUE(stream);
  EXPECT_EQ(stream->status, 503);
  EXPECT_EQ(nlohmann::json::parse(stream->body)["error"]["code"].get<std::string>(),
            "FRAME_UNAVAILABLE");
}

TEST(Api, StreamServesMultipartAndUnsubscribesOnDisconnect) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  auto streamer = std::make_shared<FakeStreamCallbacks>();

  ApiServer api(api_config(), source_control, metrics, "127.0.0.1",
                streamer->as_callbacks());
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;

  int status = 0;
  std::string content_type;
  std::string received;
  bool saw_part = false;
  {
    httplib::Client client("127.0.0.1", api.port());
    client.set_read_timeout(2, 0);
    client.Get(
        "/api/v1/stream",
        [&](const httplib::Response& response) {
          status = response.status;
          content_type = response.get_header_value("Content-Type");
          return status == 200;
        },
        [&](const char* data, std::size_t length) {
          received.append(data, length);
          if (received.find("--frame") != std::string::npos &&
              received.find("Content-Type: image/jpeg") != std::string::npos) {
            saw_part = true;
            return false;  // Disconnect after the first multipart header.
          }
          return true;
        });
  }

  EXPECT_EQ(status, 200);
  EXPECT_NE(content_type.find("multipart/x-mixed-replace"), std::string::npos);
  EXPECT_NE(content_type.find("boundary=frame"), std::string::npos);
  EXPECT_TRUE(saw_part);

  for (int i = 0; i < 200 && streamer->subscriptions.load() != 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(streamer->subscriptions.load(), 0);
}

TEST(Api, PatchConfigAppliesSingleFieldAndReturnsFullConfig) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  edge::ConfigAppliers appliers;
  int conf_calls = 0;
  double applied_conf = -1.0;
  appliers.set_conf_threshold = [&](double threshold) {
    ++conf_calls;
    applied_conf = threshold;
  };

  ApiServer api(api_config(), source_control, metrics, "127.0.0.1", std::nullopt,
                appliers);
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto response = client.Patch(
      "/api/v1/config", R"({"detector":{"conf_threshold":0.75}})",
      "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);

  const nlohmann::json body = nlohmann::json::parse(response->body);
  // The response is the full effective config, exactly like GET.
  for (const char* key : {"input", "detector", "event", "llm", "storage", "streamer"}) {
    EXPECT_TRUE(body.contains(key)) << key;
  }
  EXPECT_DOUBLE_EQ(body["detector"]["conf_threshold"].get<double>(), 0.75);
  EXPECT_DOUBLE_EQ(body["detector"]["motion_mad_threshold"].get<double>(), 8.0);
  EXPECT_EQ(conf_calls, 1);
  EXPECT_DOUBLE_EQ(applied_conf, 0.75);

  const auto read_back = client.Get("/api/v1/config");
  ASSERT_TRUE(read_back);
  EXPECT_DOUBLE_EQ(
      nlohmann::json::parse(read_back->body)["detector"]["conf_threshold"].get<double>(),
      0.75);
}

TEST(Api, PatchConfigAppliesAllFiveLeaves) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  edge::ConfigAppliers appliers;
  double motion = -1.0;
  double conf = -1.0;
  bool llm = false;
  int cooldown = -1;
  bool storage = false;
  appliers.set_motion_mad_threshold = [&](double value) { motion = value; };
  appliers.set_conf_threshold = [&](double value) { conf = value; };
  appliers.set_llm_enabled = [&](bool value) { llm = value; };
  appliers.set_llm_cooldown_sec = [&](int value) { cooldown = value; };
  appliers.set_storage_enabled = [&](bool value) { storage = value; };

  ApiServer api(api_config(), source_control, metrics, "127.0.0.1", std::nullopt,
                appliers);
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto response = client.Patch(
      "/api/v1/config",
      nlohmann::json{{"detector",
                      {{"motion_mad_threshold", 12.5}, {"conf_threshold", 0.6}}},
                     {"llm", {{"enabled", true}, {"cooldown_sec", 3}}},
                     {"storage", {{"enabled", true}}}}
          .dump(),
      "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);

  const nlohmann::json body = nlohmann::json::parse(response->body);
  EXPECT_DOUBLE_EQ(body["detector"]["motion_mad_threshold"].get<double>(), 12.5);
  EXPECT_DOUBLE_EQ(body["detector"]["conf_threshold"].get<double>(), 0.6);
  EXPECT_TRUE(body["llm"]["enabled"].get<bool>());
  EXPECT_EQ(body["llm"]["cooldown_sec"].get<int>(), 3);
  EXPECT_TRUE(body["storage"]["enabled"].get<bool>());

  EXPECT_DOUBLE_EQ(motion, 12.5);
  EXPECT_DOUBLE_EQ(conf, 0.6);
  EXPECT_TRUE(llm);
  EXPECT_EQ(cooldown, 3);
  EXPECT_TRUE(storage);
}

TEST(Api, PatchConfigRejectsInvalidRequestsWithoutChangingConfig) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  edge::ConfigAppliers appliers;
  int applied = 0;
  appliers.set_motion_mad_threshold = [&](double) { ++applied; };
  appliers.set_conf_threshold = [&](double) { ++applied; };
  appliers.set_llm_enabled = [&](bool) { ++applied; };
  appliers.set_llm_cooldown_sec = [&](int) { ++applied; };
  appliers.set_storage_enabled = [&](bool) { ++applied; };

  Config config = api_config();
  ApiServer api(config, source_control, metrics, "127.0.0.1", std::nullopt,
                appliers);
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto before_response = client.Get("/api/v1/config");
  ASSERT_TRUE(before_response);
  const nlohmann::json before = nlohmann::json::parse(before_response->body);

  const std::vector<std::string> invalid_bodies = {
      "{}",
      R"({"detector":{}})",
      R"({"detector":{"unknown":1}})",
      R"({"unknown":{"enabled":true}})",
      R"({"llm":{"enabled":"yes"}})",
      R"({"llm":{"cooldown_sec":1.5}})",
      R"({"storage":{"enabled":1}})",
      R"({"detector":{"conf_threshold":1.5}})",
      R"({"detector":{"motion_mad_threshold":256}})",
      R"({"llm":{"cooldown_sec":-1}})",
      R"("not an object")",
  };
  for (const std::string& request_body : invalid_bodies) {
    const auto response =
        client.Patch("/api/v1/config", request_body, "application/json");
    ASSERT_TRUE(response) << request_body;
    EXPECT_EQ(response->status, 400) << request_body;
    const nlohmann::json body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body["error"]["code"].get<std::string>(), "INVALID_PARAMETER")
        << request_body;
  }
  EXPECT_EQ(applied, 0);

  const auto after_response = client.Get("/api/v1/config");
  ASSERT_TRUE(after_response);
  EXPECT_EQ(nlohmann::json::parse(after_response->body), before);
}

TEST(Api, PatchConfigRollsBackWhenAnyFieldIsInvalid) {
  auto source_control = std::make_shared<FakeSourceControl>();
  auto metrics = std::make_shared<Metrics>();
  edge::ConfigAppliers appliers;
  int applied = 0;
  appliers.set_motion_mad_threshold = [&](double) { ++applied; };
  appliers.set_conf_threshold = [&](double) { ++applied; };
  appliers.set_llm_cooldown_sec = [&](int) { ++applied; };

  ApiServer api(api_config(), source_control, metrics, "127.0.0.1", std::nullopt,
                appliers);
  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto before_response = client.Get("/api/v1/config");
  ASSERT_TRUE(before_response);
  const nlohmann::json before = nlohmann::json::parse(before_response->body);

  // The detector fields are valid on their own, but the negative cooldown
  // rejects the whole request and nothing is applied.
  const auto response = client.Patch(
      "/api/v1/config",
      R"({"detector":{"motion_mad_threshold":20.0,"conf_threshold":0.9},)"
      R"("llm":{"cooldown_sec":-2}})",
      "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(nlohmann::json::parse(response->body)["error"]["code"].get<std::string>(),
            "INVALID_PARAMETER");

  const auto after_response = client.Get("/api/v1/config");
  ASSERT_TRUE(after_response);
  EXPECT_EQ(nlohmann::json::parse(after_response->body), before);
  EXPECT_EQ(applied, 0);
}

}  // namespace
