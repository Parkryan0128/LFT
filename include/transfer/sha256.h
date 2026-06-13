#pragma once

#include <string>
#include <string_view>

namespace lft {

// Compute SHA-256 of a file; returns lowercase hex (64 chars).
bool sha256_file(const std::string& path, std::string& out_hex);

// Compute SHA-256 of bytes in memory; returns lowercase hex (64 chars).
bool sha256_bytes(std::string_view data, std::string& out_hex);

}  // namespace lft
