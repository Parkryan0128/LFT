#include "transfer/quic_client.h"

#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include "quic_send_buffer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

namespace lft {

namespace {

constexpr int kConnectTimeoutMs = 5000;
constexpr int kShutdownTimeoutMs = 3000;
constexpr size_t kFileChunkSize = 64 * 1024;

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

void QuicClient::notify_echo_waiter(bool success) {
    {
        std::lock_guard lock(echo_mutex_);
        echo_done_ = true;
        echo_ok_ = success;
    }
    echo_cv_.notify_one();
}

void QuicClient::notify_send_waiter() {
    {
        std::lock_guard lock(send_mutex_);
        send_done_ = true;
    }
    send_cv_.notify_one();
}

void QuicClient::notify_file_waiter(bool success) {
    {
        std::lock_guard lock(file_mutex_);
        file_done_ = true;
        file_ok_ = success;
    }
    file_cv_.notify_one();
}

void QuicClient::resolve_decision(bool accepted) {
    awaiting_decision_.store(false);
    {
        std::lock_guard lock(decision_mutex_);
        decision_accepted_ = accepted;
        decision_ready_ = true;
    }
    decision_cv_.notify_one();
}

// msquic speaks C, so this static wrapper forwards to the member method.
QUIC_STATUS QuicClient::stream_callback(HQUIC stream,
                                        void* context,
                                        QUIC_STREAM_EVENT* event) {
    return static_cast<QuicClient*>(context)->on_stream_event(stream, event);
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

QUIC_STATUS QuicClient::on_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event) {
    (void)stream;
    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER& buf = event->RECEIVE.Buffers[i];
                const char* bytes = reinterpret_cast<const char*>(buf.Buffer);
                if (stream_mode_ != StreamMode::File) {
                    echo_reply_.append(bytes, buf.Length);
                } else if (awaiting_decision_.load()) {
                    decision_buf_.append(bytes, buf.Length);
                } else {
                    file_ack_.append(bytes, buf.Length);
                }
            }
            // A full ACCEPT/REJECT line resolves the accept-phase wait.
            if (stream_mode_ == StreamMode::File && awaiting_decision_.load() &&
                decision_buf_.find('\n') != std::string::npos) {
                resolve_decision(decision_buf_.starts_with("ACCEPT"));
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            if (stream_mode_ != StreamMode::File) {
                notify_echo_waiter(true);
            } else if (awaiting_decision_.load()) {
                // FIN during the decision phase (e.g. REJECT closes the stream).
                resolve_decision(decision_buf_.starts_with("ACCEPT"));
            } else {
                notify_file_waiter(file_ack_.starts_with("OK"));
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            std::free(event->SEND_COMPLETE.ClientContext);
            if (stream_mode_ == StreamMode::File) {
                notify_send_waiter();
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
                api_->StreamClose(stream);
            }
            stream_ = nullptr;
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

// Open + start a fresh bidirectional stream into stream_. Returns false (and
// leaves stream_ null) on failure.
bool QuicClient::open_stream() {
    if (QUIC_FAILED(api_->StreamOpen(
            connection_,
            QUIC_STREAM_OPEN_FLAG_NONE,
            stream_callback,
            this,
            &stream_))) {
        std::cerr << "QuicClient: StreamOpen failed\n";
        stream_ = nullptr;
        return false;
    }

    if (QUIC_FAILED(api_->StreamStart(stream_, QUIC_STREAM_START_FLAG_NONE))) {
        std::cerr << "QuicClient: StreamStart failed\n";
        api_->StreamClose(stream_);
        stream_ = nullptr;
        return false;
    }

    return true;
}

bool QuicClient::stream_send_and_wait(HQUIC stream,
                                      const void* data,
                                      size_t length,
                                      bool fin,
                                      int timeout_ms) {
    {
        std::lock_guard lock(send_mutex_);
        send_done_ = false;
    }

    const QUIC_SEND_FLAGS flags = fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
    if (QUIC_FAILED(stream_send_copy(api_, stream, data, length, flags))) {
        return false;
    }

    std::unique_lock lock(send_mutex_);
    return send_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return send_done_; });
}

// Client configuration — ALPN "lft" plus credentials that carry no certificate
// and skip server-cert validation (dev only, matches the server's self-signed cert).
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

