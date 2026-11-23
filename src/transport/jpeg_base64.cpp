#include "transport/jpeg_base64.hpp"

#include <cstddef>

// Keep this implementation local to the translation unit. edge_capture owns
// the non-static stb implementation; STB_IMAGE_WRITE_STATIC avoids duplicate
// symbols when both libraries are linked into the same executable.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace edge {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void append_jpeg_bytes(void* context, void* data, int size) {
  if (size <= 0) {
    return;
  }
  auto* output = static_cast<std::vector<std::uint8_t>*>(context);
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  output->insert(output->end(), bytes, bytes + size);
}

}  // namespace

std::string base64_encode(const std::uint8_t* data, std::size_t size) {
  std::string output;
  output.reserve(((size + 2) / 3) * 4);
  for (std::size_t i = 0; i < size; i += 3) {
    const std::size_t remaining = size - i;
    std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
    if (remaining > 1) {
      chunk |= static_cast<std::uint32_t>(data[i + 1]) << 8;
    }
    if (remaining > 2) {
      chunk |= static_cast<std::uint32_t>(data[i + 2]);
    }
    output.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
    output.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
    output.push_back(remaining > 1 ? kBase64Alphabet[(chunk >> 6) & 0x3F] : '=');
    output.push_back(remaining > 2 ? kBase64Alphabet[chunk & 0x3F] : '=');
  }
  return output;
}

EncodedImage encode_jpeg_base64(const FrameMeta& frame, int quality) {
  EncodedImage image;
  if (frame.width <= 0 || frame.height <= 0) {
    return image;
  }
  const std::size_t expected =
      static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3;
  if (frame.rgb_data.size() < expected) {
    return image;
  }
  if (quality < 1) {
    quality = 1;
  } else if (quality > 100) {
    quality = 100;
  }

  const int written = stbi_write_jpg_to_func(append_jpeg_bytes, &image.jpeg, frame.width,
                                             frame.height, 3, frame.rgb_data.data(), quality);
  if (written == 0) {
    image.jpeg.clear();
    return image;
  }
  image.base64 = base64_encode(image.jpeg.data(), image.jpeg.size());
  return image;
}

}  // namespace edge
