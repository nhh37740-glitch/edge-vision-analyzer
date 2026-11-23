#include "capture/frame_dumper.hpp"

#include <cstdio>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace edge {

FrameDumper::FrameDumper(std::string directory, int interval_sec)
    : directory_(std::move(directory)), interval_sec_(interval_sec) {}

void FrameDumper::maybe_dump(const FrameMeta& frame) {
  if (interval_sec_ <= 0 || frame.rgb_data.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (has_dumped_ && now - last_dump_ < std::chrono::seconds(interval_sec_)) {
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) {
    std::fprintf(stderr, "FrameDumper: cannot create %s: %s\n", directory_.c_str(),
                 error.message().c_str());
    return;
  }

  const auto path =
      std::filesystem::path(directory_) / ("frame_" + std::to_string(frame.frame_id) + ".jpg");
  if (stbi_write_jpg(path.string().c_str(), frame.width, frame.height, 3, frame.rgb_data.data(),
                     85) == 0) {
    std::fprintf(stderr, "FrameDumper: cannot write %s\n", path.string().c_str());
    return;
  }
  last_dump_ = now;
  has_dumped_ = true;
}

}  // namespace edge