    if (QUIC_FAILED(MsQuicOpen2(&api_))) {
        std::cerr << "QuicClient::connect: MsQuicOpen2 failed\n";
        api_ = nullptr;
        return false;
    }

    const QUIC_REGISTRATION_CONFIG reg_config{
        .AppName = "LFT",
        .ExecutionProfile = QUIC_EXECUTION_PROFILE_LOW_LATENCY,
    };

    if (QUIC_FAILED(api_->RegistrationOpen(&reg_config, &registration_))) {
        std::cerr << "QuicClient::connect: RegistrationOpen failed\n";
        registration_ = nullptr;
        disconnect();  // releases api_
        return false;
    }

    if (!open_configuration()) {
        disconnect();  // releases registration_ + api_
        return false;
    }

    if (QUIC_FAILED(api_->ConnectionOpen(
            registration_,
            connection_callback,
            this,
            &connection_))) {
        std::cerr << "QuicClient::connect: ConnectionOpen failed\n";
        connection_ = nullptr;
        disconnect();  // releases configuration_ + registration_ + api_
        return false;
    }

    // Start the handshake toward host:port (async — results arrive on the callback).
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
    if (!connected_ || connection_ == nullptr || api_ == nullptr) {
        std::cerr << "QuicClient::send_echo: not connected\n";
        return false;
    }

    echo_reply_.clear();
    stream_mode_ = StreamMode::Echo;
    {
        std::lock_guard lock(echo_mutex_);
        echo_done_ = false;
        echo_ok_ = false;
    }

    if (!open_stream()) {
        return false;
    }

    if (QUIC_FAILED(stream_send_copy(
            api_, stream_, message.data(), message.size(), QUIC_SEND_FLAG_FIN))) {
        std::cerr << "QuicClient::send_echo: StreamSend failed\n";
        abort_stream();
        return false;
    }

    // Block until the server echoes back (PEER_SEND_SHUTDOWN in callback).
    {
        std::unique_lock lock(echo_mutex_);
        const bool finished = echo_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return echo_done_; });

        if (!finished || !echo_ok_) {
            std::cerr << "QuicClient::send_echo: timed out or echo failed\n";
            return false;
        }
    }

    out_reply = echo_reply_;
    std::cout << "QuicClient: received echo \"" << out_reply << "\"\n";
    return true;
}

bool QuicClient::send_file(const std::string& file_path,
                           int timeout_ms,
                           ProgressFn on_progress) {
    if (!connected_ || connection_ == nullptr || api_ == nullptr) {
        std::cerr << "QuicClient::send_file: not connected\n";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || ec) {
        std::cerr << "QuicClient::send_file: file not found: " << file_path << '\n';
        return false;
    }

    if (std::filesystem::is_directory(file_path, ec)) {
        std::cerr << "QuicClient::send_file: path is a directory: " << file_path << '\n';
        return false;
    }

    if (!std::filesystem::is_regular_file(file_path, ec) || ec) {
        std::cerr << "QuicClient::send_file: not a regular file: " << file_path << '\n';
        return false;
    }

    const uint64_t file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        std::cerr << "QuicClient::send_file: cannot determine file size\n";
        return false;
    }

    std::string file_hash;
    if (!sha256_file(file_path, file_hash)) {
        std::cerr << "QuicClient::send_file: failed to hash file\n";
        return false;
    }

    const FileTransferHeader header{
        .name = std::filesystem::path(file_path).filename().string(),
        .size = file_size,
        .sha256_hex = file_hash,
    };
    return send_file_internal(file_path, header, timeout_ms, std::move(on_progress));
}

