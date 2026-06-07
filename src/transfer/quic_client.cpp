#include "transfer/quic_client.h"

namespace lft {

QuicClient::QuicClient() = default;

QuicClient::~QuicClient() {
    disconnect();
}

bool QuicClient::connect(std::string_view host, uint16_t port) {
    (void)host;
    (void)port;
    // TODO: MsQuicOpen2, create registration + configuration, connect to server.
    return false;
}

void QuicClient::disconnect() {
    // TODO: close connection, configuration, registration.
    connected_ = false;
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
    // TODO: open bidi stream, send bytes, receive reply.
    return false;
}

}  // namespace lft
