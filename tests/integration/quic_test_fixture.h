#pragma once

#include "transfer/quic_client.h"
#include "transfer/quic_server.h"
#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
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

inline uint16_t next_port() {
    static std::atomic<uint16_t> port{15600};
    return port.fetch_add(1);
}

inline std::filesystem::path temp_test_dir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

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

struct ServerSession {
    QuicServer server;
    Sync sync;
    std::thread thread;
    bool receive_ok = false;
    FileReceiveResult result;
    int timeout_ms = 15000;

    explicit ServerSession(uint16_t port) : server(port) {}

    void start_receive(const std::string& output_path,
                       std::function<void()> on_armed = nullptr,
                       std::function<bool(const FileTransferHeader&)> on_offer = nullptr) {
        thread = std::thread([this, output_path, on_armed = std::move(on_armed),
                              on_offer = std::move(on_offer)]() mutable {
            if (!server.start(kLoopbackHost)) {
                std::lock_guard lock(sync.mutex);
                sync.server_ready = true;
                sync.server_failed = true;
                sync.cv.notify_all();
                return;
            }

            receive_ok = server.receive_file(
                output_path, timeout_ms, std::move(on_armed), std::move(on_offer));
            result = server.last_file_result();

            std::unique_lock lock(sync.mutex);
            sync.cv.wait(lock, [&] { return sync.client_done; });
            lock.unlock();
            server.stop();
        });
    }

    bool wait_ready() {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
        return !sync.server_failed;
    }

    void signal_client_done() {
        {
            std::lock_guard lock(sync.mutex);
            sync.client_done = true;
        }
        sync.cv.notify_all();
    }

    void join() {
        if (thread.joinable()) {
            thread.join();
        }
    }
};

inline bool run_file_transfer(uint16_t port,
                              const std::filesystem::path& input,
                              const std::filesystem::path& output,
                              int timeout_ms = 15000) {
    ServerSession session(port);
    session.timeout_ms = timeout_ms;
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });

    if (!session.wait_ready()) {
        session.join();
        return false;
    }

    bool ok = true;
    QuicClient client;
    if (!client.connect(kLoopbackHost, port)) {
        ok = false;
    } else if (!client.send_file(input.string(), timeout_ms)) {
        ok = false;
    }
    client.disconnect();

    session.signal_client_done();
    session.join();

    return ok && std::filesystem::exists(output) && hashes_match(input, output);
}

class ReadOnlyPath {
public:
    explicit ReadOnlyPath(const std::filesystem::path& path) : path_(path) {
        std::error_code ec;
        saved_ = std::filesystem::status(path_, ec).permissions();
        std::filesystem::permissions(
            path_,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace,
            ec);
    }

    ~ReadOnlyPath() {
        std::error_code ec;
        std::filesystem::permissions(path_, saved_, std::filesystem::perm_options::replace, ec);
    }

    ReadOnlyPath(const ReadOnlyPath&) = delete;
    ReadOnlyPath& operator=(const ReadOnlyPath&) = delete;

private:
    std::filesystem::path path_;
    std::filesystem::perms saved_{};
};

}  // namespace lft::test
