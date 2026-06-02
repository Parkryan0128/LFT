# Product Specification: High-Performance P2P UDP File Transfer Engine

## Overview
This project is a lightweight C++ application for fast, direct peer-to-peer file transfer using raw UDP. It is designed to move very large files without cloud storage, bypass TCP overhead, and keep data flowing directly between sender and receiver.

## Goals
* Transfer one file at a time in the first version.
* Build a CLI-first engine, then add a GUI later.
* Support Windows, macOS, and Linux first, with iOS and Android as long-term targets.
* Use a custom UDP reliability layer to recover from packet loss and reordering.
* Keep the core transfer engine separate from any UI.

## Value Proposition
* **Maximum throughput:** Avoids TCP congestion and browser overhead for raw network performance.
* **No cloud egress fees:** Pure P2P delivery means no intermediary storage or per-byte cloud billing.
* **Direct transfer privacy:** Data moves from sender disk to receiver disk over a direct socket path.

---

## Scope and Assumptions
* Initial implementation supports only a single file.
* Command-line interface is the first user-facing layer.
* Cross-platform portability is critical.
* Security should be prepared for stronger protection beyond checksum validation.
* STUN-based NAT traversal is required, but the rendezvous service is not yet finalized.

---

## Architecture
The engine uses a decoupled, multi-threaded design that separates disk I/O, networking, and user interaction.

Sender flow:

  Disk Reader Thread → Thread-Safe Ring Buffer → UDP Network Thread

Receiver flow:

  UDP Network Thread → Out-of-Order Reassembly Buffer → Disk Writer Thread

UI updates are produced by a separate main thread that consumes state from the core engine.

---

## Network Topology
1. **Control Phase:** Both peers briefly use a public STUN server to discover public-facing IP and NAT port mapping.
2. **Hole Punching Phase:** Both clients send simultaneous outbound packets to establish NAT bindings.
3. **Data Phase:** The STUN connection closes, and the peers exchange raw file data directly.

---

## Protocol: UDP-RL (UDP Reliability Layer)
This project uses a custom application-layer reliability protocol on top of UDP.

### Packet Format
Each packet is a compact binary frame with a 24-byte header:

| Offset | Type | Field | Purpose |
| --- | --- | --- | --- |
| 0–1 | `uint16_t` | `MagicNumber` | Protocol ID (`0x5032`) |
| 2 | `uint8_t` | `PacketType` | `0x01`=Data, `0x02`=ACK, `0x03`=NACK, `0x04`=KeepAlive |
| 3 | `uint8_t` | `Flags` | Bit flags (e.g. final packet) |
| 4–11 | `uint64_t` | `SequenceNumber` | Packet ordering ID |
| 12–19 | `uint64_t` | `ChunkID` | Logical file chunk index |
| 20–23 | `uint32_t` | `Checksum` | CRC32 of payload |
| 24+ | `uint8_t[]` | `Payload` | File segment data |

### Reliability Behavior
* **Selective Repeat ARQ:** The sender keeps a sliding window of outstanding packets.
* **ACK / NACK:** The receiver acknowledges received packets and requests retransmission for missing ones.
* **RTT-based retransmission:** Timeouts are computed dynamically and retransmissions are retried automatically.

---

## Data Flow and Threading
The application avoids blocking the main execution path and uses pre-allocated buffers where possible.

### Sender Threads
* **Disk Reader:** Reads the file sequentially in large blocks (e.g. 4 MB) and writes them into a ring buffer.
* **Network Blaster:** Takes buffer data, segments it to MTU-safe packet sizes (around 1400 bytes), adds the UDP-RL header, and sends packets with non-blocking `sendto()` calls.

### Receiver Threads
* **Network Listener:** Receives UDP packets with non-blocking `recvfrom()`, verifies CRC32 checksums, and inserts valid payloads into a reassembly buffer.
* **Disk Writer:** Detects contiguous received data, drains it in order, and writes it sequentially to disk.

---

## Technical Stack
* **Language:** C++20 or newer.
* **Sockets:** Native OS sockets only (`winsock2` on Windows, POSIX sockets on macOS/Linux).
* **Concurrency:** `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`.
* **UI:** Decoupled from core engine; built later with Qt or Dear ImGui.
* **Integrity:** A separate worker thread computes SHA-256 of the final received file and compares it to the sender hash.

---

## Development Checkpoints
Each checkpoint is a stable milestone with a clear verification test.

### Checkpoint 1: Local UDP Proof of Concept
* Build local loopback UDP messaging on `127.0.0.1`.
* Test with at least 5 million sequential packets and no leaks or descriptor exhaustion.

### Checkpoint 2: Custom Reliability Layer
* Implement header parsing, sequence validation, ACK/NACK, and retransmit.
* Verify with a simulated 15% packet drop scenario and confirm perfect reassembly.

### Checkpoint 3: Multi-threaded File Transfer Pipeline
* Connect disk I/O and network flow using thread-safe buffers.
* Transfer a 10 GB binary file and verify SHA-256 integrity with stable RAM usage.

### Checkpoint 4: STUN / NAT Traversal
* Add STUN-based public endpoint discovery and hole punching.
* Validate with two firewalled networks and a direct handshake without manual port configuration.

### Checkpoint 5: GUI and Bandwidth Control
* Wrap the transfer engine in a UI layer.
* Add a throttling control that limits sender output immediately.

---

## Open Questions
* Which level of data protection is required beyond CRC32? (encryption, authentication, replay protection)
* How will peers discover each other? (rendezvous code, PIN, server-assisted exchange)
* What is the first production target: desktop only, or desktop plus mobile simultaneously?
* Should there be a fallback path for NAT traversal failures?

---

## Summary
This project is a focused, high-performance peer-to-peer file transfer engine built around a custom UDP reliability protocol. Starting with CLI-based single-file transfer and desktop portability makes the first implementation tractable, while later GUI and mobile support remain achievable.
