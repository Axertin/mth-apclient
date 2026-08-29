#include <cctype>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/dev_commands.hpp"

using mth::CommandKind;
using mth::parse_command;

namespace
{

struct VerbRow
{
    const char *verb;
    CommandKind kind;
};

// Every kind the console dispatches, in enum order. None and Unknown are parse outcomes, not verbs.
constexpr VerbRow kVerbs[] = {
    {"help", CommandKind::Help},
    {"clear", CommandKind::Clear},
    {"status", CommandKind::Status},
    {"gate", CommandKind::Gate},
    {"items", CommandKind::Items},
    {"giveapitem", CommandKind::GiveItem},
    {"removelock", CommandKind::RemoveLock},
    {"modifier", CommandKind::Modifier},
    {"modifiers", CommandKind::ModifierLock},
    {"caps", CommandKind::StatCaps},
    {"ability", CommandKind::Ability},
    {"connect", CommandKind::Connect},
    {"disconnect", CommandKind::Disconnect},
    {"deathlink", CommandKind::Deathlink},
    {"litlamps", CommandKind::LitLamps},
    {"savetest", CommandKind::SaveTest},
    {"trap", CommandKind::Trap},
    {"switches", CommandKind::Switches},
};

std::string to_upper(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

TEST_CASE("parse_command: empty and whitespace are None", "[mth][commands]")
{
    REQUIRE(parse_command("").kind == CommandKind::None);
    REQUIRE(parse_command("   \t ").kind == CommandKind::None);
}

TEST_CASE("parse_command: every dispatchable kind has a verb, mapped case-insensitively", "[mth][commands]")
{
    for (const VerbRow &row : kVerbs)
    {
        INFO(row.verb);
        REQUIRE(parse_command(row.verb).kind == row.kind);
        REQUIRE(parse_command(to_upper(row.verb)).kind == row.kind);
        // Nothing past token 0 reaches the mapping, so one trailing-argument shape stands in for all of them.
        REQUIRE(parse_command(std::string(row.verb) + " 1 2 3").kind == row.kind);
    }

    // Driven off the enum rather than the table, so a kind whose branch in verb_to_kind is missing or
    // misspelled fails here instead of silently parsing as Unknown. Switches is the last enumerator; a
    // kind added after it has to move this bound.
    for (int i = static_cast<int>(CommandKind::Help); i <= static_cast<int>(CommandKind::Switches); ++i)
    {
        const auto kind = static_cast<CommandKind>(i);
        INFO(i);
        int rows = 0;
        for (const VerbRow &row : kVerbs)
            rows += (row.kind == kind) ? 1 : 0;
        REQUIRE(rows == 1);
    }
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

    // The console echoes the verb back at the player, so it has to come back as typed.
    REQUIRE(parse_command("FROBNICATE").verb == "FROBNICATE");
}

TEST_CASE("parse_command: trap verb is case-insensitive", "[dev_commands][trap]")
{
    REQUIRE(mth::parse_command("TRAP 204").kind == mth::CommandKind::Trap);
}
