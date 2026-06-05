// UDP loopback test using the UDPSocket helper
#include "udp_socket.h"           

#include <iostream>               
#include <thread>                 
#include <atomic>                 
#include <cstring>                
#include <chrono>                 

// Packet is just a sequence number
struct SimplePacket { uint64_t sequence; };

constexpr int LOOPBACK_PORT = 5000;             // port to bind receiver
constexpr const char* LOOPBACK_ADDR = "127.0.0.1"; // loopback address
constexpr uint64_t NUM_PACKETS = 5'000'000;     // how many packets to send

std::atomic<uint64_t> packets_received{0};     // counter of received packets
std::atomic<bool> receiver_ready{false};       // flag when receiver is ready
std::atomic<bool> should_stop{false};          // signal to stop (unused now)

// Receiver thread: bind, receive packets, verify sequence
void receiver_thread() {
    UDPSocket sock;                             // create socket object
    if (!sock.bind(LOOPBACK_ADDR, LOOPBACK_PORT)) { // bind to loopback:port
        std::cerr << "Receiver: failed to bind socket\n"; // error
        return;                                 // stop thread
    }

    sock.set_receive_timeout(3000);             // don't block forever

    receiver_ready = true;                      // let sender start

    uint8_t buffer[sizeof(SimplePacket)];      // receive buffer
    std::string src_addr;                       // source address holder
    uint16_t src_port;                          // source port holder
    uint64_t expected_seq = 0;                  // next expected sequence
    uint64_t missing_count = 0;                 // missing packet counter

    while (true) {
        int received = sock.receive_from(buffer, sizeof(buffer), src_addr, src_port); // recv
        if (received < 0) break;               // timeout or error -> exit

        if (received != static_cast<int>(sizeof(SimplePacket))) { // validate size
            std::cerr << "Receiver: unexpected size " << received << "\n";
            continue;                           // skip bad packet
        }

        SimplePacket pkt;                       // unpack packet
        std::memcpy(&pkt, buffer, sizeof(SimplePacket));

        if (pkt.sequence != expected_seq) {    // check order
            missing_count++;                    // record gap
            if (missing_count <= 10) {
                std::cerr << "Receiver: expected " << expected_seq << " got " << pkt.sequence << "\n";
            }
            expected_seq = pkt.sequence + 1;    // re-sync
        } else {
            expected_seq++;                    // advance
        }

        packets_received++;                    // increment counter

        if (packets_received % 500000 == 0)   // periodic progress
            std::cout << "Receiver: received " << packets_received << " packets\n";
    }

    std::cout << "Receiver: final count = " << packets_received << "\n"; // final report
    std::cout << "Receiver: missing sequences = " << missing_count << "\n";
}

// Sender thread: wait for receiver, then blast packets
void sender_thread() {
    while (!receiver_ready) std::this_thread::sleep_for(std::chrono::milliseconds(10)); // wait

    UDPSocket sock;                             // sender socket
    if (!sock.bind("127.0.0.1", 0)) {         // bind any port
        std::cerr << "Sender: failed to bind socket\n";
        return;
    }

    uint8_t buffer[sizeof(SimplePacket)];      // send buffer

    std::cout << "Sender: starting to send " << NUM_PACKETS << " packets\n";
    auto start = std::chrono::high_resolution_clock::now(); // start timer

    for (uint64_t i = 0; i < NUM_PACKETS; ++i) {
        SimplePacket pkt{i};                    // fill packet
        std::memcpy(buffer, &pkt, sizeof(SimplePacket)); // copy to buffer

        if (!sock.send_to(LOOPBACK_ADDR, LOOPBACK_PORT, buffer, sizeof(SimplePacket))) { // send
            std::cerr << "Sender: failed to send " << i << "\n";
            break;                              // stop on error
        }

        if ((i + 1) % 500000 == 0)              // periodic progress
            std::cout << "Sender: sent " << (i + 1) << " packets\n";
    }

    auto end = std::chrono::high_resolution_clock::now(); // end timer
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Sender: completed in " << duration.count() << " seconds\n";

    std::this_thread::sleep_for(std::chrono::seconds(2)); // let receiver finish
    should_stop = true;                          // signal stop (not used by receiver here)
}

int main() {
    std::cout << "=== Checkpoint 1: Local UDP Loopback Test ===\n"; // header

    std::thread recv(receiver_thread);          // start receiver
    std::thread send(sender_thread);            // start sender

    recv.join();                                // wait for receiver
    send.join();                                // wait for sender

    std::cout << "\n=== Test Results ===\n";  // show results
    std::cout << "Expected packets: " << NUM_PACKETS << "\n";
    std::cout << "Received packets: " << packets_received << "\n";

    if (packets_received == NUM_PACKETS) {
        std::cout << "✓ CHECKPOINT 1 PASSED\n"; // success
        return 0;
    } else {
        std::cout << "✗ CHECKPOINT 1 FAILED\n"; // failure
        return 1;
    }
}
