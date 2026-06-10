// Milestone 1 Step 3: verify QuicServer binds and shuts down cleanly.
#include "transfer/quic_server.h"

#include <iostream>

int main() {
    lft::QuicServer server(4433);

    if (!server.start("127.0.0.1")) {
        std::cerr << "QuicServer::start failed\n";
        return 1;
    }

    if (!server.is_running()) {
        std::cerr << "server not running after start\n";
        return 1;
    }

    server.stop();

    if (server.is_running()) {
        std::cerr << "server still running after stop\n";
        return 1;
    }

    std::cout << "QuicServer start/stop OK\n";
    return 0;
}
