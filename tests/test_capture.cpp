#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "api/api_server.hpp"
#include "capture/capture_node.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "core/build_info.hpp"
#include "core/node_state.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace {

using edge::ApiServer;
using edge::CaptureNode;
using edge::Metrics;
using edge::NodeState;

struct FormatDeleter {
  void operator()(AVFormatContext* context) const {
    if (context != nullptr) {
      if (context->pb != nullptr) {
        avio_closep(&context->pb);
      }
      avformat_free_context(context);
    }
  }
};

struct CodecDeleter {
  void operator()(AVCodecContext* context) const {
    if (context != nullptr) {
      avcodec_free_context(&context);
    }
  }
};

struct FrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
  }
};

struct PacketDeleter {
  void operator()(AVPacket* packet) const {
    if (packet != nullptr) {
      av_packet_free(&packet);
    }
  }
};

// Encodes a few tiny MPEG4 frames so capture tests never need repository media
// or a live source.
bool write_test_video(const std::string& path) {
  AVFormatContext* raw_format = nullptr;
  if (avformat_alloc_output_context2(&raw_format, nullptr, "avi", path.c_str()) < 0) {
    return false;
  }
  std::unique_ptr<AVFormatContext, FormatDeleter> format(raw_format);

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
  if (codec == nullptr) {
    return false;
  }
  AVStream* stream = avformat_new_stream(format.get(), nullptr);
  if (stream == nullptr) {
    return false;
  }
  std::unique_ptr<AVCodecContext, CodecDeleter> context(avcodec_alloc_context3(codec));
  if (!context) {
    return false;
  }
  context->width = 64;
  context->height = 48;
  context->time_base = AVRational{1, 10};
  context->framerate = AVRational{10, 1};
  context->pix_fmt = AV_PIX_FMT_YUV420P;
  context->max_b_frames = 0;
  context->gop_size = 10;
  context->bit_rate = 80000;
  if (avcodec_open2(context.get(), codec, nullptr) < 0 ||
      avcodec_parameters_from_context(stream->codecpar, context.get()) < 0) {
    return false;
  }
  stream->time_base = context->time_base;

  if (avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE) < 0 ||
      avformat_write_header(format.get(), nullptr) < 0) {
    return false;
  }

  std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
  std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
  if (!frame || !packet) {
    return false;
  }
  frame->format = context->pix_fmt;
  frame->width = context->width;
  frame->height = context->height;
  if (av_frame_get_buffer(frame.get(), 32) < 0) {
    return false;
  }

  const int frame_count = 5;
  for (int index = 0; index < frame_count; ++index) {
    if (av_frame_make_writable(frame.get()) < 0) {
      return false;
    }
    for (int row = 0; row < context->height; ++row) {
      std::memset(frame->data[0] + row * frame->linesize[0], 40 + index * 20,
                  static_cast<std::size_t>(context->width));
    }
    for (int row = 0; row < context->height / 2; ++row) {
      std::memset(frame->data[1] + row * frame->linesize[1], 128,
                  static_cast<std::size_t>(context->width / 2));
      std::memset(frame->data[2] + row * frame->linesize[2], 128,
                  static_cast<std::size_t>(context->width / 2));
    }
    frame->pts = index;
    if (avcodec_send_frame(context.get(), frame.get()) < 0) {
      return false;
    }
    while (avcodec_receive_packet(context.get(), packet.get()) == 0) {
      av_packet_rescale_ts(packet.get(), context->time_base, stream->time_base);
      packet->stream_index = stream->index;
      av_interleaved_write_frame(format.get(), packet.get());
      av_packet_unref(packet.get());
    }
  }

  avcodec_send_frame(context.get(), nullptr);
  while (avcodec_receive_packet(context.get(), packet.get()) == 0) {
    av_packet_rescale_ts(packet.get(), context->time_base, stream->time_base);
    packet->stream_index = stream->index;
    av_interleaved_write_frame(format.get(), packet.get());
    av_packet_unref(packet.get());
  }
  return av_write_trailer(format.get()) >= 0;
}

std::string unique_temp_path() {
  static std::atomic<std::uintptr_t> counter{0};
  std::ostringstream name;
  name << "edge_vision_test_" << std::hex
       << reinterpret_cast<std::uintptr_t>(&counter) << "_" << counter.fetch_add(1) << ".avi";
  return (std::filesystem::temp_directory_path() / name.str()).string();
}

class TempVideo {
 public:
  TempVideo() : path_(unique_temp_path()), valid_(write_test_video(path_)) {}
  ~TempVideo() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TempVideo(const TempVideo&) = delete;
  TempVideo& operator=(const TempVideo&) = delete;

  const std::string& path() const { return path_; }
  bool valid() const { return valid_; }

