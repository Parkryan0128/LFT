// LFT command-line interface.
//
// Milestone 2, Step 1: argument parsing + command dispatch only. The `send` and
// `recv` handlers are stubs that validate their flags and print what they would
// do; the transfer engine gets wired in during later steps.
//
// Usage:
//   lft recv --port <n> --out <dir>
//   lft send --host <ip> --port <n> --file <path>
#include "transfer/quic_client.h"
#include "transfer/quic_server.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// POSIX network-interface enumeration (macOS + Linux) for showing the LAN IP.
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {

constexpr uint16_t kDefaultPort = 53317;

// How long `recv` waits for a sender to connect before giving up.
constexpr int kRecvTimeoutMs = 10 * 60 * 1000;  // 10 minutes

// How long `send` waits for the transfer (handshake + bytes + ack).
constexpr int kSendTimeoutMs = 5 * 60 * 1000;  // 5 minutes

// Collect this machine's non-loopback IPv4 addresses, so a `recv` user can tell
// the sender which IP to target. Best-effort: returns empty on any failure.
std::vector<std::string> local_ipv4s() {
    std::vector<std::string> result;
    ifaddrs* ifa_list = nullptr;
    if (getifaddrs(&ifa_list) != 0) {
        return result;
    }
    for (ifaddrs* ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const auto* addr = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) != nullptr) {
            result.emplace_back(buf);
        }
    }
    freeifaddrs(ifa_list);
    return result;
}

void print_usage(std::ostream& os) {
    os << "LFT — LAN File Transfer\n\n"
       << "Usage:\n"
       << "  lft recv --port <n> --out <dir>\n"
       << "  lft send --host <ip> --port <n> --file <path>\n\n"
       << "Commands:\n"
       << "  recv    Receive one file and save it into <dir>.\n"
       << "  send    Send one file to a receiver at <ip>:<port>.\n\n"
       << "Options:\n"
       << "  --host <ip>    Receiver IP address (send only).\n"
       << "  --port <n>     UDP port (default " << kDefaultPort << ").\n"
       << "  --file <path>  File to send (send only).\n"
       << "  --out <dir>    Directory to save into (recv only).\n"
       << "  -h, --help     Show this help.\n";
}

// Parsed --key value pairs. Values are stored as strings; numeric/range checks
// happen in the command handlers so error messages can be specific.
using Flags = std::unordered_map<std::string, std::string>;

// Parse the tokens after the subcommand into a flag map. Supports "--key value"
// and "--key=value". Returns std::nullopt (after printing an error) on anything
// malformed: a non-flag token, a missing value, or a duplicate flag.
std::optional<Flags> parse_flags(const std::vector<std::string_view>& args) {
    Flags flags;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string_view tok = args[i];
        if (tok.size() < 3 || tok.substr(0, 2) != "--") {
            std::cerr << "error: unexpected argument \"" << tok << "\"\n";
            return std::nullopt;
        }
        tok.remove_prefix(2);

        std::string key;
        std::string value;
        if (const size_t eq = tok.find('='); eq != std::string_view::npos) {
            key = std::string(tok.substr(0, eq));
            value = std::string(tok.substr(eq + 1));
        } else {
            key = std::string(tok);
            if (i + 1 >= args.size()) {
                std::cerr << "error: flag \"--" << key << "\" needs a value\n";
                return std::nullopt;
            }
            value = std::string(args[++i]);
        }

        if (!flags.emplace(key, value).second) {
            std::cerr << "error: flag \"--" << key << "\" given more than once\n";
            return std::nullopt;
        }
    }
    return flags;
}

// Reject any flag the caller did not expect, so typos like "--prot" surface
// instead of being silently ignored.
bool reject_unknown_flags(const Flags& flags,
                          const std::vector<std::string_view>& allowed) {
    bool ok = true;
    for (const auto& [key, _] : flags) {
        bool found = false;
        for (std::string_view a : allowed) {
            if (key == a) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "error: unknown flag \"--" << key << "\"\n";
            ok = false;
        }
    }
    return ok;
}

