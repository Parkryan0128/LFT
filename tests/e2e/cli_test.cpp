// End-to-end CLI argument parsing and validation tests (subprocess).
#include "e2e/subprocess.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST(CliUsage, NoArgsShowsUsage) {
    const auto result = lft::test::run_cli({});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

TEST(CliUsage, HelpFlag) {
    const auto result = lft::test::run_cli({"--help"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("LFT"), std::string::npos);
}

TEST(CliUsage, UnknownCommand) {
    const auto result = lft::test::run_cli({"upload"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("unknown command"), std::string::npos);
}

TEST(CliSend, MissingHost) {
    const auto result = lft::test::run_cli({"send", "--file", "/tmp/x"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("requires --host"), std::string::npos);
}

TEST(CliSend, MissingFile) {
    const auto result = lft::test::run_cli({"send", "--host", "127.0.0.1"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("requires --file"), std::string::npos);
}

TEST(CliSend, InvalidPort) {
    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--port", "99999", "--file", "/tmp/x"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("--port must be a number"), std::string::npos);
}

TEST(CliSend, UnknownFlag) {
    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--file", "/tmp/x", "--prot", "1"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("unknown flag"), std::string::npos);
}

TEST(CliSend, DuplicateFlag) {
    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--host", "127.0.0.2", "--file", "/tmp/x"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("more than once"), std::string::npos);
}

TEST(CliSend, PositionalArgumentRejected) {
    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--file", "/tmp/x", "extra"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("unexpected argument"), std::string::npos);
}

TEST(CliSend, FlagEqualsForm) {
    const auto result = lft::test::run_cli(
        {"send", "--host=127.0.0.1", "--file=/tmp/lft_missing_cli_test.bin"});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("file not found"), std::string::npos);
}

TEST(CliSend, MissingFileOnDisk) {
    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--port", "59999",
         "--file", "/tmp/lft_cli_missing_file_xyz.bin"});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("file not found"), std::string::npos);
}

TEST(CliSend, CannotConnect) {
    const auto dir = fs::temp_directory_path() / "lft_cli_send_test";
    fs::create_directories(dir);
    const auto file = dir / "payload.txt";
    {
        std::ofstream out(file);
        out << "cli connect test";
    }

    const auto result = lft::test::run_cli(
        {"send", "--host", "127.0.0.1", "--port", "1", "--file", file.string()});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("could not connect"), std::string::npos);
}

TEST(CliRecv, MissingOut) {
    const auto result = lft::test::run_cli({"recv", "--port", "53317"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("requires --out"), std::string::npos);
}

TEST(CliRecv, InvalidPort) {
    const auto result = lft::test::run_cli({"recv", "--port", "0", "--out", "/tmp"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("--port must be a number"), std::string::npos);
}

TEST(CliRecv, OutMustBeDirectory) {
    const auto dir = fs::temp_directory_path() / "lft_cli_recv_test";
    fs::create_directories(dir);
    const auto file = dir / "not_a_dir";
    {
        std::ofstream out(file);
        out << "not a directory";
    }

    const auto result = lft::test::run_cli({"recv", "--port", "53318", "--out", file.string()});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("--out must be a directory"), std::string::npos);
}

TEST(CliRecv, UnknownFlag) {
    const auto result = lft::test::run_cli({"recv", "--out", "/tmp", "--foo", "bar"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.output.find("unknown flag"), std::string::npos);
}
