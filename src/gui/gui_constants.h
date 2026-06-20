#pragma once

#include <cstdint>

namespace lft::gui {

inline constexpr uint16_t kDefaultPort = 53317;
inline constexpr int kRecvTimeoutMs = 10 * 60 * 1000;
inline constexpr int kSendTimeoutMs = 5 * 60 * 1000;
inline constexpr int kDiscoveryTimeoutMs = 3000;

}  // namespace lft::gui
