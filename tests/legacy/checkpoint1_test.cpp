// UDP loopback test using the UDPSocket helper (legacy Milestone 1 checkpoint).
#include "udp_socket.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

struct SimplePacket {
    uint64_t sequence;
};

constexpr int LOOPBACK_PORT = 5000;
constexpr const char* LOOPBACK_ADDR = "127.0.0.1";
constexpr uint64_t NUM_PACKETS = 5'000'000;

std::atomic<uint64_t> packets_received{0};
std::atomic<bool> receiver_ready{false};
std::atomic<bool> should_stop{false};

void receiver_thread() {
    UDPSocket sock;
    if (!sock.bind(LOOPBACK_ADDR, LOOPBACK_PORT)) {
        std::cerr << "Receiver: failed to bind socket\n";
        return;
    }

    sock.set_receive_timeout(3000);
    receiver_ready = true;

    uint8_t buffer[sizeof(SimplePacket)];
    std::string src_addr;
    uint16_t src_port;
    uint64_t expected_seq = 0;
    uint64_t missing_count = 0;

    while (true) {
        const int received = sock.receive_from(buffer, sizeof(buffer), src_addr, src_port);
        if (received < 0) {
            break;
        }

        if (received != static_cast<int>(sizeof(SimplePacket))) {
            std::cerr << "Receiver: unexpected size " << received << "\n";
            continue;
        }

        SimplePacket pkt;
        std::memcpy(&pkt, buffer, sizeof(SimplePacket));

        if (pkt.sequence != expected_seq) {
            missing_count++;
            if (missing_count <= 10) {
                std::cerr << "Receiver: expected " << expected_seq << " got " << pkt.sequence << "\n";
            }
            expected_seq = pkt.sequence + 1;
        } else {
            expected_seq++;
        }

        packets_received++;

        if (packets_received % 500000 == 0) {
            std::cout << "Receiver: received " << packets_received << " packets\n";
        }
    }

    std::cout << "Receiver: final count = " << packets_received << "\n";
    std::cout << "Receiver: missing sequences = " << missing_count << "\n";
}

void sender_thread() {
    while (!receiver_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    UDPSocket sock;
    if (!sock.bind("127.0.0.1", 0)) {
        std::cerr << "Sender: failed to bind socket\n";
        return;
    }

    uint8_t buffer[sizeof(SimplePacket)];

    std::cout << "Sender: starting to send " << NUM_PACKETS << " packets\n";
    const auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < NUM_PACKETS; ++i) {
        const SimplePacket pkt{i};
        std::memcpy(buffer, &pkt, sizeof(SimplePacket));

        if (!sock.send_to(LOOPBACK_ADDR, LOOPBACK_PORT, buffer, sizeof(SimplePacket))) {
            std::cerr << "Sender: failed to send " << i << "\n";
            break;
        }

        if ((i + 1) % 500000 == 0) {
            std::cout << "Sender: sent " << (i + 1) << " packets\n";
        }
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Sender: completed in " << duration.count() << " seconds\n";

    std::this_thread::sleep_for(std::chrono::seconds(2));
    should_stop = true;
}

int main() {
    std::cout << "=== Checkpoint 1: Local UDP Loopback Test ===\n";

    std::thread recv(receiver_thread);
    std::thread send(sender_thread);

    recv.join();
    send.join();

    std::cout << "\n=== Test Results ===\n";
    std::cout << "Expected packets: " << NUM_PACKETS << "\n";
    std::cout << "Received packets: " << packets_received << "\n";

    if (packets_received == NUM_PACKETS) {
        std::cout << "CHECKPOINT 1 PASSED\n";
        return 0;
    }

    std::cout << "CHECKPOINT 1 FAILED\n";
    return 1;
}
