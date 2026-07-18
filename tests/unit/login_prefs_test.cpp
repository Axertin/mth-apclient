#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/login_prefs.hpp"

namespace
{

std::filesystem::path temp_prefs(const char *name)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

void write_file(const std::filesystem::path &path, const char *text)
{
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("LoginPrefs round-trips server and slot", "[login_prefs]")
{
    const auto path = temp_prefs("mthap_test_login.prefs");

    {
        mth::LoginPrefs p(path);
        REQUIRE(p.server().empty());
        REQUIRE(p.slot().empty());
        p.set("archipelago.gg:38281", "Mina");
        p.save();
    }
    {
        mth::LoginPrefs p(path);
        REQUIRE(p.server() == "archipelago.gg:38281");
        REQUIRE(p.slot() == "Mina");
    }
    std::filesystem::remove(path);
}

TEST_CASE("LoginPrefs on a missing file starts empty", "[login_prefs]")
{
    mth::LoginPrefs p(temp_prefs("mthap_no_such_login.prefs"));
    REQUIRE(p.server().empty());
    REQUIRE(p.slot().empty());
}

TEST_CASE("LoginPrefs preserves spaces in a slot name", "[login_prefs]")
{
    const auto path = temp_prefs("mthap_test_login_spaces.prefs");

    {
        mth::LoginPrefs p(path);
        p.set("localhost:38281", "Some Player Name");
        p.save();
    }
    {
        mth::LoginPrefs p(path);
        REQUIRE(p.slot() == "Some Player Name");
    }
    std::filesystem::remove(path);
}

TEST_CASE("LoginPrefs skips unknown keys and malformed lines", "[login_prefs]")
{
    const auto path = temp_prefs("mthap_test_login_garbage.prefs");
    write_file(path, "\nnonsense\npassword hunter2\nserver host:1234\n= =\nslot Mina\n");

    mth::LoginPrefs p(path);
    REQUIRE(p.server() == "host:1234");
    REQUIRE(p.slot() == "Mina");
    std::filesystem::remove(path);
}

TEST_CASE("LoginPrefs save replaces the previous contents", "[login_prefs]")
{
    const auto path = temp_prefs("mthap_test_login_overwrite.prefs");

    {
        mth::LoginPrefs p(path);
        p.set("first:1", "AAA");
        p.save();
    }
    {
        mth::LoginPrefs p(path);
        p.set("second:2", "B");
        p.save();
    }
    {
        mth::LoginPrefs p(path);
        REQUIRE(p.server() == "second:2");
        REQUIRE(p.slot() == "B");
    }
    std::filesystem::remove(path);
}

TEST_CASE("LoginPrefs trims trailing carriage returns", "[login_prefs]")
{
    const auto path = temp_prefs("mthap_test_login_crlf.prefs");
    write_file(path, "server host:9\r\nslot Mina\r\n");

    mth::LoginPrefs p(path);
    REQUIRE(p.server() == "host:9");
    REQUIRE(p.slot() == "Mina");
    std::filesystem::remove(path);
}
