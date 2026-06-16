#include "transfer/quic_server.h"

#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include "quic_send_buffer.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>

#ifndef LFT_CERT_FILE
#define LFT_CERT_FILE "certs/lft-cert.pem"
#endif
#ifndef LFT_KEY_FILE
#define LFT_KEY_FILE "certs/lft-key.pem"
#endif

namespace lft {

QuicServer::QuicServer(uint16_t port)
    : port_(port) {}

QuicServer::~QuicServer() {
    stop();
}

bool QuicServer::open_configuration() {
    cert_path_ = LFT_CERT_FILE;
    key_path_ = LFT_KEY_FILE;

    if (!std::filesystem::exists(cert_path_) || !std::filesystem::exists(key_path_)) {
        std::cerr << "QuicServer::open_configuration: missing cert files\n"
                  << "  cert: " << cert_path_ << '\n'
                  << "  key:  " << key_path_ << '\n'
                  << "  run:  scripts/generate_dev_certs.sh\n";
        return false;
    }

    // ALPN tells client and server which application protocol runs on this QUIC connection.
    QUIC_SETTINGS settings{};
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    // Allow the client to open one bidirectional stream (required for echo).
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;

    QUIC_BUFFER alpn_buffer{
        .Length = static_cast<uint32_t>(alpn_.size()),
        .Buffer = reinterpret_cast<uint8_t*>(alpn_.data()),
    };

    if (QUIC_FAILED(api_->ConfigurationOpen(
            registration_,
            &alpn_buffer,
            1,
            &settings,
            sizeof(settings),
            nullptr,
            &configuration_))) {
        std::cerr << "QuicServer::open_configuration: ConfigurationOpen failed\n";
        configuration_ = nullptr;
        return false;
    }

    cert_files_.CertificateFile = cert_path_.c_str();
    cert_files_.PrivateKeyFile = key_path_.c_str();

    cred_config_ = {};
    cred_config_.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    cred_config_.Flags = QUIC_CREDENTIAL_FLAG_NONE;  // server credentials
    cred_config_.CertificateFile = &cert_files_;

    if (QUIC_FAILED(api_->ConfigurationLoadCredential(configuration_, &cred_config_))) {
        std::cerr << "QuicServer::open_configuration: ConfigurationLoadCredential failed\n";
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
        return false;
    }

    return true;
}

// simply calls on_connection_event with the connection and event. msquic speaks C, so we need to wrap the method in a static function.
QUIC_STATUS QuicServer::connection_callback(HQUIC connection,
                                            void* context,
                                            QUIC_CONNECTION_EVENT* event) {
    return static_cast<QuicServer*>(context)->on_connection_event(connection, event);
}

QUIC_STATUS QuicServer::stream_callback(HQUIC stream,
                                        void* context,
                                        QUIC_STREAM_EVENT* event) {
    return static_cast<QuicServer*>(context)->on_stream_event(stream, event);
}

void QuicServer::notify_echo_waiter(bool success) {
    {
        std::lock_guard lock(echo_mutex_);
        echo_done_ = true;
        echo_ok_ = success;
    }
    echo_cv_.notify_one();
}

void QuicServer::notify_file_waiter(bool success) {
    {
        std::lock_guard lock(file_mutex_);
        file_done_ = true;
        file_ok_ = success;
        file_result_.success = success;
    }
    file_cv_.notify_one();
}

// Handles the connection event.
QUIC_STATUS QuicServer::on_connection_event(HQUIC connection,
                                            QUIC_CONNECTION_EVENT* event) {
    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            // TLS handshake finished; client is fully connected.
            {
                std::lock_guard lock(conn_mutex_);
                client_connection_ = connection;
            }
            std::cout << "QuicServer: client connected\n";
            return QUIC_STATUS_SUCCESS;

        // Shutdown finished; msquic says it is safe to close the handle. We own
        // this connection, so close it here (unless we already requested the
        // close ourselves) and forget it.
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
                api_->ConnectionClose(connection);
            }
            {
                std::lock_guard lock(conn_mutex_);
                if (client_connection_ == connection) {
                    client_connection_ = nullptr;
                }
            }
            // Wake receive_file()/wait_for_echo() if they are draining the
            // connection close (confirms our ack reached the client).
            conn_cv_.notify_all();
            return QUIC_STATUS_SUCCESS;

