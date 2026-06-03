#include "udp_socket.h"

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>

UDPSocket::UDPSocket() {
    socket_fd = INVALID_SOCKET;
}

UDPSocket::~UDPSocket() {
    close();
}

bool UDPSocket::bind(const std::string& addr, uint16_t port) {
    // AF_INET for IPv4, SOCK_DGRAM for UDP
    socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    // if socket creation failed, socket_fd will be -1, therefore return false
    if (socket_fd < 0) {
        return false;
    }

    // SOL_SOCKET means we're setting an option at the socket level (not at the TCP/UDP protocol level),
    // Because if we want to resue the address/port, we need to define the socket at the socket level, not at the TCP/UDP protocol level.
    // SO_REUSEADDR allows us to reuse the address/port if it's in TIME_WAIT. When OS finishes using address/port, it does not terminate right away but puts it in TIME_WAIT state for a while
    // setsockop is basically setting options for the socket that I create here.
    int reuse = 1;
    if (::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(socket_fd);
        socket_fd = INVALID_SOCKET;
        return false;
    }

    // Prepare sockaddr_in and bind. Going to declare address/port in sockaddr_in and send to OS to bind.
    struct sockaddr_in sa;
    // memset is used to set all bytes of the sockaddr_in structure to 0.
    std::memset(&sa, 0, sizeof(sa));
    // AF_INET means we're using IPv4
    sa.sin_family = AF_INET;
    // htons converts the port number from host byte order to network byte order (big-endian)
    sa.sin_port = htons(port);

    // inet_pton converts the IP address from text to binary form and stores it in sa.sin_addr. If it returns <= 0, it means the conversion failed (invalid address format).
    if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) <= 0) {
        ::close(socket_fd);
        socket_fd = INVALID_SOCKET;
        return false;
    }

    // bind the sockaddr_in to the socket. If it returns < 0, it means the bind failed (e.g., address already in use).
    if (::bind(socket_fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(socket_fd);
        socket_fd = INVALID_SOCKET;
        return false;
    }

    return true;
}

bool UDPSocket::send_to(const std::string& dest_addr, uint16_t dest_port,
                        const uint8_t* data, size_t len) {
    // check if socket is valid.
    if (socket_fd < 0) {
        return false;
    }

    // Same with bind, prepare sockaddr_in for destination address and port
    struct sockaddr_in dest_sa;
    std::memset(&dest_sa, 0, sizeof(dest_sa));
    dest_sa.sin_family = AF_INET;
    dest_sa.sin_port = htons(dest_port);

    // conversion from text to binary form for destination address.
    if (::inet_pton(AF_INET, dest_addr.c_str(), &dest_sa.sin_addr) <= 0) {
        return false;
    }

    // sendto is used to send data to the destination address and port. It returns the number of bytes sent, or -1 on error.
    ssize_t sent = ::sendto(socket_fd, data, len, 0,
                            reinterpret_cast<struct sockaddr*>(&dest_sa), sizeof(dest_sa));

    // if sent is smaller than 0, (i.e., -1), it means there was an error.
    if (sent < 0) {
        return false;
    }

    // if sent is not equal to len, it means not all bytes were sent.
    if (static_cast<size_t>(sent) != len) {
        return false;
    }

    return true;
}

int UDPSocket::receive_from(uint8_t* buffer, size_t buffer_len,
                            std::string& src_addr, uint16_t& src_port) {

}

bool UDPSocket::set_receive_timeout(int timeout_ms) {

}

void UDPSocket::close() {
    if (socket_fd >= 0) {
        ::close(socket_fd);
        socket_fd = INVALID_SOCKET;
    }
}

bool UDPSocket::is_valid() const {
    return socket_fd >= 0;
}
