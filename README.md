# LFT — LAN File Transfer

## Overview

LFT is a C++ desktop application for **direct file transfer between devices on the same Wi‑Fi or local network**. There is no cloud upload, no account, and no internet dependency — files move straight from sender disk to receiver disk.

**v1 focus:** one file, one sender, one receiver, same LAN. Ship a working CLI and a minimal Qt GUI.

---

## Goals

* Transfer **one file at a time** between two peers on the same network.
* Use **QUIC** for reliable, encrypted file transfer.
* Use **mDNS (UDP)** for automatic peer discovery on the LAN.
* Provide a **CLI** for scripting and testing, plus a **minimal Qt GUI** for everyday use.
* Keep the **transfer engine separate** from CLI and GUI.
* Verify file integrity with **SHA-256** after each transfer.

## Value Proposition

* **No cloud:** Files never leave the local network.
* **Simple sharing:** Discover nearby devices automatically; send a large file without USB or Dropbox.
* **Secure by default:** QUIC provides encrypted transport; SHA-256 confirms the received file matches the original.
* **Honest scope:** Built for same-Wi‑Fi / LAN use — not a general internet P2P tool.

---

## v1 Scope

### In scope

| Feature | Details |
| --- | --- |
| **Transfer** | One file, 1 sender → 1 receiver |
| **Network** | Same Wi‑Fi / LAN only (same subnet) |
| **Transport** | QUIC (reliability + encryption) |
| **Discovery** | mDNS / Bonjour (UDP) — peers appear by device name |
| **Fallback** | Manual IP + port when discovery fails |
| **UI** | CLI (`send` / `recv`) + minimal Qt GUI |
| **Security UX** | Receiver **accepts or rejects** each incoming transfer |
| **Integrity** | SHA-256 hash verified at end of transfer |
| **Progress** | Byte progress and basic status in CLI and GUI |
| **Platforms** | macOS and Linux first; Windows if feasible |

### Out of scope (v1)

* Internet transfer, NAT traversal, STUN, hole punching, relay servers
* Folders, multiple files in one session, 1→many broadcast
* Mobile apps (iOS / Android)
* Resume / partial transfer recovery
* Bandwidth throttling
* Custom UDP reliability layer (superseded by QUIC)

---

## Architecture

The application is split into three layers. CLI and GUI are thin clients over the same engine.

```
┌─────────────┐   ┌─────────────┐
│   Qt GUI    │   │     CLI     │
└──────┬──────┘   └──────┬──────┘
       │                 │
       └────────┬────────┘
                ▼
       ┌─────────────────┐
       │ Transfer Engine │  ← QUIC file streams, chunked I/O, hashing
       └────────┬────────┘
                │
       ┌────────┴────────┐
       ▼                 ▼
  QUIC (file data)   mDNS (discovery)
  reliable +         UDP multicast /
  encrypted          peer advertisement
```

### Why this stack?

| Layer | Protocol | Reason |
| --- | --- | --- |
| **File transfer** | QUIC | Reliable delivery, built-in TLS, modern congestion control |
| **Discovery** | mDNS (UDP) | Standard LAN peer discovery; small messages where loss is fine |
| **Manual connect** | QUIC over TCP-like connect to IP:port | Fallback when mDNS is blocked (guest/corporate Wi‑Fi) |

### Sender flow

1. Discover peers via mDNS (or user enters IP manually).
2. User selects a file and target device.
3. Engine computes SHA-256 of the file.
4. Sender opens QUIC connection, sends metadata (filename, size, hash), then file bytes in chunks.
5. Receiver accepts or rejects the transfer request.
6. Receiver verifies SHA-256 after download completes.

### Receiver flow

1. Advertise service via mDNS and listen for QUIC connections.
2. On incoming request, show sender name, filename, and size → **Accept / Reject**.
3. Stream incoming bytes to disk.
4. Verify SHA-256 and report success or failure.

---

## CLI (planned)

```bash
# Receiver — listen for incoming transfers
lft recv --port 53317 --out ./downloads/

# Sender — discover and send (GUI will wrap the same commands)
lft send --to "Jiwon's-MacBook" --file ./video.mp4

# Manual fallback when discovery does not work
lft send --host 192.168.1.42 --port 53317 --file ./video.mp4
```

---

## Technical Stack

| Component | Choice |
| --- | --- |
| **Language** | C++20 |
| **Transfer** | QUIC via [msquic](https://github.com/microsoft/msquic) (or similar) |
| **Discovery** | mDNS / Bonjour (UDP) |
| **GUI** | Qt 6 |
| **Build** | CMake |
| **Concurrency** | `std::thread` for disk I/O pipeline (optional in v1) |
| **Integrity** | SHA-256 |

---

## Development Milestones

Each milestone should be demo-able before moving on.

### Milestone 1: QUIC loopback transfer
* Send one file over QUIC on `127.0.0.1`.
* Verify byte-for-byte or SHA-256 match.

### Milestone 2: CLI send / recv on LAN
* Two machines on same Wi‑Fi transfer a file via manual IP.
* Accept/reject prompt on receiver.

### Milestone 3: mDNS discovery
* Receiver advertises; sender sees device list without typing IP.
* Manual IP fallback still works.

### Milestone 4: Minimal Qt GUI
* Device list, file picker, send button, receive accept/reject, progress bar.

### Milestone 5: Polish
* README demo, error handling, large-file test (1 GB+), clean project structure.

---

## Definition of Done (v1)

- [ ] Two laptops on the same Wi‑Fi discover each other via mDNS
- [ ] Sender transfers one file; receiver accepts and saves it
- [ ] SHA-256 hash matches on completion
- [ ] Works via CLI and Qt GUI
- [ ] Manual IP connect works when discovery fails
- [ ] README documents architecture, limitations, and demo steps

---

## Current Status

Early prototyping included a POSIX **UDP socket wrapper** and a **5M-packet loopback stress test** (Checkpoint 1). The project direction now uses **QUIC for file data** and **UDP only for discovery**. Legacy UDP code may remain in the repo during migration.

---

## Limitations (v1)

* Requires both devices on the **same local network**.
* Both devices must be **running LFT** (discovery and receive mode).
* **Corporate or guest Wi‑Fi** may block device-to-device traffic or mDNS — use manual IP in those cases.
* **One file per transfer** — zip folders manually if needed.

---

## Summary

LFT is a scoped LAN file-sharing tool: **QUIC for transfer, mDNS for discovery, Qt for a simple GUI, CLI for power users.** The goal is a finishable, demo-ready project — not a full LocalSend competitor or internet P2P engine.
