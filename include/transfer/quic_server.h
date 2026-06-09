#pragma once

#include <msquic.h>

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
    // Step 2: TLS + ALPN configuration (must succeed before ListenerOpen).
    bool open_configuration();

    uint16_t port_;
    std::string bind_host_;
    bool running_ = false;

    // Step 1: API + registration.
    const QUIC_API_TABLE* api_ = nullptr;
    HQUIC registration_ = nullptr;

    // Step 2: QUIC settings for this server (TLS cert + ALPN "lft").
    HQUIC configuration_ = nullptr;
    std::string alpn_ = "lft";
    std::string cert_path_;
    std::string key_path_;
    QUIC_CERTIFICATE_FILE cert_files_{};
    QUIC_CREDENTIAL_CONFIG cred_config_{};
};

}  // namespace lft
