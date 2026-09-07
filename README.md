# LFT

LFT is a C++ application for sending files directly between devices on the same local network. It includes a Qt desktop app and a command-line interface.

Devices are discovered with mDNS, files are transferred over QUIC, and each completed transfer is checked with SHA-256. No account, cloud storage, or internet connection is required.

## How it works

```text
Sender                         Receiver
  │                               │
  ├── discovers _lft._udp ───────►│  mDNS / Bonjour
  ├── sends file details ────────►│
  │◄────── accept or reject ──────┤
  ├── streams file over QUIC ────►│
  │◄──── SHA-256 result ──────────┤
```

The receiver advertises its name and port on the LAN. The sender can connect by that name or use an IP address when discovery is unavailable. Both the GUI and CLI use the same transfer library.

## Project structure

```text
include/       Public headers
src/common/    Shared formatting code
src/net/       mDNS discovery
src/transfer/  QUIC transfer and SHA-256 verification
src/gui/       Qt desktop app
tests/         Unit, integration, and CLI tests
```

## Build

The project is developed and tested on macOS. Install a C++20 compiler, CMake 3.20 or newer, Qt 6, and msquic:

```bash
brew install cmake qt libmsquic
```

Generate the development certificate, then build:

```bash
./scripts/generate_dev_certs.sh

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DBUILD_GUI=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_INTEGRATION_TESTS=ON

cmake --build build --parallel
```

The build creates:

- `build/src/lft_cli`
- `build/src/gui/lft_gui.app`

The generated certificate is self-signed and certificate validation is disabled in the client. It is intended for local development, not production use.

## Run

Start the desktop app:

```bash
open build/src/gui/lft_gui.app
```

For the CLI, start a receiver on one device:

```bash
./build/src/lft_cli recv --out ./downloads
```

On the other device, find the receiver and send a file:

```bash
./build/src/lft_cli list
./build/src/lft_cli send --to "Receiver Name" --file ./video.mp4
```

If mDNS is unavailable, connect by IP:

```bash
./build/src/lft_cli send \
  --host 192.168.1.42 \
  --port 53317 \
  --file ./video.mp4
```

The receiver must approve the transfer before file data is sent.

## Tests

```bash
ctest --test-dir build --output-on-failure -j1
```

## Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [github.com/Parkryan0128](https://github.com/Parkryan0128)
