#pragma once

#include <msquic.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
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
    // on_armed (optional): called after expected message is set, before blocking.
    bool wait_for_echo(std::string_view expected_message,
                       std::string& out_reply,
                       int timeout_ms,
                       std::function<void()> on_armed = nullptr);

private:
    // Step 2: TLS + ALPN configuration (must succeed before ListenerOpen).
    bool open_configuration();

    // Step 3: Bind UDP port and accept incoming QUIC connections.
    bool open_listener();

    QUIC_STATUS on_listener_event(HQUIC listener, QUIC_LISTENER_EVENT* event);
    QUIC_STATUS on_connection_event(HQUIC connection, QUIC_CONNECTION_EVENT* event);
    QUIC_STATUS on_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);

    // msquic needs plain C function pointers; these static wrappers forward to
    // the methods above. `context` is the QuicServer* passed to ListenerOpen.
    static QUIC_STATUS listener_callback(HQUIC listener,
                                         void* context,
                                         QUIC_LISTENER_EVENT* event);

    static QUIC_STATUS connection_callback(HQUIC connection,
                                           void* context,
                                           QUIC_CONNECTION_EVENT* event);

    static QUIC_STATUS stream_callback(HQUIC stream,
                                       void* context,
                                       QUIC_STREAM_EVENT* event);

    // Wake wait_for_echo() once the echo exchange finishes.
    void notify_echo_waiter(bool success);

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

    // Step 3: UDP/QUIC listener on bind_host_:port_. Fires listener_callback
    // when a client attempts to connect.
    HQUIC listener_ = nullptr;

    // The accepted client connection. msquic gives us this handle in
    // NEW_CONNECTION and we must close it, otherwise RegistrationClose() in
    // stop() blocks forever waiting for it to be cleaned up. Guarded by
    // conn_mutex_ because the shutdown callback runs on an msquic worker thread.
    std::mutex conn_mutex_;
    HQUIC client_connection_ = nullptr;

    // wait_for_echo() blocks on this until the client message is echoed back.
    std::mutex echo_mutex_;
    std::condition_variable echo_cv_;
    bool echo_done_ = false;
    bool echo_ok_ = false;
    std::string expected_message_;
    std::string* echo_out_reply_ = nullptr;

    // Bytes accumulated from the client's stream before PEER_SEND_SHUTDOWN.
    std::string stream_receive_buffer_;
};

}  // namespace lft
