// Verify libmsquic is linked and MsQuicOpen2 works.
#include <gtest/gtest.h>

#include <msquic.h>

TEST(Msquic, OpensAndCloses) {
    const QUIC_API_TABLE* api = nullptr;
    const QUIC_STATUS status = MsQuicOpen2(&api);
    ASSERT_FALSE(QUIC_FAILED(status)) << "MsQuicOpen2 failed: " << status;
    MsQuicClose(api);
}
