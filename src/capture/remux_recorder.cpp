#include "capture/remux_recorder.hpp"

extern "C" {
#include <libavutil/mathematics.h>
}

namespace edge {

RemuxRecorder::~RemuxRecorder() { close(); }

bool RemuxRecorder::open(const std::string& path, AVFormatContext* input, int duration_sec,
                         std::string& error) {
  if (output_ != nullptr) {
    error = "recorder is already open";
    return false;
  }

  if (avformat_alloc_output_context2(&output_, nullptr, nullptr, path.c_str()) < 0 ||
      output_ == nullptr) {
    error = "cannot create output container for " + path;
    close();
    return false;
  }

  const int video_index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_index < 0) {
    error = "no video stream to remux";
    close();
    return false;
  }

  AVStream* output_stream = avformat_new_stream(output_, nullptr);
  if (output_stream == nullptr) {
    error = "cannot allocate output stream";
    close();
    return false;
  }
  if (avcodec_parameters_copy(output_stream->codecpar, input->streams[video_index]->codecpar) < 0) {
    error = "cannot copy codec parameters";
    close();
    return false;
  }
  output_stream->codecpar->codec_tag = 0;
  input_time_base_ = input->streams[video_index]->time_base;
  output_stream->time_base = input_time_base_;
  input_video_stream_ = video_index;
  output_video_stream_ = output_stream->index;

  if (avio_open(&output_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0 || output_->pb == nullptr) {
    error = "cannot open output file " + path;
    close();
    return false;
  }
  if (avformat_write_header(output_, nullptr) < 0) {
    error = "the source codec is not supported by " + path;
    close();
    return false;
  }

  header_written_ = true;
  started_ = std::chrono::steady_clock::now();
  duration_sec_ = duration_sec;
  finished_ = false;
  return true;
}

void RemuxRecorder::write_packet(const AVPacket* packet) {
  if (!header_written_ || finished_ || output_ == nullptr || packet == nullptr) {
    return;
  }
  if (packet->stream_index != input_video_stream_) {
    return;
  }

  AVPacket* copy = av_packet_clone(packet);
  if (copy == nullptr) {
    return;
  }
  AVStream* output_stream = output_->streams[output_video_stream_];
  av_packet_rescale_ts(copy, input_time_base_, output_stream->time_base);
  copy->pos = -1;
  const int result = av_interleaved_write_frame(output_, copy);
  av_packet_free(&copy);
  if (result < 0) {
    close();
    finished_ = true;
    return;
  }

  if (duration_sec_ > 0 && std::chrono::steady_clock::now() - started_ >=
                               std::chrono::seconds(duration_sec_)) {
    close();
    finished_ = true;
  }
}

void RemuxRecorder::close() {
  if (output_ == nullptr) {
    return;
  }
  if (header_written_) {
    av_write_trailer(output_);
    header_written_ = false;
  }
  if (output_->pb != nullptr) {
    avio_closep(&output_->pb);
  }
  avformat_free_context(output_);
  output_ = nullptr;
  input_video_stream_ = -1;
  output_video_stream_ = -1;
}

}  // namespace edge
