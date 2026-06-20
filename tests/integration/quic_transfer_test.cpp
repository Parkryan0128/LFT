// QUIC integration tests: connect, echo, file transfer, and edge cases.
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

TEST(QuicServer, StartStop) {
    lft::QuicServer server(lft::test::next_port());
    ASSERT_TRUE(server.start(lft::test::kLoopbackHost));
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(QuicConnect, Handshake) {
    const uint16_t port = lft::test::next_port();
    lft::test::ListenSession session(port);
    session.start();
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_TRUE(client.is_connected());
    client.disconnect();

    session.signal_client_done();
    session.join();
}

TEST(QuicConnect, WrongPortFails) {
    lft::QuicClient client;
    EXPECT_FALSE(client.connect(lft::test::kLoopbackHost, 1));
    EXPECT_FALSE(client.is_connected());
}

TEST(QuicConnect, DisconnectThenReconnect) {
    const uint16_t port_a = lft::test::next_port();
    const uint16_t port_b = lft::test::next_port();

    lft::QuicServer server_a(port_a);
    lft::QuicServer server_b(port_b);
    ASSERT_TRUE(server_a.start(lft::test::kLoopbackHost));
    ASSERT_TRUE(server_b.start(lft::test::kLoopbackHost));

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port_a));
    EXPECT_TRUE(client.is_connected());
    client.disconnect();
    EXPECT_FALSE(client.is_connected());

    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port_b));
    EXPECT_TRUE(client.is_connected());
    client.disconnect();

    server_a.stop();
    server_b.stop();
}

TEST(QuicConnect, DoubleDisconnectSafe) {
    const uint16_t port = lft::test::next_port();
    lft::QuicServer server(port);
    ASSERT_TRUE(server.start(lft::test::kLoopbackHost));

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    client.disconnect();
    client.disconnect();
    EXPECT_FALSE(client.is_connected());

    server.stop();
}

