#include "transfer/quic_server.h"

#include <cstring>
#include <filesystem>
#include <chrono>
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
            return QUIC_STATUS_SUCCESS;

        // The client opened a bidirectional stream — attach our stream callback.
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            stream_receive_buffer_.clear();
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
        // Client sent bytes — append each chunk to our buffer.
        case QUIC_STREAM_EVENT_RECEIVE:
            for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER& buf = event->RECEIVE.Buffers[i];
                stream_receive_buffer_.append(
                    reinterpret_cast<const char*>(buf.Buffer),
                    buf.Length);
            }
            return QUIC_STATUS_SUCCESS;

        // Client finished sending (FIN) — echo the message back on the same stream.
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
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

            // Build a send buffer: [QUIC_BUFFER header][payload bytes].
            const size_t payload_len = stream_receive_buffer_.size();
            auto* send_raw = static_cast<uint8_t*>(
                std::malloc(sizeof(QUIC_BUFFER) + payload_len));
            if (send_raw == nullptr) {
                std::cerr << "QuicServer: echo send buffer allocation failed\n";
                notify_echo_waiter(false);
                return QUIC_STATUS_SUCCESS;
            }

            auto* send_buf = reinterpret_cast<QUIC_BUFFER*>(send_raw);
            send_buf->Buffer = send_raw + sizeof(QUIC_BUFFER);
            send_buf->Length = static_cast<uint32_t>(payload_len);
            std::memcpy(send_buf->Buffer, stream_receive_buffer_.data(), payload_len);

            if (QUIC_FAILED(api_->StreamSend(
                    stream,
                    send_buf,
                    1,
                    QUIC_SEND_FLAG_FIN,
                    send_buf))) {
                std::cerr << "QuicServer: StreamSend echo failed\n";
                std::free(send_raw);
                notify_echo_waiter(false);
            }
            return QUIC_STATUS_SUCCESS;
        }

        // Echo send finished — free the buffer we passed to StreamSend.
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            std::free(event->SEND_COMPLETE.ClientContext);
            notify_echo_waiter(true);
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
        MsQuicClose(api_);
        api_ = nullptr;
        registration_ = nullptr;
        return false;
    }

    // Step 2: TLS credentials + ALPN (required before ListenerOpen).
    if (!open_configuration()) {
        api_->RegistrationClose(registration_);
        registration_ = nullptr;
        MsQuicClose(api_);
        api_ = nullptr;
        return false;
    }

    // Step 3: Bind to bind_host_:port_ and accept connections.
    if (!open_listener()) {
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
        api_->RegistrationClose(registration_);
        registration_ = nullptr;
        MsQuicClose(api_);
        api_ = nullptr;
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

}  // namespace lft