 private:
  std::string path_;
  bool valid_;
};

CaptureNode::Options capture_options(const std::string& source, bool loop) {
  CaptureNode::Options options;
  options.source = source;
  options.loop = loop;
  options.width = 64;
  options.height = 48;
  options.target_fps = 30;
  options.dump_interval_sec = 0;
  return options;
}

edge::Config api_config(const std::string& source) {
  edge::Config config;
  config.input.source = source;
  config.input.loop = true;
  config.streamer.http_port = 0;
  config.llm.endpoint = "http://127.0.0.1:8080/v1/chat/completions";
  config.storage.endpoint = "http://127.0.0.1:8090/api/v1/events";
  return config;
}

bool wait_for(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

TEST(Capture, DecodesLocalVideoAndLoopsPastEof) {
  TempVideo video;
  ASSERT_TRUE(video.valid());
  auto metrics = std::make_shared<Metrics>();
  CaptureNode node(capture_options(video.path(), true), metrics);

  node.start();
  ASSERT_TRUE(wait_for([&node] { return node.frames_captured() >= 7; }));
  node.stop();

  EXPECT_GE(node.frames_captured(), 7u);
  EXPECT_EQ(metrics->capture_frames(), node.frames_captured());
  EXPECT_EQ(node.state(), NodeState::kStopped);
}

TEST(Capture, RejectsInvalidSourceAndKeepsOriginal) {
  TempVideo first;
  TempVideo second;
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());
  CaptureNode node(capture_options(first.path(), true), std::make_shared<Metrics>());

  std::string error;
  EXPECT_FALSE(node.switch_source("", false, error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(node.switch_source(unique_temp_path(), false, error));
  EXPECT_EQ(node.source(), first.path());
  EXPECT_TRUE(node.loop());

  EXPECT_TRUE(node.switch_source(second.path(), false, error)) << error;
  EXPECT_EQ(node.source(), second.path());
  EXPECT_FALSE(node.loop());
}

TEST(Api, HealthReportsRunningCapture) {
  TempVideo video;
  ASSERT_TRUE(video.valid());
  auto metrics = std::make_shared<Metrics>();
  auto capture = std::make_shared<CaptureNode>(capture_options(video.path(), true), metrics);
  ApiServer api(api_config(video.path()), capture, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  ASSERT_GT(api.port(), 0);
  capture->start();
  ASSERT_TRUE(wait_for([&capture] { return capture->state() == NodeState::kRunning; }));

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

TEST(Api, SwitchFailureKeepsOriginalAndSuccessUpdatesConfig) {
  TempVideo first;
  TempVideo second;
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());
  auto metrics = std::make_shared<Metrics>();
  auto capture = std::make_shared<CaptureNode>(capture_options(first.path(), true), metrics);
  ApiServer api(api_config(first.path()), capture, metrics, "127.0.0.1");

  std::string error;
  ASSERT_TRUE(api.start(error)) << error;
  httplib::Client client("127.0.0.1", api.port());

  const auto malformed = client.Post("/api/v1/source/switch", R"({"source":"x"})",
                                     "application/json");
  ASSERT_TRUE(malformed);
  EXPECT_EQ(malformed->status, 400);
  EXPECT_EQ(nlohmann::json::parse(malformed->body)["error"]["code"].get<std::string>(),
            "INVALID_PARAMETER");

  const std::string missing = unique_temp_path();
  const auto failed = client.Post("/api/v1/source/switch",
                                  nlohmann::json{{"source", missing}, {"loop", true}}.dump(),
                                  "application/json");
  ASSERT_TRUE(failed);
  EXPECT_EQ(failed->status, 400);
  EXPECT_EQ(nlohmann::json::parse(failed->body)["error"]["code"].get<std::string>(),
            "INVALID_SOURCE");

  const auto unchanged = client.Get("/api/v1/config");
  ASSERT_TRUE(unchanged);
  const nlohmann::json before = nlohmann::json::parse(unchanged->body);
  EXPECT_EQ(before["input"]["source"], first.path());
  EXPECT_TRUE(before["input"]["loop"].get<bool>());

  const auto switched = client.Post(
      "/api/v1/source/switch",
      nlohmann::json{{"source", second.path()}, {"loop", false}}.dump(), "application/json");
  ASSERT_TRUE(switched);
  EXPECT_EQ(switched->status, 200);

  const auto updated = client.Get("/api/v1/config");
  ASSERT_TRUE(updated);
  const nlohmann::json after = nlohmann::json::parse(updated->body);
  EXPECT_EQ(after["input"]["source"], second.path());
  EXPECT_FALSE(after["input"]["loop"].get<bool>());
}

}  // namespace
