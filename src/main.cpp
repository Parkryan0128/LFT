// LFT command-line interface.
//
// Milestone 2, Step 1: argument parsing + command dispatch only. The `send` and
// `recv` handlers are stubs that validate their flags and print what they would
// do; the transfer engine gets wired in during later steps.
//
// Usage:
//   lft recv --port <n> --out <dir>
//   lft send --host <ip> --port <n> --file <path>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint16_t kDefaultPort = 53317;

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

    // Step 1 stub: no networking yet.
    std::cout << "[recv] would listen on port " << *port
              << " and save into \"" << out_it->second << "\"\n";
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

    // Step 1 stub: no networking yet.
    std::cout << "[send] would send \"" << file_it->second << "\" to "
              << host_it->second << ':' << *port << "\n";
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
