#pragma once

#include <cstdint>
#include <string>
#include <array>

class UDPSocket {
public:
    static constexpr int INVALID_SOCKET = -1;

    UDPSocket();
    ~UDPSocket();

    // Create and bind a UDP socket to the given address and port
    bool bind(const std::string& addr, uint16_t port);

    // Send a packet to the given address and port
    bool send_to(const std::string& dest_addr, uint16_t dest_port, 
                 const uint8_t* data, size_t len);

    // Receive a packet (blocking)
    // Returns number of bytes received, or -1 on error
    int receive_from(uint8_t* buffer, size_t buffer_len,
                     std::string& src_addr, uint16_t& src_port);

    // Set socket receive timeout (in milliseconds)
    bool set_receive_timeout(int timeout_ms);

    // Close the socket
    void close();

    // Check if socket is valid
    bool is_valid() const;

private:
    int socket_fd;
};
