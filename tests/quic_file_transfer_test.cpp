// Milestone 1 Step 6: send one file over QUIC loopback and verify SHA-256.
#include "transfer/quic_client.h"
#include "transfer/quic_server.h"
#include "transfer/sha256.h"

#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

constexpr uint16_t kTestPort = 14435;
constexpr const char* kTestHost = "127.0.0.1";
constexpr const char* kTestContent =
    "LFT step 6 — file transfer over QUIC with SHA-256 verification.\n";

bool write_test_file(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(kTestContent, static_cast<std::streamsize>(std::strlen(kTestContent)));
    return static_cast<bool>(out);
}

bool files_match(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::string hash_a;
    std::string hash_b;
    if (!lft::sha256_file(a.string(), hash_a) || !lft::sha256_file(b.string(), hash_b)) {
        return false;
    }
    return hash_a == hash_b;
}

}  // namespace

int main() {
    const auto tmp_dir = std::filesystem::temp_directory_path() / "lft_quic_test";
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);

    const auto input_path = tmp_dir / "input.txt";
    const auto output_path = tmp_dir / "output.txt";

    if (!write_test_file(input_path)) {
        std::cerr << "failed to create test input file\n";
        return 1;
    }

    std::cout << "=== QUIC File Transfer Test ===\n";

    lft::QuicServer server(kTestPort);

    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool server_ready = false;
    bool server_failed = false;
    bool client_done = false;

    std::thread server_thread([&]() {
        if (!server.start(kTestHost)) {
            std::lock_guard lock(sync_mutex);
            server_ready = true;
            server_failed = true;
            sync_cv.notify_all();
            return;
        }

        if (!server.receive_file(
                output_path.string(),
                /*timeout_ms=*/10000,
                [&] {
                    std::lock_guard lock(sync_mutex);
                    server_ready = true;
                    sync_cv.notify_all();
                })) {
            std::cerr << "server: receive_file failed\n";
        }

        // Wait until the client has read the OK ack before tearing down.
        std::unique_lock lock(sync_mutex);
        sync_cv.wait(lock, [&] { return client_done; });
        lock.unlock();

        server.stop();
    });

    {
        std::unique_lock lock(sync_mutex);
        sync_cv.wait(lock, [&] { return server_ready; });
        if (server_failed) {
            std::cerr << "server failed to start\n";
            server_thread.join();
            return 1;
        }
    }

    lft::QuicClient client;
    if (!client.connect(kTestHost, kTestPort)) {
        std::cerr << "client: connect failed\n";
        server_thread.join();
        return 1;
    }

    if (!client.send_file(input_path.string(), /*timeout_ms=*/10000)) {
        std::cerr << "client: send_file failed\n";
        client.disconnect();
        {
            std::lock_guard lock(sync_mutex);
            client_done = true;
        }
        sync_cv.notify_all();
        server_thread.join();
        return 1;
    }

    client.disconnect();

    {
        std::lock_guard lock(sync_mutex);
        client_done = true;
    }
    sync_cv.notify_all();
    server_thread.join();

    if (!std::filesystem::exists(output_path)) {
        std::cerr << "output file was not created\n";
        return 1;
    }

    if (!files_match(input_path, output_path)) {
        std::cerr << "hash mismatch between input and output\n";
        return 1;
    }

    std::cout << "Quic file transfer OK (SHA-256 verified)\n";
    return 0;
}
