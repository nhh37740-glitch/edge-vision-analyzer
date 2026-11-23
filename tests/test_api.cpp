#include <gtest/gtest.h>

#include <memory>
#include <string>

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

Config api_config() {
  Config config;
  config.input.source = "original";
  config.input.loop = true;
  config.streamer.http_port = 0;
  config.llm.endpoint = "http://127.0.0.1:8080/v1/chat/completions";
  config.storage.endpoint = "http://127.0.0.1:8090/api/v1/events";
  return config;
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

}  // namespace
