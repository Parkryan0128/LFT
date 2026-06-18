#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lft {

// Address and port for QUIC connections (loopback in Milestone 1).
struct QuicEndpoint {
    std::string host;
    uint16_t port = 0;
};

// Result of a one-shot echo round-trip (client sends, server replies).
struct QuicEchoResult {
    bool success = false;
    std::string message_sent;
    std::string message_received;
    std::string error;
};

// Largest header block we will buffer before giving up (guards against a peer
// that never sends the terminating blank line).
inline constexpr size_t kMaxHeaderBytes = 64 * 1024;

// Step 6 wire format (text header, then raw file bytes):
//
//   LFT/1\n
//   name=<filename>\n
//   size=<byte count>\n
//   hash=<64-char sha256 hex>\n
//   \n
//   <exactly `size` bytes>
struct FileTransferHeader {
    std::string name;
    uint64_t size = 0;
    std::string sha256_hex;
};

// Strip a path to its filename component and replace control characters.
// Used so a malicious/odd header name can never break framing or escape a dir.
std::string sanitize_file_name(std::string_view raw);

// True if `s` is exactly 64 lowercase-or-uppercase hex characters.
bool is_sha256_hex(std::string_view s);

struct FileReceiveResult {
    bool success = false;
    bool rejected = false;  // receiver declined the transfer (not an error)
    std::string path;
    std::string expected_hash;
    std::string computed_hash;
    uint64_t bytes_received = 0;
    std::string error;
};

// Build the header block (ends with blank line). Does not include file bytes.
std::string encode_file_header(const FileTransferHeader& header);

// Parse a header from the front of `data`. On success, sets `header` and
// `header_byte_count` (total bytes consumed, including trailing blank line).
bool decode_file_header(std::string_view data,
                        FileTransferHeader& header,
                        size_t& header_byte_count);

}  // namespace lft
