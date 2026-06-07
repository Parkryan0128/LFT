// Verify libmsquic is linked and MsQuicOpen2 works.
#include <iostream>
#include <msquic.h>

int main() {
    const QUIC_API_TABLE* api = nullptr;
    const QUIC_STATUS status = MsQuicOpen2(&api);
    if (QUIC_FAILED(status)) {
        std::cerr << "MsQuicOpen2 failed: " << status << '\n';
        return 1;
    }

    std::cout << "MsQuicOpen2 OK\n";
    MsQuicClose(api);
    return 0;
}
