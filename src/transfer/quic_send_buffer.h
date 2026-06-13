#pragma once

// Internal helper shared by QuicClient and QuicServer (not part of the public
// API). msquic's StreamSend takes ownership of the buffer until SEND_COMPLETE,
// so we allocate the QUIC_BUFFER header and its payload as one block and free
// it via the SEND_COMPLETE event's ClientContext.

#include <msquic.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace lft {

// Allocate a [QUIC_BUFFER header][payload] block, copy `length` bytes from
// `data`, and send it on `stream` with `flags`. The block is passed as the
// send context so the SEND_COMPLETE handler can std::free() it. On failure the
// block is freed here. Returns the QUIC_STATUS from StreamSend (or
// QUIC_STATUS_OUT_OF_MEMORY if allocation failed).
inline QUIC_STATUS stream_send_copy(const QUIC_API_TABLE* api,
                                    HQUIC stream,
                                    const void* data,
                                    size_t length,
                                    QUIC_SEND_FLAGS flags) {
    auto* raw = static_cast<uint8_t*>(std::malloc(sizeof(QUIC_BUFFER) + length));
    if (raw == nullptr) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }

    auto* buffer = reinterpret_cast<QUIC_BUFFER*>(raw);
    buffer->Buffer = raw + sizeof(QUIC_BUFFER);
    buffer->Length = static_cast<uint32_t>(length);
    if (length > 0) {
        std::memcpy(buffer->Buffer, data, length);
    }

    const QUIC_STATUS status = api->StreamSend(stream, buffer, 1, flags, buffer);
    if (QUIC_FAILED(status)) {
        std::free(raw);
    }
    return status;
}

}  // namespace lft
