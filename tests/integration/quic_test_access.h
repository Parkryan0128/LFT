#pragma once

#include "transfer/quic_client.h"
#include "transfer/quic_transfer.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lft::test {

// Friend helpers for negative-path QUIC integration tests.
struct QuicTestAccess {
    static bool SendFileWithHeader(QuicClient& client,
                                   const std::string& file_path,
                                   FileTransferHeader header,
                                   int timeout_ms,
                                   std::optional<uint64_t> bytes_to_send = std::nullopt);

    static bool SendRawStream(QuicClient& client,
                              std::string_view data,
                              bool fin,
                              int timeout_ms);
};

}  // namespace lft::test
