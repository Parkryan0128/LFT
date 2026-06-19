#pragma once

#include "transfer/quic_client.h"
#include "transfer/quic_server.h"
#include "transfer/sha256.h"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace lft::test {

inline constexpr const char* kLoopbackHost = "127.0.0.1";

struct Sync {
    std::mutex mutex;
    std::condition_variable cv;
    bool server_ready = false;
    bool server_failed = false;
    bool client_done = false;
};

inline bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

inline bool hashes_match(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::string ha;
    std::string hb;
    return sha256_file(a.string(), ha) && sha256_file(b.string(), hb) && ha == hb;
}

// Run a file transfer over loopback; returns true on success and hash match.
inline bool run_file_transfer(uint16_t port,
                              const std::filesystem::path& input,
                              const std::filesystem::path& output,
                              int timeout_ms = 15000) {
    QuicServer server(port);
    Sync sync;

    std::thread server_thread([&]() {
        if (!server.start(kLoopbackHost)) {
            std::lock_guard lock(sync.mutex);
            sync.server_ready = true;
            sync.server_failed = true;
            sync.cv.notify_all();
            return;
        }

        server.receive_file(output.string(), timeout_ms, [&] {
            std::lock_guard lock(sync.mutex);
            sync.server_ready = true;
            sync.cv.notify_all();
        });

        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        lock.unlock();
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
        if (sync.server_failed) {
            server_thread.join();
            return false;
        }
    }

    bool ok = true;
    QuicClient client;
    if (!client.connect(kLoopbackHost, port)) {
        ok = false;
    } else if (!client.send_file(input.string(), timeout_ms)) {
        ok = false;
    }
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();

    return ok && std::filesystem::exists(output) && hashes_match(input, output);
}

}  // namespace lft::test
