// Integration tests for mDNS/DNS-SD discovery (Milestone 3).
#include "net/mdns.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <string>
#include <thread>

namespace {

constexpr int kBrowseTimeoutMs = 8000;
constexpr int kSettleMs = 1000;

std::string next_service_name() {
    static std::atomic<int> counter{0};
    return "lft-test-" + std::to_string(counter.fetch_add(1));
}

uint16_t next_mdns_port() {
    static std::atomic<uint16_t> port{25700};
    return port.fetch_add(1);
}

class MdnsAdvertiserTest : public ::testing::Test {
protected:
    void SetUp() override {
        name_ = next_service_name();
        port_ = next_mdns_port();
    }

    std::string name_;
    uint16_t port_ = 0;
    lft::MdnsAdvertiser advertiser_;
};

}  // namespace

// Advertiser starts, reports running, and stops cleanly.
TEST_F(MdnsAdvertiserTest, StartAndStop) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    EXPECT_TRUE(advertiser_.is_running());

    advertiser_.stop();
    EXPECT_FALSE(advertiser_.is_running());
}

// A second start while already running is rejected.
TEST_F(MdnsAdvertiserTest, DoubleStartReturnsFalse) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    EXPECT_FALSE(advertiser_.start(name_, port_));
}

// After stop(), the advertiser can register again under a new name.
TEST_F(MdnsAdvertiserTest, RestartAfterStop) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    advertiser_.stop();
    EXPECT_FALSE(advertiser_.is_running());

    const std::string new_name = next_service_name();
    const uint16_t new_port = next_mdns_port();
    ASSERT_TRUE(advertiser_.start(new_name, new_port));
    EXPECT_TRUE(advertiser_.is_running());

    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name(new_name, kBrowseTimeoutMs);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->port, new_port);
}

// Destroying the advertiser without an explicit stop still deregisters the service.
TEST(MdnsDiscovery, DestructorUnregistersService) {
    const std::string name = next_service_name();
    const uint16_t port = next_mdns_port();
    {
        lft::MdnsAdvertiser advertiser;
        ASSERT_TRUE(advertiser.start(name, port));
        std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
        lft::MdnsBrowser browser;
        ASSERT_TRUE(browser.find_by_name(name, kBrowseTimeoutMs).has_value());
    }

    // Allow Bonjour to propagate removal; a fresh browse should not resolve it.
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    lft::MdnsBrowser browser;
    EXPECT_FALSE(browser.find_by_name(name, 2000).has_value());
}

// An advertised service shows up in a LAN browse and resolves to IPv4 + port.
TEST_F(MdnsAdvertiserTest, BrowseFindsAdvertisedPeer) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::vector<lft::PeerInfo> peers = browser.browse(kBrowseTimeoutMs);

    bool found = false;
    for (const lft::PeerInfo& peer : peers) {
        if (peer.name == name_) {
            found = true;
            EXPECT_EQ(peer.port, port_);
            EXPECT_FALSE(peer.host.empty());
            break;
        }
    }
    EXPECT_TRUE(found) << "expected to find \"" << name_ << "\" among "
                       << peers.size() << " peer(s)";
}

// find_by_name returns the matching peer with the advertised port.
TEST_F(MdnsAdvertiserTest, FindByNameReturnsPeer) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name(name_, kBrowseTimeoutMs);

    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->name, name_);
    EXPECT_EQ(peer->port, port_);
    EXPECT_FALSE(peer->host.empty());
}

// find_by_name returns nullopt when no service with that name exists.
TEST(MdnsDiscovery, FindByNameUnknownReturnsNullopt) {
    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer =
        browser.find_by_name("lft-definitely-not-present-xyz", kBrowseTimeoutMs);
    EXPECT_FALSE(peer.has_value());
}

// find_by_name rejects an empty name immediately (nothing to match).
TEST(MdnsDiscovery, FindByNameEmptyStringReturnsNullopt) {
    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name("", 1000);
    EXPECT_FALSE(peer.has_value());
}

// Device names are matched case-sensitively.
TEST(MdnsDiscovery, FindByNameIsCaseSensitive) {
    const std::string name = next_service_name() + "-Case";
    const uint16_t port = next_mdns_port();

    lft::MdnsAdvertiser advertiser;
    ASSERT_TRUE(advertiser.start(name, port));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    std::string wrong_case = name;
    for (char& c : wrong_case) {
        c = static_cast<char>(c == static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                                  ? std::toupper(static_cast<unsigned char>(c))
                                  : std::tolower(static_cast<unsigned char>(c)));
    }
    ASSERT_NE(wrong_case, name);

    EXPECT_TRUE(browser.find_by_name(name, kBrowseTimeoutMs).has_value());
    EXPECT_FALSE(browser.find_by_name(wrong_case, 2000).has_value());
}

// Resolved peers carry a dotted IPv4 address suitable for QuicClient::connect.
TEST_F(MdnsAdvertiserTest, ResolvedPeerHasIpv4Host) {
    ASSERT_TRUE(advertiser_.start(name_, port_));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::optional<lft::PeerInfo> peer = browser.find_by_name(name_, kBrowseTimeoutMs);
    ASSERT_TRUE(peer.has_value());
    EXPECT_NE(peer->host.find('.'), std::string::npos);
}

// Two advertisers with distinct names and ports both appear in browse results.
TEST(MdnsDiscovery, BrowseFindsMultipleAdvertisers) {
    const std::string name_a = next_service_name();
    const std::string name_b = next_service_name();
    const uint16_t port_a = next_mdns_port();
    const uint16_t port_b = next_mdns_port();

    lft::MdnsAdvertiser adv_a;
    lft::MdnsAdvertiser adv_b;
    ASSERT_TRUE(adv_a.start(name_a, port_a));
    ASSERT_TRUE(adv_b.start(name_b, port_b));
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));

    lft::MdnsBrowser browser;
    const std::vector<lft::PeerInfo> peers = browser.browse(kBrowseTimeoutMs);

    bool found_a = false;
    bool found_b = false;
    for (const lft::PeerInfo& peer : peers) {
        if (peer.name == name_a && peer.port == port_a) {
            found_a = true;
        }
        if (peer.name == name_b && peer.port == port_b) {
            found_b = true;
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}
