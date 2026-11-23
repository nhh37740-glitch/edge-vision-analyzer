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
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "capture/capture_node.hpp"
#include "capture/remux_recorder.hpp"
#include "common/metrics.hpp"
#include "core/node_state.hpp"

namespace {

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

struct InputFormatDeleter {
  void operator()(AVFormatContext* context) const {
    if (context != nullptr) {
      AVFormatContext* owned = context;
      avformat_close_input(&owned);
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

// Encodes a few tiny MPEG4 frames (and optionally a PCM audio track) so capture
// tests never need repository media or a live source.
bool write_test_video(const std::string& path, bool with_audio = false) {
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

  std::unique_ptr<AVCodecContext, CodecDeleter> audio_context;
  AVStream* audio_stream = nullptr;
  if (with_audio) {
    const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (audio_codec == nullptr) {
      return false;
    }
    audio_stream = avformat_new_stream(format.get(), nullptr);
    audio_context.reset(avcodec_alloc_context3(audio_codec));
    if (audio_stream == nullptr || !audio_context) {
      return false;
    }
    audio_context->sample_fmt = AV_SAMPLE_FMT_S16;
    audio_context->sample_rate = 8000;
    audio_context->ch_layout = AV_CHANNEL_LAYOUT_MONO;
    audio_context->time_base = AVRational{1, audio_context->sample_rate};
    if (avcodec_open2(audio_context.get(), audio_codec, nullptr) < 0 ||
        avcodec_parameters_from_context(audio_stream->codecpar, audio_context.get()) < 0) {
      return false;
    }
    audio_stream->time_base = audio_context->time_base;
  }

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

  std::unique_ptr<AVFrame, FrameDeleter> audio_frame;
  if (with_audio) {
    audio_frame.reset(av_frame_alloc());
    if (!audio_frame) {
      return false;
    }
    audio_frame->format = audio_context->sample_fmt;
    audio_frame->sample_rate = audio_context->sample_rate;
    if (av_channel_layout_copy(&audio_frame->ch_layout, &audio_context->ch_layout) < 0) {
      return false;
    }
    audio_frame->nb_samples = audio_context->sample_rate / 10;
    if (av_frame_get_buffer(audio_frame.get(), 0) < 0) {
      return false;
    }
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
    if (with_audio) {
      if (av_frame_make_writable(audio_frame.get()) < 0) {
        return false;
      }
      std::memset(audio_frame->data[0], index * 10,
                  static_cast<std::size_t>(audio_frame->nb_samples) * 2);
      audio_frame->pts = static_cast<std::int64_t>(index) * audio_frame->nb_samples;
      if (avcodec_send_frame(audio_context.get(), audio_frame.get()) < 0) {
        return false;
      }
      while (avcodec_receive_packet(audio_context.get(), packet.get()) == 0) {
        av_packet_rescale_ts(packet.get(), audio_context->time_base, audio_stream->time_base);
        packet->stream_index = audio_stream->index;
        av_interleaved_write_frame(format.get(), packet.get());
        av_packet_unref(packet.get());
      }
    }
  }

  avcodec_send_frame(context.get(), nullptr);
  while (avcodec_receive_packet(context.get(), packet.get()) == 0) {
    av_packet_rescale_ts(packet.get(), context->time_base, stream->time_base);
    packet->stream_index = stream->index;
    av_interleaved_write_frame(format.get(), packet.get());
    av_packet_unref(packet.get());
  }
  if (with_audio) {
    avcodec_send_frame(audio_context.get(), nullptr);
    while (avcodec_receive_packet(audio_context.get(), packet.get()) == 0) {
      av_packet_rescale_ts(packet.get(), audio_context->time_base, audio_stream->time_base);
      packet->stream_index = audio_stream->index;
      av_interleaved_write_frame(format.get(), packet.get());
      av_packet_unref(packet.get());
    }
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
  explicit TempVideo(bool with_audio = false)
      : path_(unique_temp_path()), valid_(write_test_video(path_, with_audio)) {}
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

TEST(Capture, RemuxWritesOnlyVideoTrack) {
  TempVideo video(/*with_audio=*/true);
  ASSERT_TRUE(video.valid());

  AVFormatContext* raw_input = nullptr;
  ASSERT_GE(avformat_open_input(&raw_input, video.path().c_str(), nullptr, nullptr), 0);
  std::unique_ptr<AVFormatContext, InputFormatDeleter> input(raw_input);
  ASSERT_GE(avformat_find_stream_info(input.get(), nullptr), 0);
  ASSERT_GE(input->nb_streams, 2u);

  const std::string output_path = unique_temp_path();
  edge::RemuxRecorder recorder;
  std::string error;
  ASSERT_TRUE(recorder.open(output_path, input.get(), 0, error)) << error;

  std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
  ASSERT_TRUE(packet != nullptr);
  while (av_read_frame(input.get(), packet.get()) >= 0) {
    recorder.write_packet(packet.get());
    av_packet_unref(packet.get());
  }
  recorder.close();

  AVFormatContext* raw_output = nullptr;
  ASSERT_GE(avformat_open_input(&raw_output, output_path.c_str(), nullptr, nullptr), 0);
  std::unique_ptr<AVFormatContext, InputFormatDeleter> output(raw_output);
  ASSERT_GE(avformat_find_stream_info(output.get(), nullptr), 0);
  EXPECT_EQ(output->nb_streams, 1u);
  EXPECT_EQ(output->streams[0]->codecpar->codec_type, AVMEDIA_TYPE_VIDEO);

  std::error_code ignored;
  std::filesystem::remove(output_path, ignored);
}

}  // namespace
