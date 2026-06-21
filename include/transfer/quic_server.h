#pragma once

#include <msquic.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "transfer/quic_transfer.h"

namespace lft {

// QUIC server: listens for QUIC connections and receives file streams.
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

    // Block until a client sends one file on a stream. Writes to output_path
    // and verifies SHA-256. Returns false on timeout or hash mismatch.
    //
    // on_offer (optional): called on this thread once the file header arrives,
    // BEFORE any bytes are written. Return true to accept the transfer or false
    // to reject it. If null, the transfer is auto-accepted.
    // on_progress (optional): called as bytes are written to disk.
    bool receive_file(const std::string& output_path,
                      int timeout_ms,
                      std::function<void()> on_armed = nullptr,
                      std::function<bool(const FileTransferHeader&)> on_offer = nullptr,
                      ProgressFn on_progress = nullptr);

    // Result of the most recent receive_file() call.
    const FileReceiveResult& last_file_result() const { return file_result_; }

private:
    // TLS + ALPN configuration (must succeed before ListenerOpen).
    bool open_configuration();

    // Bind the QUIC listener on bind_host_:port_.
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

    // Wake receive_file() once the file is saved and verified.
    void notify_file_waiter(bool success);

    void try_parse_file_header();
    void append_file_bytes(const uint8_t* data, uint32_t length);
    void finish_file_receive(HQUIC stream);
    bool send_file_ack(HQUIC stream, bool ok);
    bool open_output_file();  // opens file_out_ at file_output_path_

    uint16_t port_;
    std::string bind_host_;
    bool running_ = false;

    // msquic API table and process registration.
    const QUIC_API_TABLE* api_ = nullptr;
    HQUIC registration_ = nullptr;

    // QUIC settings for this server (TLS cert + ALPN "lft").
    HQUIC configuration_ = nullptr;
    std::string alpn_ = "lft";
    std::string cert_path_;
    std::string key_path_;
    QUIC_CERTIFICATE_FILE cert_files_{};
    QUIC_CREDENTIAL_CONFIG cred_config_{};

    // QUIC listener on bind_host_:port_. Fires listener_callback when a client
    // attempts to connect.
    HQUIC listener_ = nullptr;

    // The accepted client connection. msquic gives us this handle in
    // NEW_CONNECTION and we must close it, otherwise RegistrationClose() in
    // stop() blocks forever waiting for it to be cleaned up. Guarded by
    // conn_mutex_ because the shutdown callback runs on an msquic worker thread.
    std::mutex conn_mutex_;
    std::condition_variable conn_cv_;  // notified when client_connection_ clears
    HQUIC client_connection_ = nullptr;

    // Bytes accumulated from the client's stream before PEER_SEND_SHUTDOWN.
    std::string stream_receive_buffer_;

    // Set by receive_file(); stream I/O is handled only while true.
    std::atomic<bool> receiving_file_{false};

    // receive_file() state.
    std::mutex file_mutex_;
    std::condition_variable file_cv_;
    bool file_done_ = false;
    bool file_ok_ = false;
    // Accept/reject handshake. The header arrives, then either the callback
    // thread auto-accepts (no prompt) or receive_file()'s thread prompts the
    // user and accepts/rejects. Body bytes only flow after ACCEPT is sent.
    std::function<bool(const FileTransferHeader&)> file_offer_;
    ProgressFn file_progress_;             // optional receive-progress callback
    bool offer_ready_ = false;             // header parsed, awaiting decision
    std::atomic<bool> file_accepted_{false};
    bool sending_final_ack_ = false;       // distinguishes the OK/FAIL send
    HQUIC file_stream_ = nullptr;          // active file stream (for ACCEPT send)
    // Where to write. If file_output_is_dir_ is true, file_output_dir_ holds the
    // target directory and the final file_output_path_ is computed from the
    // sender's (sanitized) filename once the header arrives.
    bool file_output_is_dir_ = false;
    std::string file_output_dir_;
    std::string file_output_path_;
    FileTransferHeader file_header_;
    bool file_header_parsed_ = false;
    bool file_io_error_ = false;   // could not open/write the output file
    bool file_overflow_ = false;   // peer sent more bytes than declared
    uint64_t file_bytes_written_ = 0;
    std::unique_ptr<std::ofstream> file_out_;
    FileReceiveResult file_result_;
};

}  // namespace lft
