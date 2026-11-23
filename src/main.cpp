#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "api/api_server.hpp"
#include "capture/capture_node.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "core/build_info.hpp"

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
  auto api = std::make_shared<edge::ApiServer>(config, capture, metrics);

  std::string error;
  if (!api->start(error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  capture->start();
  std::fprintf(stderr, "edge-vision-analyzer %s listening on :%d\n", edge::version(),
               api->port());

  while (!g_stop_requested.load()) {
    if (!options.record_clip.empty() && capture->recording_finished()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  capture->stop();
  api->stop();
  return 0;
}
