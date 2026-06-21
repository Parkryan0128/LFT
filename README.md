# LFT — LAN File Transfer

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/Build-CMake-green.svg)]()
[![Transport](https://img.shields.io/badge/Transport-QUIC-orange.svg)]()

A C++ desktop application for **direct file transfer between devices on the same Wi‑Fi or local network**. There is no cloud upload, no account, and no internet dependency — files move straight from sender disk to receiver disk over **QUIC** with **TLS encryption** and **SHA-256 integrity verification**.

***

## 📋 Table of Contents

* [Key Features](#-key-features)
* [Project Structure](#-project-structure)
* [How to Build and Run](#️-how-to-build-and-run)
* [How It Works (Architecture)](#️-how-it-works-architecture)
* [Testing](#-testing)
* [Limitations](#-limitations)
* [Contact](#-contact)

***

## ✨ Key Features

* **Direct LAN Transfer:** Send one file at a time between two peers on the same network — no cloud, no relay server.
* **QUIC Transport:** Reliable, encrypted file streaming via [msquic](https://github.com/microsoft/msquic) with TLS and chunked I/O for large files.
* **Automatic Discovery:** mDNS / DNS-SD (Bonjour) advertises receivers on the LAN so senders can find devices by name.
* **Manual Fallback:** Connect by IP and port when mDNS is blocked (guest or corporate Wi‑Fi).
* **Accept / Reject UX:** Receiver must approve each incoming transfer before any file bytes are written.
* **Integrity Verification:** SHA-256 hash computed on the sender and verified on the receiver after every transfer.
* **Dual Interface:** Full **CLI** (`send` / `recv` / `list`) and a minimal **Qt 6 GUI** share the same transfer engine.
* **Layered Architecture:** Transfer engine is decoupled from CLI and GUI for clean separation and testability.
* **Automated Tests:** Google Test unit/integration suite and GitHub Actions CI on macOS.

***

## 📁 Project Structure

The project is organized into libraries, frontends, and tests.

```
.
├── include/                    # Public headers
│   ├── lft/                    # Shared constants and formatting
│   ├── net/                    # mDNS / DNS-SD discovery
│   └── transfer/               # QUIC client, server, wire protocol, SHA-256
├── src/
│   ├── common/                 # Shared utilities (format_bytes)
│   ├── net/                    # mDNS implementation
│   ├── transfer/               # QUIC transfer engine (msquic)
│   ├── gui/                    # Qt 6 desktop UI
│   └── main.cpp                # CLI entry point
├── tests/
│   ├── unit/                   # Wire protocol + SHA-256 tests
│   ├── integration/            # QUIC + mDNS integration tests
│   └── e2e/                    # CLI subprocess tests
├── cmake/                      # FindMsquic.cmake
├── scripts/
│   └── generate_dev_certs.sh   # Dev TLS certificates for QUIC
├── .github/workflows/
│   └── ci.yml                  # Build + test on macOS
└── CMakeLists.txt
```

***

## ⚙️ How to Build and Run

### 1. Requirements

**macOS (primary development target)**

* **C++20 compiler** (Apple Clang via Xcode Command Line Tools)
* **CMake** 3.20+
* **Homebrew packages:**
  ```bash
  brew install qt libmsquic cmake
  ```

**Linux**

* CMake 3.20+, C++20 compiler, Qt 6, msquic, OpenSSL
* mDNS: `avahi-compat-libdns_sd` (or equivalent DNS-SD library)

### 2. Generate Dev TLS Certificates

QUIC uses TLS. Generate self-signed dev certificates once per clone:

```bash
./scripts/generate_dev_certs.sh
```

This writes `certs/lft-cert.pem` and `certs/lft-key.pem` (gitignored).

### 3. Configure and Build

From the project root:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DBUILD_GUI=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_INTEGRATION_TESTS=ON

cmake --build build --parallel
```

Binaries:

| Target | Path |
|--------|------|
| CLI | `build/src/lft_cli` |
| GUI | `build/src/gui/lft_gui.app` |

### 4. Run the CLI

**Receiver** (Machine A — listen and save to a folder):

```bash
./build/src/lft_cli recv --port 53317 --out ./downloads/
```

**Sender** (Machine B — discover by device name):

```bash
./build/src/lft_cli list
./build/src/lft_cli send --to "Machine-A-Name" --file ./video.mp4
```

**Manual IP fallback** (when mDNS is blocked):

```bash
./build/src/lft_cli send --host 192.168.1.42 --port 53317 --file ./video.mp4
```

### 5. Run the GUI

```bash
open build/src/gui/lft_gui.app
```

Or:

```bash
./build/src/gui/lft_gui.app/Contents/MacOS/lft_gui
```

**macOS note:** If macOS blocks the unsigned app on first launch, run:

```bash
xattr -cr /path/to/lft_gui.app
```

Or right-click the app → **Open** → **Open**.

### 6. Demo Flow (Two Laptops)

1. Connect both machines to the **same Wi‑Fi**.
2. On **Machine A:** open LFT → **Receive** → choose save folder → **Start receiving**.
3. On **Machine B:** open LFT → **Send** → pick file → select Machine A from the device list → **Send**.
4. On **Machine A:** click **Accept** when prompted.
5. Confirm success — file appears in the save folder with SHA-256 verified.

***

## 🏗️ How It Works (Architecture)

LFT uses a three-layer design. CLI and GUI are thin clients over the same engine.

```
┌─────────────┐   ┌─────────────┐
│   Qt GUI    │   │     CLI     │
└──────┬──────┘   └──────┬──────┘
       │                 │
       └────────┬────────┘
                ▼
       ┌─────────────────┐
       │ Transfer Engine │  ← QUIC streams, chunked I/O, SHA-256
       └────────┬────────┘
                │
       ┌────────┴────────┐
       ▼                 ▼
  QUIC (file data)   mDNS (discovery)
  encrypted          UDP / Bonjour
```

### 1. Discovery Layer (`lft_net`)

* Receivers advertise an `_lft._udp` DNS-SD service with their QUIC listen port.
* Senders browse the LAN and resolve device names to IPv4 addresses.
* Implemented with the DNS-SD API (`dns_sd.h`) — native on macOS, Avahi compat on Linux.

### 2. Transfer Engine (`lft_transfer`)

* **QUIC client / server** built on msquic with ALPN `"lft"` and dev TLS certificates.
* **Wire protocol:** text header (`LFT/1`, filename, size, SHA-256 hash) followed by raw file bytes.
* **Accept / reject:** receiver sends `ACCEPT\n` or `REJECT\n` before body bytes flow.
* **Chunked streaming:** 64 KB chunks with progress callbacks; empty files supported.
* **Verification:** receiver hashes the saved file and compares to the declared SHA-256.

### 3. Frontends

* **CLI** (`src/main.cpp`): `recv`, `send`, `list` commands with terminal progress output.
* **GUI** (`src/gui/`): Qt 6 pages for home, send, and receive with worker threads for non-blocking I/O.

### Sender Flow

1. Discover peers via mDNS (or enter IP manually).
2. Compute SHA-256 of the file.
3. Open QUIC connection, send metadata header.
4. Wait for receiver accept/reject.
5. Stream file bytes, wait for `OK` ack.

### Receiver Flow

1. Advertise via mDNS and listen for QUIC connections.
2. Parse incoming header → prompt **Accept / Reject**.
3. Stream bytes to disk.
4. Verify SHA-256 and report result.

***

## 🧪 Testing

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure -j1
```

Test categories:

| Category | Examples |
|----------|----------|
| **Unit** | Wire protocol encode/decode, filename sanitization, SHA-256 |
| **Integration** | QUIC connect, file transfer, hash mismatch, mDNS browse |
| **E2E** | CLI argument parsing and validation |

CI runs on every push/PR to `main` via GitHub Actions (`.github/workflows/ci.yml`).

***

## ⚠️ Limitations

* **Same LAN only** — both devices must be on the same local network.
* **One file per transfer** — zip folders manually if needed.
* **Both devices must run LFT** — receiver must be in receive mode.
* **Guest / corporate Wi‑Fi** may block peer-to-device traffic or mDNS — use manual IP.
* **Unsigned macOS builds** require the quarantine workaround above for first launch.

**Out of scope:** internet transfer, NAT traversal, folders, resume, mobile clients.

***

## 📧 Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [https://www.linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [https://github.com/Parkryan0128](https://github.com/Parkryan0128)
