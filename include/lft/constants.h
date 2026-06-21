#pragma once

#include <cstdint>

namespace lft {

inline constexpr uint16_t kDefaultPort = 53317;

// How long `recv` waits for a sender to connect before giving up.
inline constexpr int kRecvTimeoutMs = 10 * 60 * 1000;

// How long `send` waits for the transfer (handshake + bytes + ack).
inline constexpr int kSendTimeoutMs = 5 * 60 * 1000;

// How long discovery (`list`, `send --to`) browses the LAN before giving up.
inline constexpr int kDiscoveryTimeoutMs = 3000;

}  // namespace lft
