#include "transfer/quic_server.h"

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
    QUIC_BUFFER alpn_buffer{
        .Length = static_cast<uint32_t>(alpn_.size()),
        .Buffer = reinterpret_cast<uint8_t*>(alpn_.data()),
    };

    if (QUIC_FAILED(api_->ConfigurationOpen(
            registration_,
            &alpn_buffer,
            1,
            nullptr,
            0,
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

// =============================================================================
// Step 3 — Listener: bind a UDP port and accept incoming QUIC connections
// =============================================================================
//
// msquic is event-driven. We give it C callback functions; msquic calls them when
// something happens (new client, connection established, etc.).
//
// Flow when a client connects:
//   1. Client sends QUIC handshake to our UDP port
//   2. msquic fires QUIC_LISTENER_EVENT_NEW_CONNECTION on listener_callback
//   3. We attach connection_callback and apply configuration_ (TLS + ALPN)
//   4. Handshake completes → QUIC_CONNECTION_EVENT_CONNECTED on connection_callback
//
// Why two callbacks?
//   - listener_  → "someone knocked on the door"
//   - connection → "one specific client; handle their streams/messages later"

// msquic requires a plain C function pointer. This static wrapper forwards to
// the QuicServer instance passed as `context` when we called ListenerOpen.
QUIC_STATUS QuicServer::connection_callback(HQUIC connection,
                                            void* context,
                                            QUIC_CONNECTION_EVENT* event) {
    return static_cast<QuicServer*>(context)->on_connection_event(connection, event);
}

// Called for each event on an accepted client connection.
// Step 3 only logs CONNECTED; Step 5 will handle streams (send/receive echo).
QUIC_STATUS QuicServer::on_connection_event(HQUIC connection,
                                            QUIC_CONNECTION_EVENT* event) {
    (void)connection;
    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            // TLS handshake finished; client is fully connected.
            std::cout << "QuicServer: client connected\n";
            return QUIC_STATUS_SUCCESS;
        default:
            return QUIC_STATUS_SUCCESS;
    }
}

// Static wrapper for listener events (same pattern as connection_callback).
QUIC_STATUS QuicServer::listener_callback(HQUIC listener,
                                          void* context,
                                          QUIC_LISTENER_EVENT* event) {
    return static_cast<QuicServer*>(context)->on_listener_event(listener, event);
}

// Called when the listener sees activity (e.g. a new client attempting to connect).
QUIC_STATUS QuicServer::on_listener_event(HQUIC listener, QUIC_LISTENER_EVENT* event) {
    (void)listener;
    switch (event->Type) {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            // A client started a QUIC handshake. msquic created a Connection handle for us.
            //
            // Before returning, we MUST:
            //   1. SetCallbackHandler — tell msquic which function handles this connection
            //   2. ConnectionSetConfiguration — apply our TLS cert + ALPN "lft"
            //      (uses configuration_ from Step 2; without this the handshake is rejected)
            api_->SetCallbackHandler(
                event->NEW_CONNECTION.Connection,
                reinterpret_cast<void*>(connection_callback),
                this);
            return api_->ConnectionSetConfiguration(
                event->NEW_CONNECTION.Connection,
                configuration_);

        case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            // Fired after ListenerStop finishes shutting down (used during stop()).
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

// Create the listener object and begin waiting for UDP/QUIC packets on host:port.
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
                               int timeout_ms) {
    (void)expected_message;
    (void)out_reply;
    (void)timeout_ms;
    // TODO: accept connection, receive stream data, send reply.
    return false;
}

}  // namespace lft
