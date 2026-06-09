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

    running_ = true;

    // TODO Step 3: ListenerOpen on bind_host_:port_
    (void)port_;

    return true;
}

void QuicServer::stop() {
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
