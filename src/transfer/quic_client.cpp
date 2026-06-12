#include "transfer/quic_client.h"

#include <chrono>
#include <iostream>

namespace lft {

namespace {

constexpr int kConnectTimeoutMs = 5000;
constexpr int kShutdownTimeoutMs = 3000;

}  // namespace

QuicClient::QuicClient() = default;

QuicClient::~QuicClient() {
    disconnect();
}

// Wake connect() with the handshake result.
void QuicClient::notify_connect_waiter(bool success) {
    {
        std::lock_guard lock(connect_mutex_);
        connect_done_ = true;
        connect_ok_ = success;
    }
    connect_cv_.notify_one();
}

// msquic speaks C, so this static wrapper forwards to the member method.
QUIC_STATUS QuicClient::connection_callback(HQUIC connection,
                                            void* context,
                                            QUIC_CONNECTION_EVENT* event) {
    return static_cast<QuicClient*>(context)->on_connection_event(connection, event);
}

QUIC_STATUS QuicClient::on_connection_event(HQUIC connection,
                                            QUIC_CONNECTION_EVENT* event) {
    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            // TLS handshake finished; we are fully connected.
            connected_ = true;
            std::cout << "QuicClient: connected to " << host_ << ':' << port_ << '\n';
            notify_connect_waiter(true);
            return QUIC_STATUS_SUCCESS;

        // The transport (e.g. timeout, unreachable, TLS error) tore us down.
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            if (!connected_) {
                std::cerr << "QuicClient: connect failed (transport shutdown, status="
                          << event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status << ")\n";
                notify_connect_waiter(false);
            }
            return QUIC_STATUS_SUCCESS;

        // The peer explicitly refused/closed the connection.
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            if (!connected_) {
                std::cerr << "QuicClient: connect failed (peer shutdown)\n";
                notify_connect_waiter(false);
            }
            return QUIC_STATUS_SUCCESS;

        // Shutdown finished; msquic says it is safe to close the handle now.
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            connected_ = false;
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
                api_->ConnectionClose(connection);
            }
            connection_ = nullptr;
            {
                std::lock_guard lock(shutdown_mutex_);
                shutdown_complete_ = true;
            }
            shutdown_cv_.notify_one();
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

// Step 2: client configuration — ALPN "lft" plus credentials that carry no
// certificate and skip server-cert validation (dev only, matches the server's
// self-signed cert).
bool QuicClient::open_configuration() {
    QUIC_SETTINGS settings{};
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;

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
        std::cerr << "QuicClient::open_configuration: ConfigurationOpen failed\n";
        configuration_ = nullptr;
        return false;
    }

    cred_config_ = {};
    cred_config_.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred_config_.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
                         QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    if (QUIC_FAILED(api_->ConfigurationLoadCredential(configuration_, &cred_config_))) {
        std::cerr << "QuicClient::open_configuration: ConfigurationLoadCredential failed\n";
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
        return false;
    }

    return true;
}

bool QuicClient::connect(std::string_view host, uint16_t port) {
    if (connected_) {
        return true;
    }

    host_ = std::string(host);
    port_ = port;
    connect_done_ = false;
    connect_ok_ = false;
    shutdown_complete_ = false;

    // Step 1a: Load the msquic function table (MsQuicOpen2).
    if (QUIC_FAILED(MsQuicOpen2(&api_))) {
        std::cerr << "QuicClient::connect: MsQuicOpen2 failed\n";
        api_ = nullptr;
        return false;
    }

    // Step 1b: Register this client as an msquic application ("LFT").
    const QUIC_REGISTRATION_CONFIG reg_config{
        .AppName = "LFT",
        .ExecutionProfile = QUIC_EXECUTION_PROFILE_LOW_LATENCY,
    };

    if (QUIC_FAILED(api_->RegistrationOpen(&reg_config, &registration_))) {
        std::cerr << "QuicClient::connect: RegistrationOpen failed\n";
        MsQuicClose(api_);
        api_ = nullptr;
        registration_ = nullptr;
        return false;
    }

    // Step 2: ALPN + client credentials.
    if (!open_configuration()) {
        api_->RegistrationClose(registration_);
        registration_ = nullptr;
        MsQuicClose(api_);
        api_ = nullptr;
        return false;
    }

    // Step 3: Allocate the connection object and register our callback.
    if (QUIC_FAILED(api_->ConnectionOpen(
            registration_,
            connection_callback,
            this,
            &connection_))) {
        std::cerr << "QuicClient::connect: ConnectionOpen failed\n";
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
        api_->RegistrationClose(registration_);
        registration_ = nullptr;
        MsQuicClose(api_);
        api_ = nullptr;
        return false;
    }

    // Step 4: Start the handshake toward host:port. This is async — results
    // arrive on the connection callback.
    if (QUIC_FAILED(api_->ConnectionStart(
            connection_,
            configuration_,
            QUIC_ADDRESS_FAMILY_UNSPEC,
            host_.c_str(),
            port_))) {
        std::cerr << "QuicClient::connect: ConnectionStart failed\n";
        disconnect();
        return false;
    }

    // Block until CONNECTED, a failure event, or timeout.
    {
        std::unique_lock lock(connect_mutex_);
        const bool finished = connect_cv_.wait_for(
            lock,
            std::chrono::milliseconds(kConnectTimeoutMs),
            [this] { return connect_done_; });

        if (!finished || !connect_ok_) {
            std::cerr << "QuicClient::connect: timed out or handshake failed\n";
            lock.unlock();
            disconnect();
            return false;
        }
    }

    return true;
}

void QuicClient::disconnect() {
    // Ask msquic to shut down the connection, then wait for SHUTDOWN_COMPLETE.
    // The handle is closed inside the callback (msquic's required pattern);
    // calling ConnectionClose directly here can block forever.
    if (api_ != nullptr && connection_ != nullptr) {
        shutdown_complete_ = false;
        const auto flags = connected_ ? QUIC_CONNECTION_SHUTDOWN_FLAG_NONE
                                      : QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT;
        api_->ConnectionShutdown(connection_, flags, 0);

        std::unique_lock lock(shutdown_mutex_);
        shutdown_cv_.wait_for(
            lock,
            std::chrono::milliseconds(kShutdownTimeoutMs),
            [this] { return shutdown_complete_; });
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

    connected_ = false;
    connect_done_ = false;
    connect_ok_ = false;
    shutdown_complete_ = false;
}

bool QuicClient::is_connected() const {
    return connected_;
}

bool QuicClient::send_echo(std::string_view message,
                           std::string& out_reply,
                           int timeout_ms) {
    (void)message;
    (void)out_reply;
    (void)timeout_ms;
    // TODO (Step 5): open bidi stream, send bytes, receive reply.
    return false;
}

}  // namespace lft
