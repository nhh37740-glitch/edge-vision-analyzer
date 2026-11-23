#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/frame_meta.hpp"

namespace edge {

// One in-memory JPEG plus its Base64 text. The caller encodes once and reuses
// the same bytes/Base64 for both the optional LLM and storage requests.
struct EncodedImage {
  std::vector<std::uint8_t> jpeg;
  std::string base64;
};

// Encodes frame.rgb_data (RGB24) as a JPEG at the given quality and returns
// its Base64 form without a data URI prefix. Returns an empty result when the
// frame does not contain a usable RGB24 buffer.
EncodedImage encode_jpeg_base64(const FrameMeta& frame, int quality);

// Standard Base64 with '=' padding, no line breaks and no data URI prefix.
std::string base64_encode(const std::uint8_t* data, std::size_t size);

}  // namespace edge
