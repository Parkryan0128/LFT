// Unit tests for the Step 6 wire protocol and SHA-256 helper (no networking).
#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FAIL: " << what << '\n';
        ++g_failures;
    }
}

// Encode a header, decode it back, and confirm every field survives intact and
// the decoder reports it consumed exactly the whole header.
void test_header_round_trip() {
    lft::FileTransferHeader in{
        .name = "video.mp4",
        .size = 123456,
        .sha256_hex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    };
    const std::string encoded = lft::encode_file_header(in);

    lft::FileTransferHeader out;
    size_t consumed = 0;
    check(lft::decode_file_header(encoded, out, consumed), "round trip decodes");
    check(out.name == in.name, "round trip name");
    check(out.size == in.size, "round trip size");
    check(out.sha256_hex == in.sha256_hex, "round trip hash");
    check(consumed == encoded.size(), "round trip consumes whole header");
}

// When file body bytes follow the header, the decoder must stop at the header
// boundary and report `consumed` == header length, leaving the body untouched.
void test_header_with_trailing_body() {
    lft::FileTransferHeader in{
        .name = "a.txt",
        .size = 3,
        .sha256_hex = std::string(64, 'a'),
    };
    std::string wire = lft::encode_file_header(in);
    const size_t header_len = wire.size();
    wire += "abc";  // body

    lft::FileTransferHeader out;
    size_t consumed = 0;
    check(lft::decode_file_header(wire, out, consumed), "decodes with body present");
    check(consumed == header_len, "consumed equals header length, body untouched");
}

// A size of 0 (empty file) is a valid header and must decode successfully.
void test_empty_file_allowed() {
    lft::FileTransferHeader in{
        .name = "empty.bin",
        .size = 0,
        .sha256_hex = std::string(64, 'f'),
    };
    const std::string encoded = lft::encode_file_header(in);
    lft::FileTransferHeader out;
    size_t consumed = 0;
    check(lft::decode_file_header(encoded, out, consumed), "size=0 is valid");
    check(out.size == 0, "size=0 parsed");
}

// Malformed headers must be rejected: missing blank-line terminator, wrong
// magic, missing fields, bad hash length/charset, and invalid size values.
void test_rejections() {
    lft::FileTransferHeader out;
    size_t consumed = 0;

    check(!lft::decode_file_header("no blank line here", out, consumed),
          "rejects header without blank line");

    check(!lft::decode_file_header("BAD/9\nname=a\nsize=1\nhash=" + std::string(64, 'a') + "\n\n",
                                   out, consumed),
          "rejects wrong magic");

    check(!lft::decode_file_header("LFT/1\nsize=1\nhash=" + std::string(64, 'a') + "\n\n",
                                   out, consumed),
          "rejects missing name");

    check(!lft::decode_file_header("LFT/1\nname=a\nhash=" + std::string(64, 'a') + "\n\n",
                                   out, consumed),
          "rejects missing size");

    check(!lft::decode_file_header("LFT/1\nname=a\nsize=1\n\n", out, consumed),
          "rejects missing hash");

    check(!lft::decode_file_header("LFT/1\nname=a\nsize=1\nhash=tooshort\n\n", out, consumed),
          "rejects short hash");

    check(!lft::decode_file_header(
              "LFT/1\nname=a\nsize=1\nhash=" + std::string(64, 'z') + "\n\n", out, consumed),
          "rejects non-hex hash");

    check(!lft::decode_file_header(
              "LFT/1\nname=a\nsize=12x\nhash=" + std::string(64, 'a') + "\n\n", out, consumed),
          "rejects size with trailing junk");

    check(!lft::decode_file_header(
              "LFT/1\nname=a\nsize=-1\nhash=" + std::string(64, 'a') + "\n\n", out, consumed),
          "rejects negative size");
}

// Filename sanitization strips directory components / traversal and falls back
// to a default name for empty/dangerous inputs, so a peer can't escape the dir.
void test_name_sanitize() {
    check(lft::sanitize_file_name("../../etc/passwd") == "passwd", "strips traversal");
    check(lft::sanitize_file_name("dir/sub/file.txt") == "file.txt", "strips path");
    check(lft::sanitize_file_name("") == "received.bin", "empty -> default");
    check(lft::sanitize_file_name("..") == "received.bin", ".. -> default");
    check(lft::sanitize_file_name("a\nb.txt").find('\n') == std::string::npos,
          "newline removed from name");
}

// is_sha256_hex accepts exactly 64 hex chars (upper or lower) and rejects
// wrong-length or non-hex strings.
void test_hash_validation() {
    check(lft::is_sha256_hex(std::string(64, 'a')), "64 hex ok");
    check(lft::is_sha256_hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"),
          "uppercase hex ok");
    check(!lft::is_sha256_hex(std::string(63, 'a')), "63 chars rejected");
    check(!lft::is_sha256_hex(std::string(64, 'g')), "non-hex rejected");
}

// The SHA-256 helper must match the published digests for "" and "abc".
void test_sha256_known_vectors() {
    std::string h;
    check(lft::sha256_bytes("", h), "hash empty ok");
    check(h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256(\"\") matches");

    check(lft::sha256_bytes("abc", h), "hash abc ok");
    check(h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256(\"abc\") matches");
}

}  // namespace

int main() {
    test_header_round_trip();
    test_header_with_trailing_body();
    test_empty_file_allowed();
    test_rejections();
    test_name_sanitize();
    test_hash_validation();
    test_sha256_known_vectors();

    if (g_failures == 0) {
        std::cout << "transfer protocol tests OK\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
}
