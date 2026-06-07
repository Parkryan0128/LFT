// Milestone 1 — Step A: QUIC loopback echo (skeleton).
//
// Planned flow once implemented:
//   1. Server thread: QuicServer::start("127.0.0.1", 4433)
//   2. Server thread: wait_for_echo("hello", reply, timeout)
//   3. Main thread:   QuicClient::connect → send_echo("hello", reply, timeout)
//   4. Assert reply matches expected echo response.

#include "transfer/quic_client.h"
#include "transfer/quic_server.h"

#include <iostream>
#include <thread>

namespace {

constexpr uint16_t kTestPort = 4433;
constexpr const char* kTestHost = "127.0.0.1";
constexpr const char* kTestMessage = "hello";

}  // namespace

int main() {
    std::cout << "=== QUIC Echo Loopback Test (skeleton) ===\n";

    lft::QuicServer server(kTestPort);
    std::string server_reply;

    std::thread server_thread([&]() {
        if (!server.start(kTestHost)) {
            std::cerr << "server: start() not implemented\n";
            return;
        }
        if (!server.wait_for_echo(kTestMessage, server_reply, /*timeout_ms=*/5000)) {
            std::cerr << "server: wait_for_echo() not implemented\n";
        }
        server.stop();
    });

    lft::QuicClient client;
    std::string client_reply;

    if (!client.connect(kTestHost, kTestPort)) {
        std::cerr << "client: connect() not implemented\n";
        server_thread.join();
        return 1;
    }

    if (!client.send_echo(kTestMessage, client_reply, /*timeout_ms=*/5000)) {
        std::cerr << "client: send_echo() not implemented\n";
        client.disconnect();
        server_thread.join();
        return 1;
    }

    client.disconnect();
    server_thread.join();

  // TODO: compare client_reply with expected echo once protocol is defined.
    std::cout << "Test not yet implemented — stubs only.\n";
    return 1;
}
