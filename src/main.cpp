// LFT command-line interface.
//
// Usage:
//   lft recv --port <n> --out <dir> [--name <device>]
//   lft send --to <device> --file <path>
//   lft send --host <ip> --port <n> --file <path>
//   lft list
#include "lft/constants.h"
#include "lft/format.h"
#include "net/mdns.h"
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
#include <unistd.h>

namespace {

// Build a progress callback that redraws a single line in place. No-op when
// stdout is not a TTY (keeps piped logs clean; the final summary still prints).
lft::ProgressFn make_progress(std::string label) {
    if (isatty(fileno(stdout)) == 0) {
        return nullptr;
    }
    return [label = std::move(label), last_pct = -1](uint64_t done,
                                                     uint64_t total) mutable {
        const int pct = total == 0 ? 100 : static_cast<int>((done * 100) / total);
        if (pct == last_pct) {
            return;  // throttle: only redraw on a percentage change
        }
        last_pct = pct;
        std::cout << '\r' << label << ' ' << pct << "%  ("
                  << lft::format_bytes(done) << " / " << lft::format_bytes(total) << ")   "
                  << std::flush;
    };
}

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

// This machine's hostname, used as the default advertised name for display.
std::string local_hostname() {
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0) {
        return "this device";
    }
    return buf;
}

