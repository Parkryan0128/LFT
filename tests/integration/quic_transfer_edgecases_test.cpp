// Integration edge-case tests for QUIC file transfer, accept/reject, and failures.
#include "integration/quic_test_access.h"
#include "integration/quic_test_fixture.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string file_hash(const fs::path& path) {
    std::string hash;
    EXPECT_TRUE(lft::sha256_file(path.string(), hash));
    return hash;
}

lft::FileTransferHeader header_for_file(const fs::path& path) {
    std::error_code ec;
    return lft::FileTransferHeader{
        .name = path.filename().string(),
        .size = fs::file_size(path, ec),
        .sha256_hex = file_hash(path),
    };
}

}  // namespace

TEST(QuicClientValidation, SendFileNotConnected) {
    lft::QuicClient client;
    EXPECT_FALSE(client.send_file("/tmp/does-not-matter", 1000));
    EXPECT_FALSE(client.was_rejected());
}

TEST(QuicClientValidation, SendFileMissingPath) {
    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive("/tmp/lft_missing_out.bin", [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file("/tmp/lft_definitely_missing_file_12345.bin", 5000));
    client.disconnect();
    session.signal_client_done();
    session.join();
}

TEST(QuicClientValidation, SendFileDirectoryPath) {
    const auto dir = lft::test::temp_test_dir("lft_dir_send_test");
    const uint16_t port = lft::test::next_port();

    lft::test::ServerSession session(port);
    session.start_receive((dir / "out.bin").string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file(dir.string(), 5000));
    client.disconnect();
    session.signal_client_done();
    session.join();
}

TEST(QuicConnect, WrongPortFails) {
    lft::QuicClient client;
    EXPECT_FALSE(client.connect(lft::test::kLoopbackHost, 1));
    EXPECT_FALSE(client.is_connected());
}

TEST(QuicFileDecision, ReceiverRejectsBeforeBody) {
    const auto dir = lft::test::temp_test_dir("lft_reject_test");
    const auto input = dir / "secret.txt";
    const auto output = dir / "should_not_exist.txt";
    ASSERT_TRUE(lft::test::write_file(input, "top secret"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(
        output.string(),
        [&] {
            std::lock_guard lock(session.sync.mutex);
            session.sync.server_ready = true;
            session.sync.cv.notify_all();
        },
        [](const lft::FileTransferHeader&) { return false; });

    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file(input.string(), 5000));
    EXPECT_TRUE(client.was_rejected());
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_TRUE(session.result.rejected);
    EXPECT_FALSE(fs::exists(output));
}

TEST(QuicFileDecision, ReceiverAcceptsViaOfferCallback) {
    const auto dir = lft::test::temp_test_dir("lft_accept_offer_test");
    const auto input = dir / "payload.txt";
    const auto output = dir / "received.txt";
    ASSERT_TRUE(lft::test::write_file(input, "accepted payload"));

    std::string offered_name;
    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(
        output.string(),
        [&] {
            std::lock_guard lock(session.sync.mutex);
            session.sync.server_ready = true;
            session.sync.cv.notify_all();
        },
        [&](const lft::FileTransferHeader& header) {
            offered_name = header.name;
            return true;
        });

    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_TRUE(client.send_file(input.string(), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_TRUE(session.receive_ok);
    EXPECT_EQ(offered_name, "payload.txt");
    EXPECT_TRUE(lft::test::hashes_match(input, output));
}

TEST(QuicFileOutput, SaveToDirectoryUsesSenderFilename) {
    const auto dir = lft::test::temp_test_dir("lft_dir_recv_test");
    const auto input = dir / "nested_name.txt";
    ASSERT_TRUE(lft::test::write_file(input, "directory target"));

    const uint16_t port = lft::test::next_port();
    const auto out_dir = dir / "incoming";
    fs::create_directories(out_dir);

    lft::test::ServerSession session(port);
    session.start_receive(out_dir.string() + "/", [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_file(input.string(), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    const auto output = out_dir / "nested_name.txt";
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(lft::test::hashes_match(input, output));
}

TEST(QuicFileOutput, TraversalFilenameIsSanitized) {
    const auto dir = lft::test::temp_test_dir("lft_traversal_test");
    const auto input = dir / "local.txt";
    ASSERT_TRUE(lft::test::write_file(input, "safe bytes"));

    const uint16_t port = lft::test::next_port();
    const auto out_dir = dir / "incoming";
    fs::create_directories(out_dir);

    lft::test::ServerSession session(port);
    session.start_receive(out_dir.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    auto header = header_for_file(input);
    header.name = "../../etc/passwd";

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(lft::test::QuicTestAccess::SendFileWithHeader(
        client, input.string(), std::move(header), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(fs::exists(dir / "passwd"));
    const auto safe_output = out_dir / "passwd";
    EXPECT_TRUE(fs::exists(safe_output));
    EXPECT_TRUE(lft::test::hashes_match(input, safe_output));
}

TEST(QuicFile, ExactChunkBoundary64KB) {
    std::string payload(64 * 1024, '\xAB');

    const auto dir = lft::test::temp_test_dir("lft_chunk_boundary_test");
    const auto input = dir / "exact_chunk.bin";
    const auto output = dir / "out_chunk.bin";
    ASSERT_TRUE(lft::test::write_file(input, payload));

    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, ProgressCallbackFires) {
    const auto dir = lft::test::temp_test_dir("lft_progress_test");
    const auto input = dir / "progress.bin";
    const auto output = dir / "out_progress.bin";
    std::string payload(128 * 1024, 'p');
    ASSERT_TRUE(lft::test::write_file(input, payload));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    uint64_t last_done = 0;
    uint64_t last_total = 0;
    int progress_calls = 0;

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_file(
        input.string(),
        5000,
        [&](uint64_t done, uint64_t total) {
            last_done = done;
            last_total = total;
            ++progress_calls;
        }));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_GT(progress_calls, 0);
    EXPECT_EQ(last_done, payload.size());
    EXPECT_EQ(last_total, payload.size());
}

TEST(QuicFileFailure, HashMismatch) {
    const auto dir = lft::test::temp_test_dir("lft_hash_mismatch_test");
    const auto input = dir / "good.bin";
    const auto output = dir / "bad_out.bin";
    ASSERT_TRUE(lft::test::write_file(input, "hash mismatch payload"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    auto header = header_for_file(input);
    header.sha256_hex = std::string(64, '0');

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(lft::test::QuicTestAccess::SendFileWithHeader(
        client, input.string(), std::move(header), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "hash mismatch");
}

TEST(QuicFileFailure, DeclaredSizeLargerThanBody) {
    const auto dir = lft::test::temp_test_dir("lft_size_mismatch_test");
    const auto input = dir / "short.bin";
    const auto output = dir / "short_out.bin";
    ASSERT_TRUE(lft::test::write_file(input, "12345"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    auto header = header_for_file(input);
    header.size = 100;

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(lft::test::QuicTestAccess::SendFileWithHeader(
        client, input.string(), std::move(header), 5000, 5));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "size mismatch");
}

TEST(QuicFileFailure, ExtraBytesBeyondDeclaredSize) {
    const auto dir = lft::test::temp_test_dir("lft_overflow_test");
    const auto input = dir / "overflow.bin";
    const auto output = dir / "overflow_out.bin";
    ASSERT_TRUE(lft::test::write_file(input, std::string(32, 'x')));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    auto header = header_for_file(input);
    header.size = 10;

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(lft::test::QuicTestAccess::SendFileWithHeader(
        client, input.string(), std::move(header), 5000, 32));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "peer sent more bytes than declared");
}

TEST(QuicFileFailure, MalformedHeaderTooLarge) {
    const uint16_t port = lft::test::next_port();
    const auto dir = lft::test::temp_test_dir("lft_bad_header_test");
    const auto output = dir / "never_written.bin";

    lft::test::ServerSession session(port);
    session.start_receive(output.string(), [&] {
        std::lock_guard lock(session.sync.mutex);
        session.sync.server_ready = true;
        session.sync.cv.notify_all();
    });
    ASSERT_TRUE(session.wait_ready());

    std::string garbage(lft::kMaxHeaderBytes + 1, 'G');

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(lft::test::QuicTestAccess::SendRawStream(client, garbage, true, 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "header too large or malformed");
    EXPECT_FALSE(fs::exists(output));
}

TEST(QuicEcho, EmptyMessage) {
    const uint16_t port = lft::test::next_port();
    lft::QuicServer server(port);
    std::string server_reply;
    lft::test::Sync sync;

    std::thread server_thread([&]() {
        ASSERT_TRUE(server.start(lft::test::kLoopbackHost));
        ASSERT_TRUE(server.wait_for_echo(
            "",
            server_reply,
            5000,
            [&] {
                std::lock_guard lock(sync.mutex);
                sync.server_ready = true;
                sync.cv.notify_all();
            }));
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
    }

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_echo("", reply, 5000));
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();

    EXPECT_EQ(reply, "");
}

TEST(QuicEcho, LargeMessage) {
    const std::string message(32 * 1024, 'E');
    const uint16_t port = lft::test::next_port();
    lft::QuicServer server(port);
    std::string server_reply;
    lft::test::Sync sync;

    std::thread server_thread([&]() {
        ASSERT_TRUE(server.start(lft::test::kLoopbackHost));
        ASSERT_TRUE(server.wait_for_echo(
            message,
            server_reply,
            10000,
            [&] {
                std::lock_guard lock(sync.mutex);
                sync.server_ready = true;
                sync.cv.notify_all();
            }));
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.client_done; });
        server.stop();
    });

    {
        std::unique_lock lock(sync.mutex);
        sync.cv.wait(lock, [&] { return sync.server_ready; });
    }

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_echo(message, reply, 10000));
    client.disconnect();

    {
        std::lock_guard lock(sync.mutex);
        sync.client_done = true;
    }
    sync.cv.notify_all();
    server_thread.join();

    EXPECT_EQ(reply, message);
}
