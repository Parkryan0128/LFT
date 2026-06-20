// Integration test: discover a receiver via mDNS, then transfer a file over QUIC.
#include "integration/quic_test_fixture.h"
#include "net/mdns.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

constexpr int kBrowseTimeoutMs = 8000;
constexpr int kSettleMs = 1000;

std::string next_service_name() {
    static std::atomic<int> counter{0};
    return "lft-xfer-" + std::to_string(counter.fetch_add(1));
}

}  // namespace

// Advertise a QUIC receiver, resolve it by name, and complete a file transfer.
TEST(MdnsTransfer, SendViaDiscoveredPeer) {
    const auto dir = lft::test::temp_test_dir("lft_mdns_xfer");
    const auto input = dir / "payload.bin";
    const auto output = dir / "received.bin";
    ASSERT_TRUE(lft::test::write_file(input, "mdns discovery transfer"));

    const uint16_t port = lft::test::next_port();
    const std::string name = next_service_name();

    lft::MdnsAdvertiser advertiser;
    ASSERT_TRUE(advertiser.start(name, port));

    lft::test::ServerSession session(port);
    session.start_receive(output.string());
    ASSERT_TRUE(session.wait_ready());

    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name(name, kBrowseTimeoutMs);
    ASSERT_TRUE(peer.has_value()) << "could not discover \"" << name << "\"";
    EXPECT_EQ(peer->port, port);

    lft::QuicClient client;
    ASSERT_TRUE(client.connect(peer->host, peer->port));
    EXPECT_TRUE(client.send_file(input.string(), session.timeout_ms));
    client.disconnect();

    session.signal_client_done();
    session.join();

    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(lft::test::hashes_match(input, output));
}

// Discovery succeeds even when nothing is listening on the advertised QUIC port.
TEST(MdnsTransfer, DiscoveredPeerWithoutServerFailsConnect) {
    const uint16_t port = lft::test::next_port();
    const std::string name = next_service_name();

    lft::MdnsAdvertiser advertiser;
    ASSERT_TRUE(advertiser.start(name, port));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name(name, kBrowseTimeoutMs);
    ASSERT_TRUE(peer.has_value());

    lft::QuicClient client;
    EXPECT_FALSE(client.connect(peer->host, peer->port));
}
