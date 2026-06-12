#pragma once

#include <msquic.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
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
    // Blocks until the handshake completes or times out.
    bool connect(std::string_view host, uint16_t port);

    // Close connection and release msquic resources.
    void disconnect();

    bool is_connected() const;

    // Open a stream, send message, wait for reply.
    bool send_echo(std::string_view message,
                   std::string& out_reply,
                   int timeout_ms);

private:
    // Step 2: client TLS + ALPN configuration (no cert, skips validation).
    bool open_configuration();

    QUIC_STATUS on_connection_event(HQUIC connection, QUIC_CONNECTION_EVENT* event);

    // msquic needs a plain C function pointer; this static wrapper forwards to
    // on_connection_event. `context` is the QuicClient* passed to ConnectionOpen.
    static QUIC_STATUS connection_callback(HQUIC connection,
                                           void* context,
                                           QUIC_CONNECTION_EVENT* event);

    // Wake connect() once the handshake succeeds or fails.
    void notify_connect_waiter(bool success);

    std::string host_;
    uint16_t port_ = 0;
    bool connected_ = false;

    // Step 1: API + registration.
    const QUIC_API_TABLE* api_ = nullptr;
    HQUIC registration_ = nullptr;

    // Step 2: client config (ALPN "lft", no certificate).
    HQUIC configuration_ = nullptr;
    std::string alpn_ = "lft";
    QUIC_CREDENTIAL_CONFIG cred_config_{};

    // Step 4: the connection to the server.
    HQUIC connection_ = nullptr;

    // connect() blocks on this until CONNECTED or a shutdown event.
    std::mutex connect_mutex_;
    std::condition_variable connect_cv_;
    bool connect_done_ = false;
    bool connect_ok_ = false;

    // disconnect() blocks on this until SHUTDOWN_COMPLETE.
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    bool shutdown_complete_ = false;
};

}  // namespace lft
