#include "transfer/quic_server.h"

namespace lft {

QuicServer::QuicServer(uint16_t port)
    : port_(port) {}

QuicServer::~QuicServer() {
    stop();
}

bool QuicServer::start(std::string_view host) {
    (void)host;
    // TODO: MsQuicOpen2, create registration + configuration + listener.
    return false;
}

void QuicServer::stop() {
    // TODO: close listener, configuration, registration.
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
