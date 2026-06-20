#include "net/mdns.h"

#include <dns_sd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <chrono>
#include <cstring>

namespace lft {

namespace {

using Clock = std::chrono::steady_clock;

int ms_until(Clock::time_point deadline) {
    const auto now = Clock::now();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

// Block up to `timeout_ms` for the DNS-SD socket to have data, then dispatch it
// (which fires the relevant callback). Returns true if a result was processed,
// false on timeout or error.
bool pump(DNSServiceRef ref, int timeout_ms) {
    const int fd = DNSServiceRefSockFD(ref);
    if (fd < 0) {
        return false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    const int n = select(fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (n > 0 && FD_ISSET(fd, &read_fds)) {
        return DNSServiceProcessResult(ref) == kDNSServiceErr_NoError;
    }
    return false;
}

// Turn an mDNS host target (e.g. "device.local.") into a connectable IPv4
// string. Returns std::nullopt if it cannot be resolved.
std::optional<std::string> resolve_hostname_ipv4(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) {
        return std::nullopt;
    }

    std::optional<std::string> out;
    for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET) {
            continue;
        }
        const auto* addr = reinterpret_cast<const sockaddr_in*>(ai->ai_addr);
        char buf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) != nullptr) {
            out = buf;
            break;
        }
    }
    freeaddrinfo(result);
    return out;
}

// A service instance seen during browsing, kept so it can be resolved later.
struct FoundService {
    std::string name;
    std::string regtype;
    std::string domain;
    uint32_t interface_index = 0;
};

struct BrowseContext {
    std::vector<FoundService> items;
};

void DNSSD_API on_browse(DNSServiceRef /*ref*/,
                         DNSServiceFlags flags,
                         uint32_t interface_index,
                         DNSServiceErrorType error,
                         const char* name,
                         const char* regtype,
                         const char* domain,
                         void* context) {
    if (error != kDNSServiceErr_NoError || (flags & kDNSServiceFlagsAdd) == 0) {
        return;  // only care about additions
    }
    auto* ctx = static_cast<BrowseContext*>(context);
    for (const auto& existing : ctx->items) {
        if (existing.name == name) {
            return;  // already recorded
        }
    }
    ctx->items.push_back(FoundService{
        .name = name != nullptr ? name : "",
        .regtype = regtype != nullptr ? regtype : "",
        .domain = domain != nullptr ? domain : "",
        .interface_index = interface_index,
    });
}

struct ResolveContext {
    bool done = false;
    std::string host_target;
    uint16_t port = 0;
};

void DNSSD_API on_resolve(DNSServiceRef /*ref*/,
                          DNSServiceFlags /*flags*/,
                          uint32_t /*interface_index*/,
                          DNSServiceErrorType error,
                          const char* /*fullname*/,
                          const char* host_target,
                          uint16_t port_net,
                          uint16_t /*txt_len*/,
                          const unsigned char* /*txt*/,
                          void* context) {
    auto* ctx = static_cast<ResolveContext*>(context);
    if (error != kDNSServiceErr_NoError) {
        ctx->done = true;
        return;
    }
    ctx->host_target = host_target != nullptr ? host_target : "";
    ctx->port = ntohs(port_net);
    ctx->done = true;
}

// Resolve one browsed service to a PeerInfo (host target -> IPv4 + port).
std::optional<PeerInfo> resolve_service(const FoundService& service, int timeout_ms) {
    DNSServiceRef ref = nullptr;
    ResolveContext ctx;
    if (DNSServiceResolve(&ref,
                          0,
                          service.interface_index,
                          service.name.c_str(),
                          service.regtype.c_str(),
                          service.domain.c_str(),
                          &on_resolve,
                          &ctx) != kDNSServiceErr_NoError) {
        return std::nullopt;
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!ctx.done) {
        const int remaining = ms_until(deadline);
        if (remaining <= 0 || !pump(ref, remaining)) {
            break;
        }
    }
    DNSServiceRefDeallocate(ref);

    if (!ctx.done || ctx.host_target.empty()) {
        return std::nullopt;
    }
    const auto ip = resolve_hostname_ipv4(ctx.host_target);
    if (!ip) {
        return std::nullopt;
    }
    return PeerInfo{.name = service.name, .host = *ip, .port = ctx.port};
}

constexpr int kResolveTimeoutMs = 3000;

}  // namespace

MdnsAdvertiser::~MdnsAdvertiser() {
    stop();
}

bool MdnsAdvertiser::start(const std::string& service_name, uint16_t port) {
    if (running_.load()) {
        return false;
    }

    const DNSServiceErrorType err = DNSServiceRegister(
        &ref_,
        0,
        0,  // all interfaces
        service_name.empty() ? nullptr : service_name.c_str(),
        kMdnsServiceType,
        nullptr,  // default domain (.local)
        nullptr,  // this host
        htons(port),
        0,
        nullptr,
        nullptr,  // no register callback needed
        nullptr);
    if (err != kDNSServiceErr_NoError) {
        ref_ = nullptr;
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this] { run(); });
    return true;
}

void MdnsAdvertiser::run() {
    const int fd = DNSServiceRefSockFD(ref_);
    while (running_.load()) {
        if (fd < 0) {
            break;
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        timeval tv{};
        tv.tv_sec = 1;  // wake periodically so stop() is noticed promptly
        tv.tv_usec = 0;

        const int n = select(fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(fd, &read_fds)) {
            if (DNSServiceProcessResult(ref_) != kDNSServiceErr_NoError) {
                break;
            }
        }
    }
}

void MdnsAdvertiser::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (ref_ != nullptr) {
        DNSServiceRefDeallocate(ref_);
        ref_ = nullptr;
    }
}

std::vector<PeerInfo> MdnsBrowser::browse(int timeout_ms) {
    DNSServiceRef ref = nullptr;
    BrowseContext ctx;
    if (DNSServiceBrowse(&ref,
                         0,
                         0,
                         kMdnsServiceType,
                         nullptr,
                         &on_browse,
                         &ctx) != kDNSServiceErr_NoError) {
        return {};
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (ms_until(deadline) > 0) {
        if (!pump(ref, ms_until(deadline))) {
            break;  // timed out with no further results
        }
    }
    DNSServiceRefDeallocate(ref);

    std::vector<PeerInfo> peers;
    for (const auto& service : ctx.items) {
        if (auto peer = resolve_service(service, kResolveTimeoutMs)) {
            peers.push_back(std::move(*peer));
        }
    }
    return peers;
}

std::optional<PeerInfo> MdnsBrowser::find_by_name(const std::string& name, int timeout_ms) {
    DNSServiceRef ref = nullptr;
    BrowseContext ctx;
    if (DNSServiceBrowse(&ref,
                         0,
                         0,
                         kMdnsServiceType,
                         nullptr,
                         &on_browse,
                         &ctx) != kDNSServiceErr_NoError) {
        return std::nullopt;
    }

    std::optional<FoundService> match;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!match && ms_until(deadline) > 0) {
        if (!pump(ref, ms_until(deadline))) {
            break;
        }
        for (const auto& service : ctx.items) {
            if (service.name == name) {
                match = service;
                break;
            }
        }
    }
    DNSServiceRefDeallocate(ref);

    if (!match) {
        return std::nullopt;
    }
    return resolve_service(*match, kResolveTimeoutMs);
}

}  // namespace lft
