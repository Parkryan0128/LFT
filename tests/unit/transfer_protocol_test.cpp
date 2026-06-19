// Unit tests for the wire protocol and SHA-256 helper (no networking).
#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include <gtest/gtest.h>

#include <string>

namespace {

lft::FileTransferHeader make_header(const std::string& name,
                                    uint64_t size,
                                    const std::string& hash) {
    return lft::FileTransferHeader{
        .name = name,
        .size = size,
        .sha256_hex = hash,
    };
}

}  // namespace

TEST(Protocol, HeaderRoundTrip) {
    const lft::FileTransferHeader in = make_header(
        "video.mp4",
        123456,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string encoded = lft::encode_file_header(in);

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(encoded, out, consumed));
    EXPECT_EQ(out.name, in.name);
    EXPECT_EQ(out.size, in.size);
    EXPECT_EQ(out.sha256_hex, in.sha256_hex);
    EXPECT_EQ(consumed, encoded.size());
}

TEST(Protocol, HeaderWithTrailingBody) {
    const lft::FileTransferHeader in = make_header("a.txt", 3, std::string(64, 'a'));
    std::string wire = lft::encode_file_header(in);
    const size_t header_len = wire.size();
    wire += "abc";

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(wire, out, consumed));
    EXPECT_EQ(consumed, header_len);
}

TEST(Protocol, EmptyFileAllowed) {
    const lft::FileTransferHeader in = make_header("empty.bin", 0, std::string(64, 'f'));
    const std::string encoded = lft::encode_file_header(in);

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(encoded, out, consumed));
    EXPECT_EQ(out.size, 0u);
}

TEST(Protocol, RejectsMalformedHeaders) {
    lft::FileTransferHeader out;
    size_t consumed = 0;

    EXPECT_FALSE(lft::decode_file_header("no blank line here", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "BAD/9\nname=a\nsize=1\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nsize=1\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header("LFT/1\nname=a\nsize=1\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=1\nhash=tooshort\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=1\nhash=" + std::string(64, 'z') + "\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=12x\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=-1\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
}

TEST(Protocol, SanitizeFileName) {
    EXPECT_EQ(lft::sanitize_file_name("../../etc/passwd"), "passwd");
    EXPECT_EQ(lft::sanitize_file_name("dir/sub/file.txt"), "file.txt");
    EXPECT_EQ(lft::sanitize_file_name(""), "received.bin");
    EXPECT_EQ(lft::sanitize_file_name(".."), "received.bin");
    EXPECT_EQ(lft::sanitize_file_name("a\nb.txt").find('\n'), std::string::npos);
}

TEST(Protocol, HashValidation) {
    EXPECT_TRUE(lft::is_sha256_hex(std::string(64, 'a')));
    EXPECT_TRUE(lft::is_sha256_hex(
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    EXPECT_FALSE(lft::is_sha256_hex(std::string(63, 'a')));
    EXPECT_FALSE(lft::is_sha256_hex(std::string(64, 'g')));
}

TEST(Protocol, Sha256KnownVectors) {
    std::string h;
    ASSERT_TRUE(lft::sha256_bytes("", h));
    EXPECT_EQ(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    ASSERT_TRUE(lft::sha256_bytes("abc", h));
    EXPECT_EQ(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