// Parse a port string into 1..65535. Returns std::nullopt on non-numeric or
// out-of-range input.
std::optional<uint16_t> parse_port(const std::string& s) {
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || value < 1 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

// Look up a flag, returning a default if absent.
std::string flag_or(const Flags& flags, const std::string& key, std::string fallback) {
    const auto it = flags.find(key);
    return it == flags.end() ? std::move(fallback) : it->second;
}

int run_recv(const std::vector<std::string_view>& args) {
    auto parsed = parse_flags(args);
    if (!parsed) {
        return 2;
    }
    const Flags& flags = *parsed;

    if (!reject_unknown_flags(flags, {"port", "out"})) {
        return 2;
    }

    const auto port = parse_port(flag_or(flags, "port", std::to_string(kDefaultPort)));
    if (!port) {
        std::cerr << "error: --port must be a number in 1..65535\n";
        return 2;
    }

    const auto out_it = flags.find("out");
    if (out_it == flags.end() || out_it->second.empty()) {
        std::cerr << "error: recv requires --out <dir>\n";
        return 2;
    }
    const std::string& out_dir = out_it->second;

    // --out is always a directory; create it up front so the engine saves the
    // file under the sender's name (and so the dir-vs-file decision is certain).
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (!std::filesystem::is_directory(out_dir, ec)) {
        std::cerr << "error: --out must be a directory: " << out_dir << "\n";
        return 1;
    }

    // Bind all interfaces so other machines on the LAN can reach us.
    lft::QuicServer server(*port);
    if (!server.start("0.0.0.0")) {
        std::cerr << "error: failed to start receiver on port " << *port << "\n";
        return 1;
    }

    std::cout << "[recv] listening on port " << *port << " (all interfaces), "
              << "saving into \"" << out_dir << "\"\n";
    const std::vector<std::string> ips = local_ipv4s();
    if (ips.empty()) {
        std::cout << "  (could not determine this machine's LAN IP)\n";
    } else {
        std::cout << "  on another machine, run:\n";
        for (const std::string& ip : ips) {
            std::cout << "    lft send --host " << ip << " --port " << *port
                      << " --file <path>\n";
        }
    }
    // Flush: live status line, and stdout is fully buffered when piped.
    std::cout << "  waiting for sender..." << std::endl;

    const bool ok = server.receive_file(out_dir, kRecvTimeoutMs);
    server.stop();

    const lft::FileReceiveResult& result = server.last_file_result();
    if (!ok) {
        std::cerr << "error: receive failed";
        if (!result.error.empty()) {
            std::cerr << ": " << result.error;
        }
        std::cerr << "\n";
        return 1;
    }

    std::cout << "received \"" << result.path << "\" ("
              << result.bytes_received << " bytes, SHA-256 verified)\n";
    return 0;
}

int run_send(const std::vector<std::string_view>& args) {
    auto parsed = parse_flags(args);
    if (!parsed) {
        return 2;
    }
    const Flags& flags = *parsed;

    if (!reject_unknown_flags(flags, {"host", "port", "file"})) {
        return 2;
    }

    const auto host_it = flags.find("host");
    if (host_it == flags.end() || host_it->second.empty()) {
        std::cerr << "error: send requires --host <ip>\n";
        return 2;
    }

    const auto port = parse_port(flag_or(flags, "port", std::to_string(kDefaultPort)));
    if (!port) {
        std::cerr << "error: --port must be a number in 1..65535\n";
        return 2;
    }

    const auto file_it = flags.find("file");
    if (file_it == flags.end() || file_it->second.empty()) {
        std::cerr << "error: send requires --file <path>\n";
        return 2;
    }
    const std::string& host = host_it->second;
    const std::string& file = file_it->second;

    // Fail early with a clear message before opening a connection.
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || std::filesystem::is_directory(file, ec)) {
        std::cerr << "error: file not found: " << file << "\n";
        return 1;
    }

    lft::QuicClient client;
    std::cout << "[send] connecting to " << host << ':' << *port << "..." << std::endl;
    if (!client.connect(host, *port)) {
        std::cerr << "error: could not connect to " << host << ':' << *port << "\n";
        return 1;
    }

    const bool ok = client.send_file(file, kSendTimeoutMs);
    client.disconnect();

    if (!ok) {
        std::cerr << "error: transfer failed\n";
        return 1;
    }

    std::cout << "sent \"" << file << "\" to " << host << ':' << *port
              << " (SHA-256 verified by receiver)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        print_usage(std::cout);
        return args.empty() ? 2 : 0;
    }

    const std::string_view command = args[0];
    const std::vector<std::string_view> rest(args.begin() + 1, args.end());

    if (command == "recv") {
        return run_recv(rest);
    }
    if (command == "send") {
        return run_send(rest);
    }

    std::cerr << "error: unknown command \"" << command << "\"\n\n";
    print_usage(std::cerr);
    return 2;
}
