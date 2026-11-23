#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "api/api_server.hpp"
#include "capture/capture_node.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "core/base_node.hpp"
#include "core/build_info.hpp"
#include "detector/motion_node.hpp"
#include "detector/ncnn_detector_node.hpp"
#include "streamer/rtsp_pusher.hpp"
#include "streamer/stream_out_node.hpp"
#include "transport/event_tracker_node.hpp"
#include "transport/genai_worker.hpp"

namespace {

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) { g_stop_requested.store(true); }

struct Options {
  std::string config_path = "config/config.local.json";
  std::string record_clip;
  int duration_sec = 0;
  bool help = false;
};

bool parse_arguments(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--config" && index + 1 < argc) {
      options.config_path = argv[++index];
    } else if (argument == "--record-clip" && index + 1 < argc) {
      options.record_clip = argv[++index];
    } else if (argument == "--duration" && index + 1 < argc) {
      options.duration_sec = std::atoi(argv[++index]);
      if (options.duration_sec < 0) {
        std::fprintf(stderr, "--duration must not be negative\n");
        return false;
      }
    } else if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argument.c_str());
      return false;
    }
  }
  if (!options.record_clip.empty() && options.duration_sec == 0) {
    std::fprintf(stderr, "--record-clip requires --duration <seconds>\n");
    return false;
  }
  return true;
}

void print_usage() {
  std::printf(
      "edge-vision-analyzer %s\n"
      "usage: edge-vision-analyzer --config <path> [--record-clip <path> --duration <sec>]\n",
      edge::version());
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_arguments(argc, argv, options)) {
    print_usage();
    return 1;
  }
  if (options.help) {
    print_usage();
    return 0;
  }

  edge::Config config;
  try {
    config = edge::Config::load(options.config_path);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
#ifdef SIGBREAK
  // Windows console break; harmless elsewhere and keeps dev runs stoppable.
  std::signal(SIGBREAK, handle_signal);
#endif

  edge::CaptureNode::Options capture_options;
  capture_options.source = config.input.source;
  capture_options.loop = config.input.loop;
  capture_options.width = config.input.width;
  capture_options.height = config.input.height;
  capture_options.target_fps = config.input.target_fps;
  capture_options.dump_interval_sec = config.input.dump_interval_sec;
  capture_options.record_path = options.record_clip;
  capture_options.record_duration_sec = options.duration_sec;

  auto metrics = std::make_shared<edge::Metrics>();
  auto capture = std::make_shared<edge::CaptureNode>(capture_options, metrics);

  auto motion = std::make_shared<edge::MotionNode>(config.detector.motion_mad_threshold);

  edge::NcnnDetectorNode::Options detector_options;
  detector_options.model_param = config.detector.model_param;
  detector_options.model_bin = config.detector.model_bin;
  detector_options.target_labels = config.detector.target_labels;
  detector_options.conf_threshold = static_cast<float>(config.detector.conf_threshold);
  detector_options.input_size = config.detector.input_size;
  detector_options.num_threads = config.detector.num_threads;
  detector_options.quantization = config.detector.quantization;
  detector_options.model_candidates = config.detector.model_candidates;
  auto detector = std::make_shared<edge::NcnnDetectorNode>(detector_options);
  if (!detector->load()) {
    // The pipeline stays usable without weights: frames pass through unchanged.
    std::fprintf(stderr,
                 "NcnnDetectorNode: model unavailable; frames will pass through\n");
  }

  std::shared_ptr<edge::GenAiWorker> worker;
  if (config.llm.enabled || config.storage.enabled) {
    worker = std::make_shared<edge::GenAiWorker>(config.llm, config.storage);
  }

  edge::EventOptions event_options;
  event_options.end_grace_sec = config.event.end_grace_sec;
  event_options.cooldown_sec = config.llm.cooldown_sec;
  if (worker) {
    event_options.submit = [worker](edge::GenAiTask task) {
      return worker->submit(std::move(task));
    };
  }
  auto tracker = std::make_shared<edge::EventTrackerNode>(std::move(event_options));

  // RTSP is opt-in. In mjpeg mode no encoder process is created at all; the
  // MJPEG endpoint keeps encoding on demand. Geometry and rate follow the
  // decoded frame stream.
  std::shared_ptr<edge::RtspPusher> rtsp_pusher;
  if (config.streamer.mode == "rtsp") {
    rtsp_pusher = std::make_shared<edge::RtspPusher>(
        config.streamer.mode, config.streamer.rtsp_url, config.input.width,
        config.input.height, config.input.target_fps);
  }

  auto stream_out = std::make_shared<edge::StreamOutNode>(2, rtsp_pusher);

  capture->attach_next(motion);
  motion->attach_next(detector);
  detector->attach_next(tracker);
  tracker->attach_next(stream_out);

  edge::StreamCallbacks stream_callbacks;
  stream_callbacks.subscribe = [stream_out] { stream_out->subscribe(); };
  stream_callbacks.unsubscribe = [stream_out] { stream_out->unsubscribe(); };
  stream_callbacks.request_snapshot = [stream_out] { stream_out->request_snapshot(); };
  stream_callbacks.wait_for_jpeg =
      [stream_out](std::uint64_t after_seq, std::chrono::milliseconds timeout) {
        return stream_out->wait_for_jpeg(after_seq, timeout);
      };
  stream_callbacks.latest_jpeg_seq = [stream_out] { return stream_out->latest_jpeg_seq(); };

  auto api = std::make_shared<edge::ApiServer>(
      config, capture, metrics, "0.0.0.0",
      std::optional<edge::StreamCallbacks>(std::move(stream_callbacks)));

  // Upstream nodes start last so no frame reaches a node whose worker is not
  // running yet.
  if (worker) {
    worker->start();
  }
  if (rtsp_pusher && !rtsp_pusher->start()) {
    // A missing ffmpeg or unreachable RTSP server must not take down the
    // pipeline: log the cause and keep the MJPEG endpoint available.
    std::fprintf(stderr, "RtspPusher: %s; MJPEG output remains available\n",
                 rtsp_pusher->last_error().c_str());
  }
  stream_out->start();
  tracker->start();
  detector->start();
  motion->start();
  capture->start();

  std::string error;
  if (!api->start(error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    // Roll back the nodes already started, upstream first.
    capture->stop();
    motion->stop();
    detector->stop();
    tracker->stop();
    stream_out->stop();
    if (rtsp_pusher) {
      rtsp_pusher->stop();
    }
    if (worker) {
      worker->stop();
    }
    return 1;
  }

  std::fprintf(stderr, "edge-vision-analyzer %s listening on :%d\n", edge::version(),
               api->port());

  while (!g_stop_requested.load()) {
    if (!options.record_clip.empty() && capture->recording_finished()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Stop upstream first, then wait for each downstream stage to drain. The
  // streamer node stops before the pusher so no worker can enter push() while
  // the encoder process is being reaped.
  capture->stop();
  motion->stop();
  detector->stop();
  tracker->stop();
  stream_out->stop();
  if (rtsp_pusher) {
    rtsp_pusher->stop();
  }
  if (worker) {
    worker->stop();
  }
  api->stop();
  return 0;
}
