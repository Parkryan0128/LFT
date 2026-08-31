# LAN File Transfer Application


A C++ desktop application for **direct file transfer between devices on the same Wi‑Fi or local network**. There is no cloud upload, no account, and no internet dependency — files move straight from sender disk to receiver disk over **QUIC** with **TLS encryption** and **SHA-256 integrity verification**.

***

## Project Structure

The project is organized into libraries, frontends, and tests.

```
.
├── include/                    
│   ├── lft/                    # Shared constants and formatting
│   ├── net/                    # mDNS / DNS-SD discovery
│   └── transfer/               # QUIC client, server, wire protocol, SHA-256
├── src/
│   ├── common/                
│   ├── net/                    # mDNS implementation
│   ├── transfer/               # QUIC transfer engine
│   ├── gui/                    # Qt 6
│   └── main.cpp                
├── tests/
│   ├── unit/                   
│   ├── integration/           
│   └── e2e/                   
├── cmake/                     
├── scripts/
│   └── generate_dev_certs.sh   # Dev TLS certificates for QUIC
├── .github/workflows/
│   └── ci.yml                  
└── CMakeLists.txt
```

***

## How to Build and Run

### 1. Requirements

**macOS (primary development target)**

* **C++20 compiler**
* **CMake** 3.20+
* **Homebrew packages:**
  ```bash
  brew install qt libmsquic cmake
  ```

### 2. Generate Dev TLS Certificates

QUIC uses TLS. Generate self-signed dev certificates once per clone:

```bash
./scripts/generate_dev_certs.sh
```

This writes `certs/lft-cert.pem` and `certs/lft-key.pem`.

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

### 6. Demo Flow (Two Laptops)

1. Connect both machines to the **same Wi‑Fi**.
2. On **Machine A:** open LFT → **Receive** → choose save folder → **Start receiving**.
3. On **Machine B:** open LFT → **Send** → pick file → select Machine A from the device list → **Send**.
4. On **Machine A:** click **Accept** when prompted.
5. Confirm success — file appears in the save folder with SHA-256 verified.

***

## How It Works (Architecture)

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

## Testing

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure -j1
```

***

## Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [https://www.linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [https://github.com/Parkryan0128](https://github.com/Parkryan0128)
