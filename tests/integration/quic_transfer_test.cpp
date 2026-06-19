// QUIC integration tests: server lifecycle, connect, echo, file transfer, edge cases.
#include "integration/quic_test_fixture.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

TEST(QuicServer, StartStop) {
    lft::QuicServer server(4433);
    ASSERT_TRUE(server.start(lft::test::kLoopbackHost));
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(QuicConnect, Handshake) {
    constexpr uint16_t kPort = 14433;
    lft::QuicServer server(kPort);
    lft::test::Sync sync;

    std::thread server_thread([&]() {
        const bool ok = server.start(lft::test::kLoopbackHost);
        {
            std::lock_guard lock(sync.mutex);
            sync.server_ready = true;
            sync.server_failed = !ok;
        }
        sync.cv.notify_all();
        if (!ok) {
            return;
        }

        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        lock.unlock();
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
        ASSERT_FALSE(sync.server_failed);
    }

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, kPort));
    EXPECT_TRUE(client.is_connected());
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();
}

TEST(QuicEcho, RoundTrip) {
    constexpr uint16_t kPort = 14434;
    constexpr const char* kMessage = "hello";

    lft::QuicServer server(kPort);
    std::string server_reply;
    lft::test::Sync sync;

    std::thread server_thread([&]() {
        if (!server.start(lft::test::kLoopbackHost)) {
            std::lock_guard lock(sync.mutex);
            sync.server_ready = true;
            sync.server_failed = true;
            sync.cv.notify_all();
            return;
        }

        ASSERT_TRUE(server.wait_for_echo(
            kMessage,
            server_reply,
            /*timeout_ms=*/5000,
            [&] {
                std::lock_guard lock(sync.mutex);
                sync.server_ready = true;
                sync.cv.notify_all();
            }));

        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        lock.unlock();
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
        ASSERT_FALSE(sync.server_failed);
    }

    lft::QuicClient client;
    std::string client_reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, kPort));
    ASSERT_TRUE(client.send_echo(kMessage, client_reply, /*timeout_ms=*/5000));
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();

    EXPECT_EQ(client_reply, kMessage);
}

TEST(QuicFile, HappyPath) {
    constexpr uint16_t kPort = 14435;
    constexpr const char* kContent =
        "LFT step 6 — file transfer over QUIC with SHA-256 verification.\n";

    const auto tmp_dir = fs::temp_directory_path() / "lft_quic_test";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    const auto input_path = tmp_dir / "input.txt";
    const auto output_path = tmp_dir / "output.txt";
    ASSERT_TRUE(lft::test::write_file(input_path, kContent));

    lft::QuicServer server(kPort);
    lft::test::Sync sync;

    std::thread server_thread([&]() {
        if (!server.start(lft::test::kLoopbackHost)) {
            std::lock_guard lock(sync.mutex);
            sync.server_ready = true;
            sync.server_failed = true;
            sync.cv.notify_all();
            return;
        }

        ASSERT_TRUE(server.receive_file(
            output_path.string(),
            /*timeout_ms=*/10000,
            [&] {
                std::lock_guard lock(sync.mutex);
                sync.server_ready = true;
                sync.cv.notify_all();
            }));

        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        lock.unlock();
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
        ASSERT_FALSE(sync.server_failed);
    }

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, kPort));
    ASSERT_TRUE(client.send_file(input_path.string(), /*timeout_ms=*/10000));
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();

    ASSERT_TRUE(fs::exists(output_path));
    EXPECT_TRUE(lft::test::hashes_match(input_path, output_path));
}

TEST(QuicFile, EmptyFile) {
    const auto dir = fs::temp_directory_path() / "lft_edge_test";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const auto input = dir / "in_empty";
    const auto output = dir / "out_empty";
    ASSERT_TRUE(lft::test::write_file(input, ""));
    EXPECT_TRUE(lft::test::run_file_transfer(15510, input, output));
}

TEST(QuicFile, SmallText) {
    const auto dir = fs::temp_directory_path() / "lft_edge_test";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const auto input = dir / "in_small";
    const auto output = dir / "out_small";
    ASSERT_TRUE(lft::test::write_file(input, "hello world\n"));
    EXPECT_TRUE(lft::test::run_file_transfer(15511, input, output));
}

TEST(QuicFile, MultiChunk200KB) {
    std::string big;
    big.reserve(200 * 1024);
    for (int i = 0; i < 200 * 1024; ++i) {
        big.push_back(static_cast<char>(i % 251));
    }

    const auto dir = fs::temp_directory_path() / "lft_edge_test";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const auto input = dir / "in_big";
    const auto output = dir / "out_big";
    ASSERT_TRUE(lft::test::write_file(input, big));
    EXPECT_TRUE(lft::test::run_file_transfer(15512, input, output));
}

TEST(QuicFile, BinaryWithNulAndDelimiter) {
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

    const auto dir = fs::temp_directory_path() / "lft_edge_test";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const auto input = dir / "in_binary";
    const auto output = dir / "out_binary";
    ASSERT_TRUE(lft::test::write_file(input, binary));
    EXPECT_TRUE(lft::test::run_file_transfer(15513, input, output));
}
