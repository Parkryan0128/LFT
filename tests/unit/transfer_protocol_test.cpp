// Unit tests for the wire protocol and SHA-256 helper (no networking).
#include "transfer/quic_transfer.h"
#include "transfer/sha256.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

bool write_temp_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
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

TEST(Protocol, EncodeSanitizesNameOnWire) {
    const lft::FileTransferHeader in = make_header(
        "../../etc/passwd",
        1,
        std::string(64, 'a'));
    const std::string encoded = lft::encode_file_header(in);

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(encoded, out, consumed));
    EXPECT_EQ(out.name, "passwd");
    EXPECT_NE(encoded.find("name=passwd"), std::string::npos);
}

TEST(Protocol, UnknownKeysIgnored) {
    const std::string wire =
        "LFT/1\n"
        "name=note.txt\n"
        "size=4\n"
        "hash=" +
        std::string(64, 'b') +
        "\n"
        "future=ignored\n"
        "\n";

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(wire, out, consumed));
    EXPECT_EQ(out.name, "note.txt");
    EXPECT_EQ(out.size, 4u);
}

TEST(Protocol, HeaderIncompleteReturnsFalse) {
    lft::FileTransferHeader out;
    size_t consumed = 0;
    EXPECT_FALSE(lft::decode_file_header("LFT/1\nname=a\nsize=1\n", out, consumed));
}

TEST(Protocol, MaxUint64Size) {
    const lft::FileTransferHeader in = make_header(
        "big.bin",
        UINT64_MAX,
        std::string(64, 'c'));
    const std::string encoded = lft::encode_file_header(in);

    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(encoded, out, consumed));
    EXPECT_EQ(out.size, UINT64_MAX);
}

TEST(Protocol, RejectsLineWithoutEquals) {
    lft::FileTransferHeader out;
    size_t consumed = 0;
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=1\nhash=" + std::string(64, 'a') + "\n"
        "badline\n\n",
        out,
        consumed));
}

TEST(Protocol, RejectsOversizedHash) {
    lft::FileTransferHeader out;
    size_t consumed = 0;
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=1\nhash=" + std::string(65, 'a') + "\n\n", out, consumed));
}

TEST(Protocol, SanitizeBackslashAndControlChars) {
    EXPECT_EQ(lft::sanitize_file_name("a\\b\\c.txt"), "a_b_c.txt");
    EXPECT_EQ(lft::sanitize_file_name(std::string("bad\x01name.bin")), "bad_name.bin");
}

TEST(Sha256, FileRoundTrip) {
    const auto path = std::filesystem::temp_directory_path() / "lft_sha256_unit_test.bin";
    constexpr const char* kContent = "sha256 file test";
    ASSERT_TRUE(write_temp_file(path, kContent));

    std::string file_hash;
    ASSERT_TRUE(lft::sha256_file(path.string(), file_hash));

    std::string bytes_hash;
    ASSERT_TRUE(lft::sha256_bytes(kContent, bytes_hash));
    EXPECT_EQ(file_hash, bytes_hash);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(Protocol, EncodeLowercasesHash) {
    lft::FileTransferHeader in = make_header(
        "a.bin",
        1,
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    const std::string encoded = lft::encode_file_header(in);
    EXPECT_NE(encoded.find("hash=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
              std::string::npos);
}

TEST(Protocol, RejectsEmptySizeField) {
    lft::FileTransferHeader out;
    size_t consumed = 0;
    EXPECT_FALSE(lft::decode_file_header(
        "LFT/1\nname=a\nsize=\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
}

TEST(Protocol, WhitespaceOnlyNameSanitizedToDefault) {
    lft::FileTransferHeader out;
    size_t consumed = 0;
    ASSERT_TRUE(lft::decode_file_header(
        "LFT/1\nname=   \nsize=1\nhash=" + std::string(64, 'a') + "\n\n", out, consumed));
    EXPECT_EQ(out.name, "received.bin");
}

TEST(Sha256, NonexistentFileFails) {
    std::string hash;
    EXPECT_FALSE(lft::sha256_file("/tmp/lft_nonexistent_sha256_input.bin", hash));
}
