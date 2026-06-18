#pragma once

#include <msquic.h>

#include <atomic>
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

    // Open a stream, send file header, wait for the receiver's ACCEPT/REJECT,
    // then (if accepted) send bytes and wait for the OK/FAIL ack.
    bool send_file(const std::string& file_path, int timeout_ms);

    // True if the most recent send_file() failed because the receiver rejected
    // the transfer (as opposed to a connection/IO error).
    bool was_rejected() const { return rejected_; }

private:
    enum class StreamMode { Echo, File };
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

    QUIC_STATUS on_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);

    static QUIC_STATUS stream_callback(HQUIC stream,
                                       void* context,
                                       QUIC_STREAM_EVENT* event);

    // Wake send_echo() once the server's reply arrives.
    void notify_echo_waiter(bool success);

    // Wake send_file() once a chunk send completes or ack arrives.
    void notify_send_waiter();
    void notify_file_waiter(bool success);

    // Record the receiver's accept/reject decision and wake send_file().
    void resolve_decision(bool accepted);

    // Open + start a fresh bidirectional stream into stream_.
    bool open_stream();

    bool stream_send_and_wait(HQUIC stream,
                              const void* data,
                              size_t length,
                              bool fin,
                              int timeout_ms);

    // Abort the current stream (used on a send failure so the peer is notified
    // instead of waiting for a timeout).
    void abort_stream();

    std::string host_;
    uint16_t port_ = 0;
    // Written on the msquic worker thread (connection callback), read on the
    // app thread (connect/send/disconnect), so it must be atomic.
    std::atomic<bool> connected_{false};

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

    // Step 5: bidirectional stream for echo.
    HQUIC stream_ = nullptr;
    // Read on the msquic worker thread (stream callback); set before each send.
    std::atomic<StreamMode> stream_mode_{StreamMode::Echo};

    // send_echo() blocks on this until the reply is received.
    std::mutex echo_mutex_;
    std::condition_variable echo_cv_;
    bool echo_done_ = false;
    bool echo_ok_ = false;
    std::string echo_reply_;

    // send_file() waits for chunk sends and the final server ack.
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    bool send_done_ = false;

    std::mutex file_mutex_;
    std::condition_variable file_cv_;
    bool file_done_ = false;
    bool file_ok_ = false;
    std::string file_ack_;

    // Accept/reject phase: after the header is sent, the receiver replies
    // "ACCEPT\n" or "REJECT\n" before any body bytes are sent.
    std::atomic<bool> awaiting_decision_{false};
    std::mutex decision_mutex_;
    std::condition_variable decision_cv_;
    bool decision_ready_ = false;
    bool decision_accepted_ = false;
    std::string decision_buf_;
    bool rejected_ = false;  // set when the most recent send_file was rejected
};

}  // namespace lft
