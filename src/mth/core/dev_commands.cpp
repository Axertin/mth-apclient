#include "mth/core/dev_commands.hpp"

#include <cctype>
#include <sstream>

namespace mth
{

namespace
{

std::string to_lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

CommandKind verb_to_kind(const std::string &lower)
{
    if (lower == "help")
        return CommandKind::Help;
    if (lower == "clear")
        return CommandKind::Clear;
    if (lower == "status")
        return CommandKind::Status;
    if (lower == "items")
        return CommandKind::Items;
    if (lower == "giveapitem")
        return CommandKind::GiveItem;
    if (lower == "removelock")
        return CommandKind::RemoveLock;
    if (lower == "modifier")
        return CommandKind::Modifier;
    if (lower == "modifiers")
        return CommandKind::ModifierLock;
    if (lower == "connect")
        return CommandKind::Connect;
    if (lower == "disconnect")
        return CommandKind::Disconnect;
    return CommandKind::Unknown;
}

} // namespace

ParsedCommand parse_command(const std::string &line)
{
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    for (std::string tok; iss >> tok;)
        tokens.push_back(tok);

    ParsedCommand cmd;
    if (tokens.empty())
        return cmd; // kind == None

    cmd.verb = tokens.front();
    cmd.kind = verb_to_kind(to_lower(tokens.front()));
    cmd.args.assign(tokens.begin() + 1, tokens.end());
    return cmd;
}

} // namespace mth
