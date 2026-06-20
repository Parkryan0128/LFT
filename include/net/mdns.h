#pragma once

// LAN peer discovery via mDNS / DNS-SD (Bonjour).
//
// Receivers advertise an "_lft._udp" service carrying their QUIC port; senders
// browse for it and resolve a peer name to an IPv4 address + port. This is the
// Milestone 3 layer that lets `lft send --to <name>` work without a typed IP.
//
// Implemented on the DNS-SD C API (dns_sd.h), which is native on macOS and
// available on Linux through avahi-compat-libdns_sd.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

typedef struct _DNSServiceRef_t* DNSServiceRef;

namespace lft {

// The DNS-SD service type LFT receivers advertise and senders browse for.
inline constexpr const char* kMdnsServiceType = "_lft._udp";

// A discovered receiver: its advertised name and a connectable address.
struct PeerInfo {
    std::string name;  // service/instance name (e.g. the device name)
    std::string host;  // resolved IPv4 address, ready for QuicClient::connect
    uint16_t port = 0;
};

// Advertises this machine's receiver on the LAN for the lifetime of the object.
// A background thread services the DNS-SD socket so registration stays live and
// name conflicts are handled by the system responder.
class MdnsAdvertiser {
public:
    MdnsAdvertiser() = default;
    ~MdnsAdvertiser();

    MdnsAdvertiser(const MdnsAdvertiser&) = delete;
    MdnsAdvertiser& operator=(const MdnsAdvertiser&) = delete;

    // Register "_lft._udp" on `port`. If `service_name` is empty, the system's
    // default (the computer's Bonjour name) is used. Returns false if the
    // responder is unavailable; the caller can fall back to manual IP sharing.
    bool start(const std::string& service_name, uint16_t port);

    // Deregister and stop the background thread. Safe to call more than once.
    void stop();

    bool is_running() const { return running_.load(); }

private:
    void run();  // background DNS-SD event pump

    DNSServiceRef ref_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

// One-shot browser for "_lft._udp" receivers on the LAN.
class MdnsBrowser {
public:
    // Browse for up to `timeout_ms`, then resolve every peer found. Returns all
    // peers that resolved to an IPv4 address (may be empty).
    std::vector<PeerInfo> browse(int timeout_ms);

    // Browse until a peer named `name` appears (or `timeout_ms` elapses), then
    // resolve just that one. Returns std::nullopt if not found or unresolved.
    std::optional<PeerInfo> find_by_name(const std::string& name, int timeout_ms);
};

}  // namespace lft
