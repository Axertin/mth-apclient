#pragma once

#include <string>
#include <vector>

namespace mth
{

enum class CommandKind
{
    None,    // empty/whitespace-only input
    Unknown, // first token matched no command
    Help,
    Clear,
    Status,
    Items,
    GiveItem,     // args: [ap_item_id]
    RemoveLock,   // args: [slot]
    Modifier,     // args: [idx, on|off]
    ModifierLock, // args: [] (status), [lock|unlock|on|off]
    StatCaps,     // args: [attack, defense, sidearm] per-stat cap-up counts (0 = frozen)
    Ability,      // args: [name, on|off]
    Connect,      // args: [server, slot, (optional) password]
    Disconnect,
};

struct ParsedCommand
{
    CommandKind kind{CommandKind::None};
    std::vector<std::string> args; // tokens after the command word
    std::string verb;              // the raw first token (for Unknown messages)
};

// Splits on whitespace, maps first token to CommandKind (case-insensitive). Empty input -> None.
ParsedCommand parse_command(const std::string &line);

} // namespace mth
