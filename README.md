# Local File Transfer (LFT)

LFT sends files directly between devices on the same local network. It is written in C++ and includes both a Qt desktop app and a command-line interface.

Devices are discovered with mDNS, files are transferred over QUIC, and completed transfers are verified with SHA-256. No account or cloud storage is required.

## How it works

```text
Sender                         Receiver
  │                               │
  ├── discovers receiver ────────►│  mDNS / Bonjour
  ├── sends file details ────────►│
  │◄────── accept or reject ──────┤
  ├── streams file over QUIC ────►│
  │◄──── verification result ─────┤
```

The receiver advertises an `_lft._udp` service on the network. The sender can connect using the discovered device name or an IP address.

Both interfaces use the same transfer library.

## Project structure

```text
include/       Public headers
src/common/    Shared utilities
src/net/       mDNS discovery
src/transfer/  QUIC transfer and SHA-256 verification
src/gui/       Qt desktop app
tests/         Unit, integration, and CLI tests
```

## Build

The project is developed and tested on macOS.

Install the required packages:

```bash
brew install cmake qt libmsquic
```

Generate a development certificate:

```bash
./scripts/generate_dev_certs.sh
```

Configure and build the project:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DBUILD_GUI=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_INTEGRATION_TESTS=ON

cmake --build build --parallel
```

The build creates:

```text
build/src/lft_cli
build/src/gui/lft_gui.app
```

The generated certificate is self-signed, and the client does not validate it. This setup is intended for local development only.

## Run

Open the desktop app:

```bash
open build/src/gui/lft_gui.app
```

To use the CLI, start a receiver on one device:

```bash
./build/src/lft_cli recv --out ./downloads
```

Find available receivers from another device:

```bash
./build/src/lft_cli list
```

Send a file to a discovered receiver:

```bash
./build/src/lft_cli send \
  --to "Receiver Name" \
  --file ./video.mp4
```

If discovery is unavailable, connect by IP:

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
