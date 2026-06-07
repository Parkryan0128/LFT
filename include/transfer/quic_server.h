#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lft {

// QUIC server: listens for one connection and handles stream I/O.
// Step A: echo server that reads a message and sends a reply on the same stream.
class QuicServer {
public:
    explicit QuicServer(uint16_t port);
    ~QuicServer();

    QuicServer(const QuicServer&) = delete;
    QuicServer& operator=(const QuicServer&) = delete;

    // Open registration, load TLS credentials, bind to host:port.
    bool start(std::string_view host = "127.0.0.1");

    // Tear down listener and msquic resources.
    void stop();

    bool is_running() const;

    // Block until a client connects, sends one message, and we send a reply.
    // Returns false on timeout or error.
    bool wait_for_echo(std::string_view expected_message,
                       std::string& out_reply,
                       int timeout_ms);

private:
    uint16_t port_;
    bool running_ = false;

    // msquic handles (registration, configuration, listener) added in Step A.
};

}  // namespace lft
