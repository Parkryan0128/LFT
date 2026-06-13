// Milestone 1 Step 5: QUIC loopback echo.
//
// Flow:
//   1. Server thread: start(), wait_for_echo("hello", ...), stop()
//   2. Main thread:   connect(), send_echo("hello", ...), assert reply == "hello"
#include "transfer/quic_client.h"
#include "transfer/quic_server.h"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

constexpr uint16_t kTestPort = 14434;
constexpr const char* kTestHost = "127.0.0.1";
constexpr const char* kTestMessage = "hello";

}  // namespace

int main() {
    std::cout << "=== QUIC Echo Loopback Test ===\n";

    lft::QuicServer server(kTestPort);
    std::string server_reply;

    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool server_ready = false;
    bool server_failed = false;

    std::thread server_thread([&]() {
        if (!server.start(kTestHost)) {
            std::lock_guard lock(sync_mutex);
            server_ready = true;
            server_failed = true;
            sync_cv.notify_all();
            return;
        }

        if (!server.wait_for_echo(
                kTestMessage,
                server_reply,
                /*timeout_ms=*/5000,
                [&] {
                    // Tell the client it is safe to connect and send.
                    std::lock_guard lock(sync_mutex);
                    server_ready = true;
                    sync_cv.notify_all();
                })) {
            std::cerr << "server: wait_for_echo failed\n";
        }
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
    std::string client_reply;

    if (!client.connect(kTestHost, kTestPort)) {
        std::cerr << "client: connect failed\n";
        server_thread.join();
        return 1;
    }

    if (!client.send_echo(kTestMessage, client_reply, /*timeout_ms=*/5000)) {
        std::cerr << "client: send_echo failed\n";
        client.disconnect();
        server_thread.join();
        return 1;
    }

    client.disconnect();
    server_thread.join();

    if (client_reply != kTestMessage) {
        std::cerr << "echo mismatch: expected \"" << kTestMessage
                  << "\", got \"" << client_reply << "\"\n";
        return 1;
    }

    std::cout << "Quic echo OK\n";
    return 0;
}
