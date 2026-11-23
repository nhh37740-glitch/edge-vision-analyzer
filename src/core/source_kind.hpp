#pragma once

#include <string>

namespace edge {

enum class SourceKind {
  kFile,
  kNetwork,
  kUnknown,
};

// Network sources are opened with protocol timeouts and reconnected after a
// failure; local files are rewound or stopped at EOF.
SourceKind classify_source(const std::string& location);
bool is_network_source(const std::string& location);

}  // namespace edge