        // The client opened a bidirectional stream — attach our stream callback.
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            stream_receive_buffer_.clear();
            file_header_parsed_ = false;
            file_io_error_ = false;
            file_overflow_ = false;
            file_bytes_written_ = 0;
            file_out_.reset();
            api_->SetCallbackHandler(
                event->PEER_STREAM_STARTED.Stream,
                reinterpret_cast<void*>(stream_callback),
                this);
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

QUIC_STATUS QuicServer::on_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event) {
    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER& buf = event->RECEIVE.Buffers[i];
                if (stream_mode_ == StreamMode::File) {
                    append_file_bytes(buf.Buffer, buf.Length);
                } else {
                    stream_receive_buffer_.append(
                        reinterpret_cast<const char*>(buf.Buffer),
                        buf.Length);
                }
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            if (stream_mode_ == StreamMode::File) {
                finish_file_receive(stream);
            } else {
                const bool matches = (stream_receive_buffer_ == expected_message_);
                if (echo_out_reply_ != nullptr) {
                    *echo_out_reply_ = stream_receive_buffer_;
                }

                if (!matches) {
                    std::cerr << "QuicServer: unexpected message (expected \""
                              << expected_message_ << "\", got \""
                              << stream_receive_buffer_ << "\")\n";
                    notify_echo_waiter(false);
                    return QUIC_STATUS_SUCCESS;
                }

                // Echo the same bytes back, with FIN to close our send side.
                if (QUIC_FAILED(stream_send_copy(
                        api_,
                        stream,
                        stream_receive_buffer_.data(),
                        stream_receive_buffer_.size(),
                        QUIC_SEND_FLAG_FIN))) {
                    std::cerr << "QuicServer: StreamSend echo failed\n";
                    notify_echo_waiter(false);
                }
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            std::free(event->SEND_COMPLETE.ClientContext);
            if (stream_mode_ == StreamMode::File) {
                notify_file_waiter(file_result_.success);
            } else {
                notify_echo_waiter(true);
            }
            return QUIC_STATUS_SUCCESS;

        // Peer abandoned the transfer (e.g. client hit an error mid-send).
        // Fail fast instead of letting the waiter block until timeout.
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            if (stream_mode_ == StreamMode::File) {
                if (file_out_ != nullptr && file_out_->is_open()) {
                    file_out_->close();
                }
                if (file_result_.error.empty()) {
                    file_result_.error = "peer aborted the transfer";
                }
                notify_file_waiter(false);
            } else {
                notify_echo_waiter(false);
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
                api_->StreamClose(stream);
            }
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

void QuicServer::try_parse_file_header() {
    if (file_header_parsed_) {
        return;
    }

    size_t header_bytes = 0;
    FileTransferHeader header;
    if (!decode_file_header(stream_receive_buffer_, header, header_bytes)) {
        // Not enough data yet — but guard against a peer that never sends the
        // terminating blank line by capping how much we will buffer.
        if (stream_receive_buffer_.size() > kMaxHeaderBytes) {
            file_io_error_ = true;
            file_result_.error = "header too large or malformed";
            file_header_parsed_ = true;  // stop buffering further bytes
            stream_receive_buffer_.clear();
        }
        return;
    }

    file_header_ = header;
    file_header_parsed_ = true;

    // For a directory target, the final path is the dir + the sender's
    // (already sanitized) filename, so traversal can't escape the directory.
    if (file_output_is_dir_) {
        file_output_path_ =
            (std::filesystem::path(file_output_dir_) / file_header_.name).string();
    }

    file_out_ = std::make_unique<std::ofstream>(
        file_output_path_,
        std::ios::binary | std::ios::trunc);
    if (!file_out_->is_open()) {
        file_io_error_ = true;
        file_result_.error = "failed to open output file";
        file_out_.reset();
        stream_receive_buffer_.clear();
        return;
    }

    const std::string_view body = std::string_view(stream_receive_buffer_).substr(header_bytes);
    if (!body.empty()) {
        append_file_bytes(reinterpret_cast<const uint8_t*>(body.data()),
                          static_cast<uint32_t>(body.size()));
    }

    stream_receive_buffer_.clear();
}

void QuicServer::append_file_bytes(const uint8_t* data, uint32_t length) {
    if (!file_header_parsed_) {
        stream_receive_buffer_.append(
            reinterpret_cast<const char*>(data),
            length);
        try_parse_file_header();
        return;
    }

    // Header failed (e.g. could not open output) — drop the rest of the stream.
    if (file_io_error_ || file_out_ == nullptr || !file_out_->is_open()) {
        return;
    }

    if (length == 0) {
        return;
    }

    // Never write more than the declared size; flag the overflow instead.
    const uint64_t remaining = file_header_.size - file_bytes_written_;
    uint64_t to_write = length;
    if (to_write > remaining) {
        to_write = remaining;
        file_overflow_ = true;
    }

    if (to_write > 0) {
        file_out_->write(reinterpret_cast<const char*>(data),
                         static_cast<std::streamsize>(to_write));
        if (!file_out_->good()) {
            file_io_error_ = true;
            file_result_.error = "failed while writing output file";
            return;
        }
        file_bytes_written_ += to_write;
    }
}

bool QuicServer::send_file_ack(HQUIC stream, bool ok) {
    const char* msg = ok ? "OK\n" : "FAIL\n";
    return QUIC_SUCCEEDED(stream_send_copy(
        api_, stream, msg, std::strlen(msg), QUIC_SEND_FLAG_FIN));
}

void QuicServer::finish_file_receive(HQUIC stream) {
    if (file_out_ != nullptr && file_out_->is_open()) {
        file_out_->close();
    }

    file_result_.path = file_output_path_;
    file_result_.bytes_received = file_bytes_written_;
    file_result_.expected_hash = file_header_.sha256_hex;

    bool verify_ok = true;

    if (file_io_error_) {
        // error string already set where the failure happened
        if (file_result_.error.empty()) {
            file_result_.error = "output I/O error";
        }
        verify_ok = false;
    } else if (!file_header_parsed_ || file_out_ == nullptr) {
        file_result_.error = "file header not received";
        verify_ok = false;
    } else if (file_overflow_) {
        file_result_.error = "peer sent more bytes than declared";
        verify_ok = false;
    } else if (file_bytes_written_ != file_header_.size) {
        file_result_.error = "size mismatch";
        verify_ok = false;
    } else if (!sha256_file(file_output_path_, file_result_.computed_hash)) {
        file_result_.error = "failed to hash received file";
        verify_ok = false;
    } else if (file_result_.computed_hash != file_header_.sha256_hex) {
        file_result_.error = "hash mismatch";
        verify_ok = false;
    }

    if (verify_ok) {
        std::cout << "QuicServer: received file \"" << file_header_.name
                  << "\" (" << file_bytes_written_ << " bytes, hash OK)\n";
    } else {
        std::cerr << "QuicServer: file receive failed: " << file_result_.error << '\n';
    }

    file_result_.success = verify_ok;

    if (!send_file_ack(stream, verify_ok)) {
        notify_file_waiter(false);
    }
    // On success, notify_file_waiter runs from SEND_COMPLETE after ack is sent.
}

// Simply calls on_listener_event with the listener and event. msquic speaks C, so we need to wrap the method in a static function.
QUIC_STATUS QuicServer::listener_callback(HQUIC listener,
                                          void* context,
                                          QUIC_LISTENER_EVENT* event) {
    return static_cast<QuicServer*>(context)->on_listener_event(listener, event);
}

// Returns QUIC_STATUS from handling the event (e.g. success/failure of ConnectionSetConfiguration).
QUIC_STATUS QuicServer::on_listener_event(HQUIC listener, QUIC_LISTENER_EVENT* event) {
    (void)listener;
    switch (event->Type) {
        // Client started a QUIC handshake — not fully connected yet.
        case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            api_->SetCallbackHandler(
                event->NEW_CONNECTION.Connection,
                reinterpret_cast<void*>(connection_callback),
                this);
            // Tell msquic to continue the handshake using our TLS cert + ALPN "lft".
            return api_->ConnectionSetConfiguration(
                event->NEW_CONNECTION.Connection,
                configuration_);

        case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

// create listener object and bind to host:port, so msquic can start accepting QUIC handshakes.
bool QuicServer::open_listener() {
    // Create listener_ and register listener_callback. Does NOT open the port yet.
    if (QUIC_FAILED(api_->ListenerOpen(
            registration_,
            listener_callback,
            this,       // passed back as `context` in listener_callback
            &listener_))) {
        std::cerr << "QuicServer::open_listener: ListenerOpen failed\n";
        listener_ = nullptr;
        return false;
    }

    // ALPN must match what the client will offer (also "lft").
    QUIC_BUFFER alpn_buffer{
        .Length = static_cast<uint32_t>(alpn_.size()),
        .Buffer = reinterpret_cast<uint8_t*>(alpn_.data()),
    };

    // Build the local address: e.g. 127.0.0.1:4433
    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_INET);
    if (!QuicAddr4FromString(bind_host_.c_str(), &address)) {
        std::cerr << "QuicServer::open_listener: invalid bind host " << bind_host_ << '\n';
        api_->ListenerClose(listener_);
        listener_ = nullptr;
        return false;
    }
    QuicAddrSetPort(&address, port_);

    // Actually bind the UDP socket and start accepting QUIC handshakes.
    if (QUIC_FAILED(api_->ListenerStart(listener_, &alpn_buffer, 1, &address))) {
        std::cerr << "QuicServer::open_listener: ListenerStart failed on "
                  << bind_host_ << ':' << port_ << '\n';
        api_->ListenerClose(listener_);
        listener_ = nullptr;
        return false;
    }

    std::cout << "QuicServer: listening on " << bind_host_ << ':' << port_ << '\n';
    return true;
}

bool QuicServer::start(std::string_view host) {
    if (running_) {
        return true;
    }

    bind_host_ = std::string(host);

    // Step 1a: Load the msquic function table (MsQuicOpen2).
    if (QUIC_FAILED(MsQuicOpen2(&api_))) {
        std::cerr << "QuicServer::start: MsQuicOpen2 failed\n";
        api_ = nullptr;
        return false;
    }

    // Step 1b: Register this process as an msquic application ("LFT").
    const QUIC_REGISTRATION_CONFIG reg_config{
        .AppName = "LFT",
        .ExecutionProfile = QUIC_EXECUTION_PROFILE_LOW_LATENCY,
    };

    if (QUIC_FAILED(api_->RegistrationOpen(&reg_config, &registration_))) {
        std::cerr << "QuicServer::start: RegistrationOpen failed\n";
        registration_ = nullptr;
        stop();  // releases api_
        return false;
    }

    // Step 2: TLS credentials + ALPN (required before ListenerOpen).
    if (!open_configuration()) {
        stop();  // releases registration_ + api_
        return false;
    }

    // Step 3: Bind to bind_host_:port_ and accept connections.
    if (!open_listener()) {
        stop();  // releases configuration_ + registration_ + api_
        return false;
    }

    running_ = true;
    return true;
}

void QuicServer::stop() {
    // If a client is still connected, ask msquic to shut the connection down.
    // The SHUTDOWN_COMPLETE callback then closes the handle, which lets
    // RegistrationClose() below return instead of blocking forever.
    if (api_ != nullptr) {
        HQUIC conn = nullptr;
        {
            std::lock_guard lock(conn_mutex_);
            conn = client_connection_;
        }
        if (conn != nullptr) {
            api_->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        }
    }

    // Tear down in reverse order of start (inner objects first).
    if (api_ != nullptr && listener_ != nullptr) {
        api_->ListenerStop(listener_);   // stop accepting new clients
        api_->ListenerClose(listener_);    // free listener handle
        listener_ = nullptr;
    }

    if (api_ != nullptr && configuration_ != nullptr) {
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
    }

    if (api_ != nullptr && registration_ != nullptr) {
        api_->RegistrationClose(registration_);
        registration_ = nullptr;
    }

    if (api_ != nullptr) {
        MsQuicClose(api_);
        api_ = nullptr;
    }

    running_ = false;
}

bool QuicServer::is_running() const {
    return running_;
}

bool QuicServer::wait_for_echo(std::string_view expected_message,
                               std::string& out_reply,
                               int timeout_ms,
                               std::function<void()> on_armed) {
    {
        std::lock_guard lock(echo_mutex_);
        stream_mode_ = StreamMode::Echo;
        expected_message_ = std::string(expected_message);
        echo_out_reply_ = &out_reply;
        echo_done_ = false;
        echo_ok_ = false;
        stream_receive_buffer_.clear();
    }

    // Safe for the client to send now — expected message is armed.
    if (on_armed) {
        on_armed();
    }

    std::unique_lock lock(echo_mutex_);
    const bool finished = echo_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return echo_done_; });

    echo_out_reply_ = nullptr;

    if (!finished || !echo_ok_) {
        std::cerr << "QuicServer::wait_for_echo: timed out or echo failed\n";
        return false;
    }

    std::cout << "QuicServer: echoed \"" << out_reply << "\"\n";
    return true;
}

bool QuicServer::receive_file(const std::string& output_path,
                              int timeout_ms,
                              std::function<void()> on_armed) {
    std::error_code ec;

    // Decide whether output_path names a directory (save under the sender's
    // filename) or a full file path (save exactly there). A trailing separator
    // or an existing directory means "directory".
    const bool looks_like_dir =
        output_path.empty() ||
        output_path.back() == '/' ||
        output_path.back() == '\\' ||
        std::filesystem::is_directory(output_path, ec);

    if (looks_like_dir) {
        std::filesystem::create_directories(output_path, ec);
    } else {
        // Make sure the parent directory exists before bytes arrive.
        const auto parent = std::filesystem::path(output_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
    }

    {
        std::lock_guard lock(file_mutex_);
        stream_mode_ = StreamMode::File;
        file_output_is_dir_ = looks_like_dir;
        file_output_dir_ = looks_like_dir ? output_path : std::string();
        file_output_path_ = looks_like_dir ? std::string() : output_path;
        file_header_parsed_ = false;
        file_io_error_ = false;
        file_overflow_ = false;
        file_bytes_written_ = 0;
        file_done_ = false;
        file_ok_ = false;
        file_result_ = {};
        stream_receive_buffer_.clear();
        file_out_.reset();
    }

    if (on_armed) {
        on_armed();
    }

    std::unique_lock lock(file_mutex_);
    const bool finished = file_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return file_done_; });

    if (!finished || !file_ok_) {
        if (file_out_ != nullptr && file_out_->is_open()) {
            file_out_->close();
        }
        std::cerr << "QuicServer::receive_file: timed out or transfer failed\n";
        return false;
    }
    lock.unlock();

    // The "OK" ack was only queued (SEND_COMPLETE = locally accepted, not yet
    // delivered). Wait for the client to close the connection — which it does
    // right after reading the ack — before we let the caller tear msquic down,
    // otherwise the ack can be lost. Bounded so a vanished peer can't hang us.
    {
        std::unique_lock conn_lock(conn_mutex_);
        conn_cv_.wait_for(
            conn_lock,
            std::chrono::seconds(2),
            [this] { return client_connection_ == nullptr; });
    }

    return true;
}

}  // namespace lft