void print_usage(std::ostream& os) {
    os << "LFT — LAN File Transfer\n\n"
       << "Usage:\n"
       << "  lft recv --out <dir> [--port <n>] [--name <device>]\n"
       << "  lft send --to <device> --file <path>\n"
       << "  lft send --host <ip> --file <path> [--port <n>]\n"
       << "  lft list\n\n"
       << "Commands:\n"
       << "  recv    Receive one file and save it into <dir>.\n"
       << "  send    Send one file to a discovered device (--to) or an IP (--host).\n"
       << "  list    Show LFT receivers discovered on the LAN.\n\n"
       << "Options:\n"
       << "  --to <device>  Receiver device name as shown by `lft list` (send only).\n"
       << "  --host <ip>    Receiver IP address (send only).\n"
       << "  --port <n>     QUIC listen port (default " << lft::kDefaultPort << ").\n"
       << "  --file <path>  File to send (send only).\n"
       << "  --out <dir>    Directory to save into (recv only).\n"
       << "  --name <name>  Advertised device name (recv only; default: hostname).\n"
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

    if (!reject_unknown_flags(flags, {"port", "out", "name"})) {
        return 2;
    }

    const auto port = parse_port(flag_or(flags, "port", std::to_string(lft::kDefaultPort)));
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

    // Advertise on the LAN so senders can find us with `lft send --to <name>`.
    // Best-effort: if the responder is unavailable, manual IP still works.
    const std::string advertised_name = flag_or(flags, "name", "");
    lft::MdnsAdvertiser advertiser;
    if (advertiser.start(advertised_name, *port)) {
        const std::string shown = advertised_name.empty() ? local_hostname() : advertised_name;
        std::cout << "  discoverable as \"" << shown << "\" — on another machine, run:\n"
                  << "    lft send --to \"" << shown << "\" --file <path>\n";
    } else {
        std::cout << "  (LAN discovery unavailable; use manual IP below)\n";
    }

    const std::vector<std::string> ips = local_ipv4s();
    if (ips.empty()) {
        std::cout << "  (could not determine this machine's LAN IP)\n";
    } else {
        std::cout << "  or by IP:\n";
        for (const std::string& ip : ips) {
            std::cout << "    lft send --host " << ip << " --port " << *port
                      << " --file <path>\n";
        }
    }
    // Flush: live status line, and stdout is fully buffered when piped.
    std::cout << "  waiting for sender..." << std::endl;

    // Prompt the user to accept/reject when the file header arrives (before any
    // bytes are written). Runs on receive_file()'s thread.
    auto on_offer = [](const lft::FileTransferHeader& header) -> bool {
        std::cout << "\nIncoming file: \"" << header.name << "\" ("
                  << header.size << " bytes)\n"
                  << "Accept? [y/N]: " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            return false;
        }
        return answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes";
    };

    const bool ok = server.receive_file(out_dir, lft::kRecvTimeoutMs, nullptr, on_offer,
                                        make_progress("  receiving"));
    if (isatty(fileno(stdout)) != 0) {
        std::cout << "\n";  // end the in-place progress line
    }
    server.stop();

    const lft::FileReceiveResult& result = server.last_file_result();
    if (!ok) {
        if (result.rejected) {
            std::cout << "declined the transfer\n";
            return 0;  // a deliberate choice, not an error
        }
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

    if (!reject_unknown_flags(flags, {"host", "to", "port", "file"})) {
        return 2;
    }

    const auto host_it = flags.find("host");
    const auto to_it = flags.find("to");
    const bool has_host = host_it != flags.end() && !host_it->second.empty();
    const bool has_to = to_it != flags.end() && !to_it->second.empty();

    if (has_host == has_to) {
        std::cerr << "error: send requires exactly one of --to <device> or --host <ip>\n";
        return 2;
    }

    const auto file_it = flags.find("file");
    if (file_it == flags.end() || file_it->second.empty()) {
        std::cerr << "error: send requires --file <path>\n";
        return 2;
    }
    const std::string& file = file_it->second;

    // Fail early with a clear message before discovery/connection.
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || std::filesystem::is_directory(file, ec)) {
        std::cerr << "error: file not found: " << file << "\n";
        return 1;
    }

    // Resolve the target: either a discovered device name (--to) or a literal IP
    // (--host). Discovery supplies the port; --port only applies to --host.
    std::string host;
    uint16_t target_port = 0;
    if (has_to) {
        const std::string& name = to_it->second;
        std::cout << "[send] searching for \"" << name << "\" on the LAN..." << std::endl;
        lft::MdnsBrowser browser;
        const auto peer = browser.find_by_name(name, lft::kDiscoveryTimeoutMs);
        if (!peer) {
            std::cerr << "error: could not find device \"" << name
                      << "\" on the LAN (try `lft list`)\n";
            return 1;
        }
        host = peer->host;
        target_port = peer->port;
    } else {
        const auto port = parse_port(flag_or(flags, "port", std::to_string(lft::kDefaultPort)));
        if (!port) {
            std::cerr << "error: --port must be a number in 1..65535\n";
            return 2;
        }
        host = host_it->second;
        target_port = *port;
    }

    lft::QuicClient client;
    std::cout << "[send] connecting to " << host << ':' << target_port << "..." << std::endl;
    if (!client.connect(host, target_port)) {
        std::cerr << "error: could not connect to " << host << ':' << target_port << "\n";
        return 1;
    }

    const bool ok = client.send_file(file, lft::kSendTimeoutMs, make_progress("  sending"));
    if (isatty(fileno(stdout)) != 0) {
        std::cout << "\n";  // end the in-place progress line
    }
    const bool rejected = client.was_rejected();
    client.disconnect();

    if (!ok) {
        if (rejected) {
            std::cerr << "transfer rejected by receiver\n";
        } else {
            std::cerr << "error: transfer failed\n";
        }
        return 1;
    }

    std::cout << "sent \"" << file << "\" to " << host << ':' << target_port
              << " (SHA-256 verified by receiver)\n";
    return 0;
}

int run_list(const std::vector<std::string_view>& args) {
    auto parsed = parse_flags(args);
    if (!parsed) {
        return 2;
    }
    if (!reject_unknown_flags(*parsed, {})) {
        return 2;
    }

    std::cout << "Searching for LFT receivers on the LAN..." << std::endl;
    lft::MdnsBrowser browser;
    const std::vector<lft::PeerInfo> peers = browser.browse(lft::kDiscoveryTimeoutMs);

    if (peers.empty()) {
        std::cout << "No receivers found. Make sure the other device is running "
                  << "`lft recv`.\n";
        return 0;
    }

    std::cout << "Found " << peers.size() << " receiver(s):\n";
    for (const lft::PeerInfo& peer : peers) {
        std::cout << "  \"" << peer.name << "\"  (" << peer.host << ':' << peer.port << ")\n"
                  << "    lft send --to \"" << peer.name << "\" --file <path>\n";
    }
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
    if (command == "list") {
        return run_list(rest);
    }

    std::cerr << "error: unknown command \"" << command << "\"\n\n";
    print_usage(std::cerr);
    return 2;
}
