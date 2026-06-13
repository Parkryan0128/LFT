// Edge-case integration tests for QUIC file transfer:
//   - empty (0-byte) file
//   - multi-chunk file (larger than the 64 KB send chunk)
//   - binary file containing NUL bytes and an embedded "\n\n" (header delimiter)
#include "transfer/quic_client.h"
#include "transfer/quic_server.h"
#include "transfer/sha256.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

bool hashes_match(const fs::path& a, const fs::path& b) {
    std::string ha;
    std::string hb;
    return lft::sha256_file(a.string(), ha) &&
           lft::sha256_file(b.string(), hb) &&
           ha == hb;
}

// Run a single file transfer over loopback. Returns true on success + hash match.
bool run_transfer(uint16_t port, const fs::path& input, const fs::path& output) {
    const char* host = "127.0.0.1";
    lft::QuicServer server(port);

    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    bool failed = false;
    bool client_done = false;

    std::thread server_thread([&]() {
        if (!server.start(host)) {
            std::lock_guard lock(m);
            ready = true;
            failed = true;
            cv.notify_all();
            return;
        }

        server.receive_file(output.string(), /*timeout_ms=*/15000, [&] {
            std::lock_guard lock(m);
            ready = true;
            cv.notify_all();
        });

        std::unique_lock lock(m);
        cv.wait(lock, [&] { return client_done; });
        lock.unlock();
        server.stop();
    });

    {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return ready; });
        if (failed) {
            server_thread.join();
            return false;
        }
    }

    bool ok = true;
    lft::QuicClient client;
    if (!client.connect(host, port)) {
        ok = false;
    } else if (!client.send_file(input.string(), /*timeout_ms=*/15000)) {
        ok = false;
    }
    client.disconnect();

    {
        std::lock_guard lock(m);
        client_done = true;
    }
    cv.notify_all();
    server_thread.join();

    if (!ok) {
        return false;
    }
    if (!fs::exists(output)) {
        std::cerr << "output not created: " << output << '\n';
        return false;
    }
    if (!hashes_match(input, output)) {
        std::cerr << "hash mismatch for " << input << '\n';
        return false;
    }
    return true;
}

struct Case {
    const char* name;
    uint16_t port;
    std::string content;
};

}  // namespace

int main() {
    const auto dir = fs::temp_directory_path() / "lft_edge_test";
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Build a multi-chunk payload (> 64 KB) and a binary payload with NULs + "\n\n".
    std::string big;
    big.reserve(200 * 1024);
    for (int i = 0; i < 200 * 1024; ++i) {
        big.push_back(static_cast<char>(i % 251));
    }

    // Binary payload with embedded NUL bytes and the "\n\n" header delimiter,
    // to make sure body framing never confuses these with the header block.
    std::string binary;
    binary += "prefix";
    binary += '\n';
    binary += '\n';
    binary += "MIDDLE";
    binary.push_back('\0');
    binary.push_back('\0');
    binary.push_back(static_cast<char>(0xFF));
    binary.push_back(static_cast<char>(0xFE));
    binary += "tail\n\nmore";

    std::vector<Case> cases = {
        {"empty file", 15510, ""},
        {"small text", 15511, "hello world\n"},
        {"multi-chunk (200 KB)", 15512, big},
        {"binary with NUL and \\n\\n", 15513, binary},
    };

    int failures = 0;
    for (const auto& c : cases) {
        const auto in = dir / (std::string("in_") + std::to_string(c.port));
        const auto out = dir / (std::string("out_") + std::to_string(c.port));

        if (!write_file(in, c.content)) {
            std::cerr << "FAIL [" << c.name << "]: could not write input\n";
            ++failures;
            continue;
        }

        if (run_transfer(c.port, in, out)) {
            std::cout << "PASS [" << c.name << "] (" << c.content.size() << " bytes)\n";
        } else {
            std::cerr << "FAIL [" << c.name << "]\n";
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "All file edge-case transfers OK\n";
        return 0;
    }
    std::cerr << failures << " edge case(s) failed\n";
    return 1;
}