bool QuicClient::send_file_internal(const std::string& file_path,
                                    const FileTransferHeader& header,
                                    int timeout_ms,
                                    ProgressFn on_progress,
                                    std::optional<uint64_t> bytes_to_send) {
    if (!connected_ || connection_ == nullptr || api_ == nullptr) {
        std::cerr << "QuicClient::send_file: not connected\n";
        return false;
    }

    const std::string header_text = encode_file_header(header);
    const uint64_t file_size = bytes_to_send.value_or(header.size);

    stream_mode_ = StreamMode::File;
    file_ack_.clear();
    rejected_ = false;
    decision_buf_.clear();
    {
        std::lock_guard lock(decision_mutex_);
        decision_ready_ = false;
        decision_accepted_ = false;
    }
    {
        std::lock_guard lock(file_mutex_);
        file_done_ = false;
        file_ok_ = false;
    }

    if (!open_stream()) {
        return false;
    }

    // Send the text header (no FIN). Arm the decision phase so the receiver's
    // ACCEPT/REJECT reply is routed correctly.
    awaiting_decision_.store(true);
    if (!stream_send_and_wait(
            stream_,
            header_text.data(),
            header_text.size(),
            false,
            timeout_ms)) {
        std::cerr << "QuicClient::send_file: failed to send header\n";
        abort_stream();
        return false;
    }

    // Wait for the receiver to accept or reject before sending body bytes.
    {
        std::unique_lock lock(decision_mutex_);
        const bool got = decision_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return decision_ready_; });

        if (!got) {
            lock.unlock();
            std::cerr << "QuicClient::send_file: no accept/reject from receiver\n";
            abort_stream();
            return false;
        }
        if (!decision_accepted_) {
            lock.unlock();
            rejected_ = true;
            std::cerr << "QuicClient::send_file: receiver rejected the transfer\n";
            abort_stream();
            return false;
        }
    }

    // Send file bytes in chunks; FIN on the last chunk.
    if (file_size == 0) {
        if (!stream_send_and_wait(stream_, "", 0, true, timeout_ms)) {
            std::cerr << "QuicClient::send_file: failed to finish empty file\n";
            abort_stream();
            return false;
        }
        if (on_progress) {
            on_progress(0, 0);
        }
    } else {
        std::ifstream input(file_path, std::ios::binary);
        if (!input) {
            std::cerr << "QuicClient::send_file: failed to open file\n";
            abort_stream();
            return false;
        }

        std::vector<char> chunk(kFileChunkSize);
        uint64_t sent = 0;
        while (sent < file_size) {
            const uint64_t remaining = file_size - sent;
            const size_t want = static_cast<size_t>(
                std::min<uint64_t>(remaining, kFileChunkSize));
            input.read(chunk.data(), static_cast<std::streamsize>(want));
            const std::streamsize n = input.gcount();
            if (n <= 0) {
                break;
            }

            sent += static_cast<uint64_t>(n);
            const bool fin = (sent >= file_size);
            if (!stream_send_and_wait(stream_, chunk.data(), static_cast<size_t>(n), fin, timeout_ms)) {
                std::cerr << "QuicClient::send_file: failed to send chunk\n";
                abort_stream();
                return false;
            }
            if (on_progress) {
                on_progress(sent, header.size);
            }
        }

        if (sent != file_size) {
            std::cerr << "QuicClient::send_file: file shrank during send\n";
            abort_stream();
            return false;
        }
    }

    // Wait for server OK/FAIL ack.
    {
        std::unique_lock lock(file_mutex_);
        const bool finished = file_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return file_done_; });

        if (!finished || !file_ok_) {
            std::cerr << "QuicClient::send_file: server rejected or timed out (ack="
                      << file_ack_ << ")\n";
            return false;
        }
    }

    std::cout << "QuicClient: sent file \"" << header.name << "\" ("
              << file_size << " bytes)\n";
    return true;
}

bool QuicClient::send_raw_stream(std::string_view data, bool fin, int timeout_ms) {
    if (!connected_ || connection_ == nullptr || api_ == nullptr) {
        return false;
    }

    stream_mode_ = StreamMode::File;
    file_ack_.clear();
    rejected_ = false;
    {
        std::lock_guard lock(file_mutex_);
        file_done_ = false;
        file_ok_ = false;
    }

    if (!open_stream()) {
        return false;
    }

    if (!stream_send_and_wait(stream_, data.data(), data.size(), fin, timeout_ms)) {
        abort_stream();
        return false;
    }

    if (fin) {
        std::unique_lock lock(file_mutex_);
        file_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return file_done_; });
    }
    return true;
}

void QuicClient::abort_stream() {
    if (api_ != nullptr && stream_ != nullptr) {
        api_->StreamShutdown(stream_, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
    }
}

}  // namespace lft
