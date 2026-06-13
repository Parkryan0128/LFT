#include "transfer/quic_transfer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <sstream>

namespace lft {

namespace {

constexpr const char* kMagic = "LFT/1";

// Parse an unsigned 64-bit integer, requiring the ENTIRE text to be digits
// (rejects "123abc", empty strings, leading signs, and overflow).
bool parse_full_u64(std::string_view text, uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}  // namespace

std::string sanitize_file_name(std::string_view raw) {
    std::string name = std::filesystem::path(std::string(raw)).filename().string();

    // Reject names that resolve to nothing useful or to directory traversal.
    if (name.empty() || name == "." || name == "..") {
        return "received.bin";
    }

    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\') {
            c = '_';
        }
    }
    return name;
}

bool is_sha256_hex(std::string_view s) {
    if (s.size() != 64) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::string encode_file_header(const FileTransferHeader& header) {
    std::string hash_lower = header.sha256_hex;
    std::transform(hash_lower.begin(), hash_lower.end(), hash_lower.begin(), to_lower_ascii);

    std::ostringstream oss;
    oss << kMagic << '\n'
        << "name=" << sanitize_file_name(header.name) << '\n'
        << "size=" << header.size << '\n'
        << "hash=" << hash_lower << '\n'
        << '\n';
    return oss.str();
}

bool decode_file_header(std::string_view data,
                        FileTransferHeader& header,
                        size_t& header_byte_count) {
    const size_t end = data.find("\n\n");
    if (end == std::string_view::npos) {
        return false;
    }

    const std::string_view block = data.substr(0, end);
    header = {};

    bool has_name = false;
    bool has_size = false;
    bool has_hash = false;

    size_t pos = 0;
    bool first_line = true;
    while (pos <= block.size()) {
        const size_t line_end = block.find('\n', pos);
        const std::string_view line = (line_end == std::string_view::npos)
                                          ? block.substr(pos)
                                          : block.substr(pos, line_end - pos);

        if (first_line) {
            if (line != kMagic) {
                return false;
            }
            first_line = false;
        } else if (!line.empty()) {
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos) {
                return false;
            }
            const std::string_view key = line.substr(0, eq);
            const std::string_view value = line.substr(eq + 1);

            if (key == "name") {
                header.name = sanitize_file_name(value);
                has_name = true;
            } else if (key == "size") {
                if (!parse_full_u64(value, header.size)) {
                    return false;
                }
                has_size = true;
            } else if (key == "hash") {
                if (!is_sha256_hex(value)) {
                    return false;
                }
                header.sha256_hex.assign(value);
                std::transform(header.sha256_hex.begin(), header.sha256_hex.end(),
                               header.sha256_hex.begin(), to_lower_ascii);
                has_hash = true;
            }
            // Unknown keys are ignored for forward compatibility.
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        pos = line_end + 1;
    }

    // size may legitimately be 0 (empty file), so we check presence, not value.
    if (!has_name || !has_size || !has_hash || header.name.empty()) {
        return false;
    }

    header_byte_count = end + 2;
    return true;
}

}  // namespace lft
