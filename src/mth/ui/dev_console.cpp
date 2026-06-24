#include "mth/ui/dev_console.hpp"

#include <cfloat>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include "mth/core/ability_ids.hpp"
#include "mth/core/command_sink.hpp"
#include "mth/core/dev_commands.hpp"
#include "mth_version.h"
#include "pal/pal_log.hpp"

namespace
{

void textOutlined(const char *text, ImU32 textCol = IM_COL32_WHITE, ImU32 outlineCol = IM_COL32_BLACK)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    const ImVec2 offsets[] = {
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}, {-1, 0}, {1, 0}, {0, -1}, {0, 1},
    };

    for (const ImVec2 &off : offsets)
        dl->AddText(ImVec2(pos.x + off.x, pos.y + off.y), outlineCol, text);
    dl->AddText(pos, textCol, text);
    ImGui::Dummy(ImGui::CalcTextSize(text));
}
} // namespace

namespace mth
{

DevConsole::DevConsole(ICommandSink &sink, BannerQueue &banner_queue) : sink_(sink), banner_(banner_queue)
{
    // Observer runs on arbitrary threads; must never call pal::logf.
    pal::set_log_observer([this](pal::LogLevel, std::string_view msg) { log_.push(msg); });
    println("mth dev console. type 'help'.");
}

DevConsole::~DevConsole()
{
    pal::set_log_observer(nullptr);
}

void DevConsole::println(const std::string &line)
{
    log_.push(line);
    scroll_to_bottom_ = true;
}

void DevConsole::draw(bool console_open)
{
    draw_version_hud(); // always visible
    banner_.draw();     // always visible; ignores console_open
    if (console_open)
        draw_console();
}

void DevConsole::draw_version_hud()
{
    // Foreground draw list: never steals input, always visible.
    char label[64];
    std::snprintf(label, sizeof(label), "mth-apclient v%.*s", static_cast<int>(version::string.size()), version::string.data());

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 pos(vp->WorkPos.x + 8.0f, vp->WorkPos.y + 6.0f);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 180), label); // drop shadow
    dl->AddText(pos, IM_COL32(255, 255, 255, 215), label);
}

void DevConsole::draw_console()
{
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0);
    if (!ImGui::Begin("mth dev console"))
    {
        ImGui::End();
        return;
    }

    const auto lines = log_.snapshot();
    if (lines.size() != last_log_size_)
    {
        last_log_size_ = lines.size();
        scroll_to_bottom_ = true;
    }

    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("output", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const auto &line : lines)
            // ImGui::TextUnformatted(line.c_str());
            textOutlined(line.c_str());
        if (scroll_to_bottom_)
        {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom_ = false;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##input", input_.data(), input_.size(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        run_input();
        ImGui::SetKeyboardFocusHere(-1); // keep input focus after submit
    }

    ImGui::End();
}

void DevConsole::run_input()
{
    const std::string line(input_.data());
    input_[0] = '\0';
    if (line.empty())
        return;

    println("> " + line);
    const ParsedCommand cmd = parse_command(line);
    switch (cmd.kind)
    {
    case CommandKind::None:
        break;
    case CommandKind::Help:
        println("commands: help, clear, status, items, connect <server> <slot> [pw], disconnect");
        println("          giveapitem <ap_item_id>, removelock <slot>");
        println("          modifier <idx> on|off, modifiers [lock|unlock]");
        println("          caps <attack> <defense> <sidearm>  (per-stat level cap-ups; 0 = frozen)");
        println("          ability <name> on|off  (names: burrow swim rope puff spring carry train)");
        break;
    case CommandKind::Clear:
        log_.clear();
        break;
    case CommandKind::Status:
        for (const auto &l : sink_.status_lines())
            println(l);
        break;
    case CommandKind::Items:
        for (const auto &l : sink_.item_lines())
            println(l);
        break;
    case CommandKind::GiveItem:
        if (cmd.args.empty())
            println("usage: giveapitem <ap_item_id>");
        else
        {
            sink_.give_item(static_cast<std::int64_t>(std::stoll(cmd.args[0])));
            println("granting item id " + cmd.args[0]);
        }
        break;
    case CommandKind::RemoveLock:
        if (cmd.args.empty())
            println("usage: removelock <slot>");
        else
        {
            sink_.remove_lock(static_cast<int>(std::stoi(cmd.args[0])));
            println("removing lock slot " + cmd.args[0]);
        }
        break;
    case CommandKind::Modifier:
        if (cmd.args.size() < 2)
            println("usage: modifier <idx> on|off");
        else
        {
            const bool on = cmd.args[1] == "on" || cmd.args[1] == "1" || cmd.args[1] == "true";
            sink_.set_modifier(static_cast<int>(std::stoi(cmd.args[0])), on);
            println("modifier " + cmd.args[0] + " " + (on ? "on" : "off"));
        }
        break;
    case CommandKind::ModifierLock:
        if (cmd.args.empty())
        {
            for (const auto &l : sink_.status_lines())
                println(l);
        }
        else
        {
            const bool armed = cmd.args[0] == "lock" || cmd.args[0] == "on" || cmd.args[0] == "1";
            sink_.lock_modifiers(armed);
            println(std::string("modifiers ") + (armed ? "locked" : "unlocked"));
        }
        break;
    case CommandKind::Connect:
        if (cmd.args.size() < 2)
        {
            println("usage: connect <server> <slot> [password]");
        }
        else
        {
            sink_.connect(cmd.args[0], cmd.args[1], cmd.args.size() > 2 ? cmd.args[2] : std::string());
            println("connecting to " + cmd.args[0] + " as " + cmd.args[1] + " ...");
        }
        break;
    case CommandKind::StatCaps:
        if (cmd.args.size() < 3)
            println("usage: caps <attack> <defense> <sidearm>  (per-stat cap-ups; 0 = frozen at level 1)");
        else
        {
            sink_.set_stat_caps(std::stoi(cmd.args[0]), std::stoi(cmd.args[1]), std::stoi(cmd.args[2]));
            println("stat caps set: attack=" + cmd.args[0] + " defense=" + cmd.args[1] + " sidearm=" + cmd.args[2]);
        }
        break;
    case CommandKind::Ability:
        if (cmd.args.size() < 2)
            println("usage: ability <name> on|off  (names: burrow swim rope puff spring carry train)");
        else
        {
            const auto ab = mth::ability_from_name(cmd.args[0]);
            if (!ab)
            {
                println("unknown ability: " + cmd.args[0]);
                println("usage: ability <name> on|off  (names: burrow swim rope puff spring carry train)");
            }
            else
            {
                const bool on = cmd.args[1] == "on" || cmd.args[1] == "1" || cmd.args[1] == "true";
                sink_.set_ability_randomized(*ab, on);
                println("ability " + cmd.args[0] + " randomized " + (on ? "on" : "off"));
            }
        }
        break;
    case CommandKind::Disconnect:
        sink_.disconnect();
        println("disconnect requested");
        break;
    case CommandKind::Unknown:
        println("unknown command: " + cmd.verb + " (try 'help')");
        break;
    }
}

} // namespace mth
