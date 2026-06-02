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

    // verb preserves the raw, original-case first token even for known commands.
    REQUIRE(parse_command("HELP").verb == "HELP");
}

TEST_CASE("parse_command: connect captures args", "[mth][commands]")
{
    const auto c = parse_command("connect localhost:38281 Mina secret");
    REQUIRE(c.kind == CommandKind::Connect);
    REQUIRE(c.args.size() == 3);

    // Arg-count validation is the console layer's job, not the parser's: a bare
    // "connect" still parses as Connect (with no args).
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
