#include "core/source_kind.hpp"

namespace edge {
namespace {

bool has_prefix(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

}  // namespace

bool is_network_source(const std::string& location) {
  return has_prefix(location, "http://") || has_prefix(location, "https://") ||
         has_prefix(location, "rtsp://") || has_prefix(location, "rtmp://") ||
         has_prefix(location, "tcp://") || has_prefix(location, "udp://");
}

SourceKind classify_source(const std::string& location) {
  if (location.empty()) {
    return SourceKind::kUnknown;
  }
  return is_network_source(location) ? SourceKind::kNetwork : SourceKind::kFile;
}

}  // namespace edge
