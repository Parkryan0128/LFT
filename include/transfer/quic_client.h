#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lft {

// QUIC client: connects to a server and sends data on a bidirectional stream.
class QuicClient {
public:
    QuicClient();
    ~QuicClient();

    QuicClient(const QuicClient&) = delete;
    QuicClient& operator=(const QuicClient&) = delete;

    // Open registration, load TLS credentials, connect to host:port.
    bool connect(std::string_view host, uint16_t port);

    // Close connection and release msquic resources.
    void disconnect();

    bool is_connected() const;

    // Open a stream, send message, wait for reply.
    bool send_echo(std::string_view message,
                   std::string& out_reply,
                   int timeout_ms);

private:
    bool connected_ = false;

    // msquic handles (registration, configuration, connection) added in Step A.
};

}  // namespace lft