TEST(QuicEcho, RoundTrip) {
    const uint16_t port = lft::test::next_port();
    lft::test::EchoSession session(port);
    session.start("hello");
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_echo("hello", reply, 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_EQ(reply, "hello");
}

TEST(QuicEcho, EmptyMessage) {
    const uint16_t port = lft::test::next_port();
    lft::test::EchoSession session(port);
    session.start("");
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_echo("", reply, 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_EQ(reply, "");
}

TEST(QuicEcho, LargeMessage) {
    const std::string message(32 * 1024, 'E');
    const uint16_t port = lft::test::next_port();
    lft::test::EchoSession session(port);
    session.timeout_ms = 10000;
    session.start(message);
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_echo(message, reply, 10000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_EQ(reply, message);
}

TEST(QuicEcho, MismatchFails) {
    const uint16_t port = lft::test::next_port();
    lft::test::EchoSession session(port);
    session.start("expected");
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    std::string reply;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_echo("different", reply, 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.echo_ok);
}

TEST(QuicClientValidation, SendFileNotConnected) {
    lft::QuicClient client;
    EXPECT_FALSE(client.send_file("/tmp/does-not-matter", 1000));
    EXPECT_FALSE(client.was_rejected());
}

TEST(QuicClientValidation, SendFileMissingPath) {
    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive("/tmp/lft_missing_out.bin");
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
    session.start_receive((dir / "out.bin").string());
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file(dir.string(), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();
}

TEST(QuicFile, HappyPath) {
    const auto dir = lft::test::temp_test_dir("lft_quic_test");
    const auto input = dir / "input.txt";
    const auto output = dir / "output.txt";
    ASSERT_TRUE(lft::test::write_file(
        input, "LFT file transfer over QUIC with SHA-256 verification.\n"));

    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, EmptyFile) {
    const auto dir = lft::test::temp_test_dir("lft_edge_test");
    const auto input = dir / "in_empty";
    const auto output = dir / "out_empty";
    ASSERT_TRUE(lft::test::write_file(input, ""));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, SmallText) {
    const auto dir = lft::test::temp_test_dir("lft_edge_test");
    const auto input = dir / "in_small";
    const auto output = dir / "out_small";
    ASSERT_TRUE(lft::test::write_file(input, "hello world\n"));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, MultiChunk200KB) {
    std::string big;
    big.reserve(200 * 1024);
    for (int i = 0; i < 200 * 1024; ++i) {
        big.push_back(static_cast<char>(i % 251));
    }

    const auto dir = lft::test::temp_test_dir("lft_edge_test");
    const auto input = dir / "in_big";
    const auto output = dir / "out_big";
    ASSERT_TRUE(lft::test::write_file(input, big));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, BinaryWithNulAndDelimiter) {
    std::string binary;
    binary += "prefix\n\nMIDDLE";
    binary.push_back('\0');
    binary.push_back('\0');
    binary.push_back(static_cast<char>(0xFF));
    binary.push_back(static_cast<char>(0xFE));
    binary += "tail\n\nmore";

    const auto dir = lft::test::temp_test_dir("lft_edge_test");
    const auto input = dir / "in_binary";
    const auto output = dir / "out_binary";
    ASSERT_TRUE(lft::test::write_file(input, binary));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, OneByteFile) {
    const auto dir = lft::test::temp_test_dir("lft_one_byte_test");
    const auto input = dir / "one.bin";
    const auto output = dir / "one_out.bin";
    ASSERT_TRUE(lft::test::write_file(input, "x"));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, ExactChunkBoundary64KB) {
    const auto dir = lft::test::temp_test_dir("lft_chunk_boundary_test");
    const auto input = dir / "exact_chunk.bin";
    const auto output = dir / "out_chunk.bin";
    ASSERT_TRUE(lft::test::write_file(input, std::string(64 * 1024, '\xAB')));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
}

TEST(QuicFile, ProgressCallbackFires) {
    const auto dir = lft::test::temp_test_dir("lft_progress_test");
    const auto input = dir / "progress.bin";
    const auto output = dir / "out_progress.bin";
    const std::string payload(128 * 1024, 'p');
    ASSERT_TRUE(lft::test::write_file(input, payload));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string());
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

TEST(QuicFileDecision, ReceiverRejectsBeforeBody) {
    const auto dir = lft::test::temp_test_dir("lft_reject_test");
    const auto input = dir / "secret.txt";
    const auto output = dir / "should_not_exist.txt";
    ASSERT_TRUE(lft::test::write_file(input, "top secret"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), nullptr, [](const lft::FileTransferHeader&) {
        return false;
    });
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
        nullptr,
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
    const auto out_dir = dir / "incoming";
    fs::create_directories(out_dir);
    ASSERT_TRUE(lft::test::write_file(input, "directory target"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(out_dir.string() + "/");
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

TEST(QuicFileOutput, ExplicitFilePath) {
    const auto dir = lft::test::temp_test_dir("lft_explicit_path_test");
    const auto input = dir / "source.bin";
    const auto output = dir / "exact_target.bin";
    ASSERT_TRUE(lft::test::write_file(input, "explicit output path"));
    EXPECT_TRUE(lft::test::run_file_transfer(lft::test::next_port(), input, output));
    EXPECT_TRUE(fs::exists(output));
}

TEST(QuicFileOutput, TraversalFilenameIsSanitized) {
    const auto dir = lft::test::temp_test_dir("lft_traversal_test");
    const auto input = dir / "local.txt";
    const auto out_dir = dir / "incoming";
    fs::create_directories(out_dir);
    ASSERT_TRUE(lft::test::write_file(input, "safe bytes"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(out_dir.string());
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

TEST(QuicFileOutput, UnicodeFilename) {
    const auto dir = lft::test::temp_test_dir("lft_unicode_test");
    const auto input = dir / "café_测试.bin";
    const auto out_dir = dir / "incoming";
    fs::create_directories(out_dir);
    ASSERT_TRUE(lft::test::write_file(input, "unicode name"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(out_dir.string());
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(client.send_file(input.string(), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    const auto output = out_dir / "café_测试.bin";
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(lft::test::hashes_match(input, output));
}

TEST(QuicFileFailure, HashMismatch) {
    const auto dir = lft::test::temp_test_dir("lft_hash_mismatch_test");
    const auto input = dir / "good.bin";
    const auto output = dir / "bad_out.bin";
    ASSERT_TRUE(lft::test::write_file(input, "hash mismatch payload"));

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string());
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
    session.start_receive(output.string());
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
    session.start_receive(output.string());
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
    session.start_receive(output.string());
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    ASSERT_TRUE(lft::test::QuicTestAccess::SendRawStream(
        client, std::string(lft::kMaxHeaderBytes + 1, 'G'), true, 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "header too large or malformed");
    EXPECT_FALSE(fs::exists(output));
}

TEST(QuicFileFailure, UnwritableOutputAutoAcceptFails) {
    const auto dir = lft::test::temp_test_dir("lft_unwritable_auto_test");
    const auto input = dir / "payload.bin";
    const auto ro_dir = dir / "readonly";
    fs::create_directories(ro_dir);
    ASSERT_TRUE(lft::test::write_file(input, "cannot write here"));

    const auto output = ro_dir / "blocked.bin";
    lft::test::ReadOnlyPath guard(ro_dir);

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string());
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file(input.string(), 5000));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_EQ(session.result.error, "failed to open output file");
    EXPECT_FALSE(fs::exists(output));
}

TEST(QuicFileFailure, UnwritableOutputRejectsWithPrompt) {
    const auto dir = lft::test::temp_test_dir("lft_unwritable_prompt_test");
    const auto input = dir / "payload.bin";
    const auto ro_dir = dir / "readonly";
    fs::create_directories(ro_dir);
    ASSERT_TRUE(lft::test::write_file(input, "cannot write here"));

    const auto output = ro_dir / "blocked.bin";
    lft::test::ReadOnlyPath guard(ro_dir);

    const uint16_t port = lft::test::next_port();
    lft::test::ServerSession session(port);
    session.start_receive(output.string(), nullptr, [](const lft::FileTransferHeader&) {
        return true;
    });
    ASSERT_TRUE(session.wait_ready());

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(lft::test::kLoopbackHost, port));
    EXPECT_FALSE(client.send_file(input.string(), 5000));
    EXPECT_TRUE(client.was_rejected());
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_FALSE(session.receive_ok);
    EXPECT_FALSE(fs::exists(output));
}
