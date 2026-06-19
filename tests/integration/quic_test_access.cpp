#include "integration/quic_test_access.h"

namespace lft::test {

bool QuicTestAccess::SendFileWithHeader(QuicClient& client,
                                        const std::string& file_path,
                                        FileTransferHeader header,
                                        int timeout_ms,
                                        std::optional<uint64_t> bytes_to_send) {
    return client.send_file_internal(
        file_path, header, timeout_ms, nullptr, bytes_to_send);
}

bool QuicTestAccess::SendRawStream(QuicClient& client,
                                   std::string_view data,
                                   bool fin,
                                   int timeout_ms) {
    return client.send_raw_stream(data, fin, timeout_ms);
}

}  // namespace lft::test
