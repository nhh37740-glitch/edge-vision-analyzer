#include "capture/capture_node.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include "capture/frame_dumper.hpp"
#include "capture/remux_recorder.hpp"
#include "core/source_kind.hpp"

namespace edge {
namespace {

struct FormatDeleter {
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

struct SwsDeleter {
  void operator()(SwsContext* context) const {
    if (context != nullptr) {
      sws_freeContext(context);
    }
  }
};

std::string ffmpeg_error(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

}  // namespace

class CaptureNode::Source {
 public:
  using FormatPtr = std::unique_ptr<AVFormatContext, FormatDeleter>;
  using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
  using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
  using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
  using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;

  FormatPtr format;
  CodecPtr codec;
  FramePtr frame;
  PacketPtr packet;
  SwsPtr sws;
  int stream_index = -1;
  AVRational time_base{1, 1000};
  bool loop = false;
  std::string location;
};

CaptureNode::CaptureNode(Options options, std::shared_ptr<Metrics> metrics)
    : BaseNode("CaptureNode"),
      options_(std::move(options)),
      metrics_(std::move(metrics)),
      source_location_(options_.source),
      loop_(options_.loop) {
  if (options_.dump_interval_sec > 0) {
    dumper_ = std::make_unique<FrameDumper>(options_.dump_dir, options_.dump_interval_sec);
  }
}

CaptureNode::~CaptureNode() { stop(); }

std::shared_ptr<CaptureNode::Source> CaptureNode::open_source(const std::string& location,
                                                              bool loop, std::string& error) {
  auto source = std::make_shared<Source>();
  source->location = location;
  source->loop = loop;

  AVFormatContext* raw_format = nullptr;
  AVDictionary* options = nullptr;
  av_dict_set(&options, "rtsp_transport", "tcp", 0);
  av_dict_set(&options, "rw_timeout", "3000000", 0);
  const int open_result = avformat_open_input(&raw_format, location.c_str(), nullptr, &options);
  av_dict_free(&options);
  if (open_result < 0) {
    error = "cannot open " + location + ": " + ffmpeg_error(open_result);
    return nullptr;
  }
  source->format.reset(raw_format);

  int result = avformat_find_stream_info(source->format.get(), nullptr);
  if (result < 0) {
    error = "cannot read stream info: " + ffmpeg_error(result);
    return nullptr;
  }

  const int stream_index =
      av_find_best_stream(source->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    error = "no video stream in " + location;
    return nullptr;
  }
  AVStream* stream = source->format->streams[stream_index];
  const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (decoder == nullptr) {
    error = "no decoder for the video stream";
    return nullptr;
  }

  AVCodecContext* codec = avcodec_alloc_context3(decoder);
  if (codec == nullptr) {
    error = "cannot allocate the decoder context";
    return nullptr;
  }
  result = avcodec_parameters_to_context(codec, stream->codecpar);
  if (result < 0) {
    avcodec_free_context(&codec);
    error = "cannot configure the decoder: " + ffmpeg_error(result);
    return nullptr;
  }
  codec->thread_count = 1;
  result = avcodec_open2(codec, decoder, nullptr);
  if (result < 0) {
    avcodec_free_context(&codec);
    error = "cannot open the decoder: " + ffmpeg_error(result);
    return nullptr;
  }
  source->codec.reset(codec);

  if (source->codec->width <= 0 || source->codec->height <= 0) {
    error = "the video stream reports an invalid size";
    return nullptr;
  }

  AVPixelFormat source_format = source->codec->pix_fmt;
  if (source_format == AV_PIX_FMT_NONE) {
    source_format = static_cast<AVPixelFormat>(stream->codecpar->format);
  }
  source->sws.reset(sws_getContext(source->codec->width, source->codec->height, source_format,
                                   options_.width, options_.height, AV_PIX_FMT_RGB24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr));
  if (!source->sws) {
    error = "cannot create the RGB24 scaler";
    return nullptr;
  }

  source->frame.reset(av_frame_alloc());
  source->packet.reset(av_packet_alloc());
  if (!source->frame || !source->packet) {
    error = "cannot allocate decoder buffers";
    return nullptr;
  }

  source->stream_index = stream_index;
  source->time_base = stream->time_base;
  return source;
}

std::shared_ptr<CaptureNode::Source> CaptureNode::current_source() const {
  std::lock_guard<std::mutex> lock(source_mutex_);
  return source_;
}

std::shared_ptr<CaptureNode::Source> CaptureNode::install_reconnected_source(
    const std::shared_ptr<Source>& candidate, const std::string& location, bool loop) {
  std::lock_guard<std::mutex> lock(source_mutex_);
  if (!source_) {
    source_ = candidate;
    source_location_ = location;
    loop_ = loop;
  }
  return source_;
}

void CaptureNode::mark_source_failed(const std::shared_ptr<Source>& source) {
  std::lock_guard<std::mutex> lock(source_mutex_);
  if (source_ == source) {
    source_.reset();
    source_location_ = source->location;
    loop_ = source->loop;
  }
}

bool CaptureNode::switch_source(const std::string& location, bool loop, std::string& error) {
  if (location.empty()) {
    error = "source must not be empty";
    return false;
  }
  if (recording_active_.load()) {
    error = "cannot switch source while recording";
    return false;
  }

  auto opened = open_source(location, loop, error);
  if (!opened) {
    return false;
  }

  std::lock_guard<std::mutex> lock(source_mutex_);
  if (recording_active_.load()) {
    error = "cannot switch source while recording";
    return false;
  }
  source_ = std::move(opened);
  source_location_ = location;
  loop_ = loop;
  return true;
}

std::string CaptureNode::source() const {
  std::lock_guard<std::mutex> lock(source_mutex_);
  return source_location_;
}

bool CaptureNode::loop() const {
  std::lock_guard<std::mutex> lock(source_mutex_);
  return loop_;
}

void CaptureNode::handle_packet(const std::shared_ptr<Source>& source, const AVPacket* packet) {
  int result = avcodec_send_packet(source->codec.get(), packet);
  if (result < 0) {
    return;
  }

  while (true) {
    result = avcodec_receive_frame(source->codec.get(), source->frame.get());
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      break;
    }
    if (result < 0) {
      std::fprintf(stderr, "CaptureNode: decode error: %s\n", ffmpeg_error(result).c_str());
      break;
    }

    auto meta = std::make_shared<FrameMeta>();
    meta->frame_id = frames_captured_.fetch_add(1) + 1;
    meta->width = options_.width;
    meta->height = options_.height;
    const std::size_t row_bytes = static_cast<std::size_t>(options_.width) * 3;
    meta->rgb_data.resize(row_bytes * static_cast<std::size_t>(options_.height));

    std::uint8_t* destination[4] = {meta->rgb_data.data(), nullptr, nullptr, nullptr};
    int destination_linesize[4] = {static_cast<int>(row_bytes), 0, 0, 0};
    sws_scale(source->sws.get(), source->frame->data, source->frame->linesize, 0,
              source->codec->height, destination, destination_linesize);

    const std::int64_t pts = source->frame->best_effort_timestamp != AV_NOPTS_VALUE
                                 ? source->frame->best_effort_timestamp
                                 : source->frame->pts;
    if (pts != AV_NOPTS_VALUE) {
      meta->timestamp_ms = av_rescale_q(pts, source->time_base, AVRational{1, 1000});
    } else {
      meta->timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    }

    metrics_->on_capture_frame();
    if (dumper_) {
      dumper_->maybe_dump(*meta);
    }
    emit(meta);
    av_frame_unref(source->frame.get());
  }
}

bool CaptureNode::sleep_interruptible(std::chrono::milliseconds duration) const {
  const auto end = std::chrono::steady_clock::now() + duration;
  while (!stop_requested()) {
    const auto remaining = end - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      break;
    }
    const auto step = std::min<std::chrono::steady_clock::duration>(
        remaining, std::chrono::milliseconds(100));
    std::this_thread::sleep_for(step);
  }
  return !stop_requested();
}

void CaptureNode::run() {
  set_state(NodeState::kRunning);
  auto next_frame_time = std::chrono::steady_clock::now();

  while (!stop_requested()) {
    auto source = current_source();
    if (!source) {
      std::string location;
      bool loop = false;
      {
        std::lock_guard<std::mutex> lock(source_mutex_);
        location = source_location_.empty() ? options_.source : source_location_;
        loop = source_location_.empty() ? options_.loop : loop_;
      }

      std::string error;
      auto opened = open_source(location, loop, error);
      if (!opened) {
        set_state(NodeState::kFailed);
        std::fprintf(stderr, "CaptureNode: %s\n", error.c_str());
        if (!sleep_interruptible(std::chrono::seconds(2))) {
          break;
        }
        continue;
      }
      source = install_reconnected_source(opened, location, loop);
      set_state(NodeState::kRunning);
    }

    if (!record_attempted_ && !options_.record_path.empty()) {
      std::lock_guard<std::mutex> lock(source_mutex_);
      if (source_ == source) {
        record_attempted_ = true;
        recorder_ = std::make_unique<RemuxRecorder>();
        std::string error;
        if (!recorder_->open(options_.record_path, source->format.get(),
                             options_.record_duration_sec, error)) {
          std::fprintf(stderr, "CaptureNode: recording disabled: %s\n", error.c_str());
          recorder_.reset();
        } else {
          recording_active_.store(true);
        }
      }
    }

    AVPacket* packet = source->packet.get();
    const int read_result = av_read_frame(source->format.get(), packet);
    if (read_result < 0) {
      av_packet_unref(packet);
      const bool network = is_network_source(source->location);

      if (read_result == AVERROR_EOF && source->loop) {
        const int result = av_seek_frame(source->format.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
          std::fprintf(stderr, "CaptureNode: cannot rewind source: %s\n",
                       ffmpeg_error(result).c_str());
          set_state(NodeState::kFailed);
          break;
        }
        avcodec_flush_buffers(source->codec.get());
        continue;
      }

      if (read_result == AVERROR_EOF && !network) {
        set_state(NodeState::kStopped);
        break;
      }

      std::fprintf(stderr, "CaptureNode: read failed: %s\n", ffmpeg_error(read_result).c_str());
      if (recorder_) {
        recorder_->close();
        recording_active_.store(false);
        recording_finished_.store(true);
      }
      mark_source_failed(source);
      if (!sleep_interruptible(std::chrono::seconds(2))) {
        break;
      }
      continue;
    }

    if (packet->stream_index == source->stream_index) {
      if (recorder_) {
        recorder_->write_packet(packet);
        if (recorder_->finished()) {
          recording_active_.store(false);
          recording_finished_.store(true);
        }
      }
      handle_packet(source, packet);
    }
    av_packet_unref(packet);

    const auto interval =
        std::chrono::microseconds(1000000 / std::max(1, options_.target_fps));
    next_frame_time += interval;
    const auto now = std::chrono::steady_clock::now();
    if (next_frame_time > now) {
      if (!sleep_interruptible(std::chrono::duration_cast<std::chrono::milliseconds>(
              next_frame_time - now))) {
        break;
      }
    } else {
      next_frame_time = now;
    }
  }

  if (recorder_) {
    recorder_->close();
    recording_active_.store(false);
    recording_finished_.store(true);
  }
  set_state(NodeState::kStopped);
}

}  // namespace edge
