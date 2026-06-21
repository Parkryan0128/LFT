#pragma once

#include <cstdint>
#include <string>

namespace lft {

// Format a byte count as a human-readable string (e.g. "1.5 MB").
std::string format_bytes(uint64_t bytes);

}  // namespace lft
