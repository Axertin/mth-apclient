#include <catch2/catch_test_macros.hpp>

#include "mth/core/dev_commands.hpp"

using mth::CommandKind;
using mth::parse_command;

TEST_CASE("parse_command: empty and whitespace are None", "[mth][commands]")
{
    REQUIRE(parse_command("").kind == CommandKind::None);
    REQUIRE(parse_command("   \t ").kind == CommandKind::None);
}

TEST_CASE("parse_command: known verbs map case-insensitively", "[mth][commands]")
{
    REQUIRE(parse_command("help").kind == CommandKind::Help);
    REQUIRE(parse_command("HELP").kind == CommandKind::Help);
    REQUIRE(parse_command("clear").kind == CommandKind::Clear);
    REQUIRE(parse_command("Status").kind == CommandKind::Status);
    REQUIRE(parse_command("items").kind == CommandKind::Items);
    REQUIRE(parse_command("disconnect").kind == CommandKind::Disconnect);

    REQUIRE(parse_command("HELP").verb == "HELP");
}

TEST_CASE("parse_command: connect captures args", "[mth][commands]")
{
    const auto c = parse_command("connect localhost:38281 Mina secret");
    REQUIRE(c.kind == CommandKind::Connect);
    REQUIRE(c.args.size() == 3);

    REQUIRE(parse_command("connect").kind == CommandKind::Connect);
    REQUIRE(parse_command("connect").args.empty());
    REQUIRE(c.args[0] == "localhost:38281");
    REQUIRE(c.args[1] == "Mina");
    REQUIRE(c.args[2] == "secret");
}

TEST_CASE("parse_command: unknown verb is reported with its text", "[mth][commands]")
{
    const auto c = parse_command("frobnicate x y");
    REQUIRE(c.kind == CommandKind::Unknown);
    REQUIRE(c.verb == "frobnicate");
}

TEST_CASE("parse_command recognizes giveapitem", "[dev_commands]")
{
    const auto cmd = mth::parse_command("giveapitem 17");
    REQUIRE(cmd.kind == mth::CommandKind::GiveItem);
    REQUIRE(cmd.args.size() == 1);
    REQUIRE(cmd.args[0] == "17");
}

TEST_CASE("parse_command recognizes removelock with a slot arg", "[commands]")
{
    const auto cmd = mth::parse_command("removelock 42");
    REQUIRE(cmd.kind == mth::CommandKind::RemoveLock);
    REQUIRE(cmd.args.size() == 1);
    REQUIRE(cmd.args[0] == "42");
}

TEST_CASE("parse_command recognizes modifier verbs", "[dev_commands]")
{
    REQUIRE(mth::parse_command("modifier 31 on").kind == mth::CommandKind::Modifier);
    REQUIRE(mth::parse_command("modifier 31 on").args == std::vector<std::string>{"31", "on"});
    REQUIRE(mth::parse_command("modifiers lock").kind == mth::CommandKind::ModifierLock);
    REQUIRE(mth::parse_command("modifiers").kind == mth::CommandKind::ModifierLock);
}

TEST_CASE("parse_command recognizes caps with three args", "[dev_commands]")
{
    const auto cmd = mth::parse_command("caps 1 4 0");
    REQUIRE(cmd.kind == mth::CommandKind::StatCaps);
    REQUIRE(cmd.args == std::vector<std::string>{"1", "4", "0"});
}

TEST_CASE("parse_command recognizes ability with name and on/off", "[dev_commands]")
{
    const auto cmd = mth::parse_command("ability burrow on");
    REQUIRE(cmd.kind == mth::CommandKind::Ability);
    REQUIRE(cmd.args == std::vector<std::string>{"burrow", "on"});
    REQUIRE(mth::parse_command("ABILITY swim off").kind == mth::CommandKind::Ability);
}

TEST_CASE("parse_command recognizes deathlink with on/off", "[dev_commands]")
{
    const auto cmd = mth::parse_command("deathlink off");
    REQUIRE(cmd.kind == mth::CommandKind::Deathlink);
    REQUIRE(cmd.args == std::vector<std::string>{"off"});
    REQUIRE(mth::parse_command("DEATHLINK on").kind == mth::CommandKind::Deathlink);
}
