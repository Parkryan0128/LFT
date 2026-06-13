#include "transfer/sha256.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <openssl/evp.h>

namespace lft {

namespace {

bool digest_to_hex(const unsigned char* digest, std::string& out_hex) {
    std::ostringstream oss;
    for (int i = 0; i < EVP_MD_size(EVP_sha256()); ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    out_hex = oss.str();
    return true;
}

}  // namespace

bool sha256_bytes(std::string_view data, std::string& out_hex) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return false;
    }

    const bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
                    EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;

    EVP_MD_CTX_free(ctx);

    if (!ok) {
        return false;
    }

    return digest_to_hex(digest, out_hex);
}

bool sha256_file(const std::string& path, std::string& out_hex) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return false;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = file.gcount();
        if (n > 0 && EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(n)) != 1) {
            EVP_MD_CTX_free(ctx);
            return false;
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    const bool ok = EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;
    EVP_MD_CTX_free(ctx);

    if (!ok) {
        return false;
    }

    return digest_to_hex(digest, out_hex);
}

}  // namespace lft
