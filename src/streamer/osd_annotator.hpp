#pragma once

#include <cstdint>
#include <vector>

#include "core/frame_meta.hpp"

namespace edge {

// Draws 2px detection rectangles and labels built from an internal 8x8 ASCII
// bitmap font onto a caller-owned RGB24 buffer. src supplies frame geometry
// only; src.rgb_data is never modified. The caller must size dst to
// src.rgb_data.size() and normally pre-fills it with the source image.
// Target coordinates are normalized [0,1] and are clipped to the frame.
void draw_osd(const FrameMeta& src, const std::vector<TargetBox>& targets,
              std::vector<std::uint8_t>& dst);

}  // namespace edge
