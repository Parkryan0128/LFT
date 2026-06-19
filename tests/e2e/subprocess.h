#pragma once

#include <cstdio>
#include <string>
#include <vector>

#ifndef LFT_CLI_PATH
#define LFT_CLI_PATH "lft_cli"
#endif

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/wait.h>
#endif

namespace lft::test {

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

// Run the lft_cli binary with the given args; capture combined stdout+stderr.
inline CommandResult run_cli(const std::vector<std::string>& args) {
    std::string cmd = LFT_CLI_PATH;
    for (const std::string& arg : args) {
        cmd += ' ';
        if (arg.find_first_of(" \t\"'$\\") != std::string::npos) {
            cmd += '"';
            for (char c : arg) {
                if (c == '"' || c == '\\') {
                    cmd += '\\';
                }
                cmd += c;
            }
            cmd += '"';
        } else {
            cmd += arg;
        }
    }
    cmd += " 2>&1";

    CommandResult result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    const int status = pclose(pipe);
#if defined(WIFEXITED) && defined(WEXITSTATUS)
    if (status != -1 && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
#else
    result.exit_code = status;
#endif
    return result;
}

}  // namespace lft::test
