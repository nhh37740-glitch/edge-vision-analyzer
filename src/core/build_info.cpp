#include "core/build_info.hpp"

#ifndef EDGE_VERSION
// Non-CMake builds keep a stable placeholder instead of guessing a Tag.
#define EDGE_VERSION "unknown"
#endif

namespace edge {

const char* version() { return EDGE_VERSION; }

}  // namespace edge
