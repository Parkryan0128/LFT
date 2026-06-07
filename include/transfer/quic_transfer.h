#pragma once

#include <cstdint>
#include <string>

namespace lft {

// Address and port for QUIC connections (loopback in Milestone 1).
struct QuicEndpoint {
    std::string host;
    uint16_t port = 0;
};

// Result of a one-shot echo round-trip (client sends, server replies).
struct QuicEchoResult {
    bool success = false;
    std::string message_sent;
    std::string message_received;
    std::string error;
};

}  // namespace lft
