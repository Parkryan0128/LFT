// Milestone 1 Step 4: client completes a QUIC handshake with a running server.
//
// Flow:
//   1. Server thread: start(), signal "ready".
//   2. Main thread:   wait for ready, connect(), assert is_connected().
//   3. Main thread:   disconnect(), signal "client done".
//   4. Server thread: wait for "client done", stop().
//
// The handshake is verified purely by connect() returning true and the server
// logging "client connected".
#include "transfer/quic_client.h"
#include "transfer/quic_server.h"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

int main() {
    constexpr uint16_t kTestPort = 14433;
    constexpr const char* kTestHost = "127.0.0.1";

    lft::QuicServer server(kTestPort);

    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool server_ready = false;
    bool server_failed = false;
    bool client_done = false;

    std::thread server_thread([&]() {
        const bool ok = server.start(kTestHost);
        {
            std::lock_guard lock(sync_mutex);
            server_ready = true;
            server_failed = !ok;
        }
        sync_cv.notify_all();
        if (!ok) {
            return;
        }

        // Keep listening until the client signals it is finished.
        std::unique_lock lock(sync_mutex);
        sync_cv.wait(lock, [&] { return client_done; });
        lock.unlock();

        server.stop();
    });

    // Wait until the listener is bound (or failed to bind).
    {
        std::unique_lock lock(sync_mutex);
        sync_cv.wait(lock, [&] { return server_ready; });
        if (server_failed) {
            lock.unlock();
            std::cerr << "server failed to start\n";
            server_thread.join();
            return 1;
        }
    }

    int result = 0;
    lft::QuicClient client;

    if (!client.connect(kTestHost, kTestPort)) {
        std::cerr << "client connect failed\n";
        result = 1;
    } else if (!client.is_connected()) {
        std::cerr << "client not connected after connect()\n";
        result = 1;
    }

    client.disconnect();

    // Release the server thread so it can stop().
    {
        std::lock_guard lock(sync_mutex);
        client_done = true;
    }
    sync_cv.notify_all();
    server_thread.join();

    if (result == 0) {
        std::cout << "QuicClient connect OK\n";
    }
    return result;
}
