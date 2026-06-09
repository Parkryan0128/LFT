#!/usr/bin/env bash
# Dev-only self-signed certificate for QUIC on localhost / LAN testing.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/certs"

openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$ROOT/certs/lft-key.pem" \
  -out "$ROOT/certs/lft-cert.pem" \
  -days 365 \
  -subj "/CN=localhost"

echo "Wrote $ROOT/certs/lft-cert.pem and lft-key.pem"
