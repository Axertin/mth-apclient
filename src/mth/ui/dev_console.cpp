#include "mth/ui/dev_console.hpp"

#include <cfloat>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "mth/core/ap_gate.hpp"
#include "mth/core/command_sink.hpp"
#include "mth/core/data/ability_ids.hpp"
#include "mth/core/dev_commands.hpp"
#include "mth/core/fountain_lamps.hpp"
#include "mth_version.h"
#include "pal/pal_log.hpp"

namespace
{

// Console args are arbitrary user text parsed on the render thread, inside the present
// hook. A throwing conversion would unwind into the game's render loop through the
// trampoline, so these report failure by value and the caller prints usage instead.
// The whole argument must be consumed: "5x" is a typo, not 5.
template <typename T> bool parse_num(const std::string &s, T &out)
{
    const char *end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end;
}

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
    draw_gate_banner(); // always visible; nothing else reliably reaches the player
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

// The gate's annunciator. Drawn on the foreground draw list for the same reason the overlay is
// the right channel at all: it depends on the graphics API, not on game symbols, so it still
// renders on exactly the builds where the game-side hooks are what failed.
void DevConsole::draw_gate_banner()
{
    const GateStatus g = sink_.gate_status();
    const GateVerdict verdict = g.verdict;
    if (verdict == GateVerdict::Clear)
        return;

    char label[256];
    if (verdict == GateVerdict::Refused)
        std::snprintf(label, sizeof(label), "mth-apclient: ARCHIPELAGO DISABLED - %s%s", g.reason.c_str(), g.enforcing ? "" : " [observe-only, not enforced]");
    else
        std::snprintf(label, sizeof(label), "mth-apclient: checking game compatibility...");

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 size = ImGui::CalcTextSize(label);
    const ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f, vp->WorkPos.y + 28.0f);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const ImU32 fg = verdict == GateVerdict::Refused ? IM_COL32(255, 96, 96, 255) : IM_COL32(255, 220, 128, 235);
    dl->AddRectFilled(ImVec2(pos.x - 8.0f, pos.y - 4.0f), ImVec2(pos.x + size.x + 8.0f, pos.y + size.y + 4.0f), IM_COL32(0, 0, 0, 170), 4.0f);
    dl->AddText(pos, fg, label);
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
        println("commands: help, clear, status, gate, items, connect <server> <slot> [pw], disconnect");
        println("          giveapitem <ap_item_id>, removelock <slot>");
        println("          modifier <idx> on|off, modifiers [lock|unlock]  (lock is the default in a session)");
        println("          trap <idx> [seconds]    fire an AP trap by hand (offline test)");
        println("          caps <attack> <defense> <sidearm>  (per-stat level cap-ups; 0 = frozen)");
        println("          ability <name> on|off  (names: burrow swim rope puff spring carry train)");
        println("          deathlink on|off  (enable/disable deathlink, must also be enabled in yaml)");
        println("          litlamps <0..5 ...>|off  (force Ossex fountain lamps lit; offline test)");
        println("          gate [enforce on|off]  (AP safety gate verdict; enforce blocks AP when refused)");
        println("          savetest dump|write|noflush|flush  (save-takeover validation; dev only)");
        break;
    case CommandKind::Clear:
        log_.clear();
        break;
    case CommandKind::Status:
        for (const auto &l : sink_.status_lines())
            println(l);
        break;
    case CommandKind::Gate:
    {
        if (cmd.args.size() >= 2 && cmd.args[0] == "enforce")
        {
            const bool on = cmd.args[1] == "on" || cmd.args[1] == "1" || cmd.args[1] == "true";
            sink_.set_gate_enforcing(on);
            println(std::string("gate enforcing ") + (on ? "on" : "off"));
            break;
        }
        if (!cmd.args.empty())
        {
            println("usage: gate            (show the verdict)");
            println("       gate enforce on|off");
            break;
        }
        const GateStatus g = sink_.gate_status();
        println(std::string("gate verdict: ") + verdict_name(g.verdict) + (g.enforcing ? " (enforcing)" : " (observe-only)"));
        if (!g.reason.empty())
            println("gate reason: " + g.reason);
        break;
    }
    case CommandKind::Items:
        for (const auto &l : sink_.item_lines())
            println(l);
        break;
    case CommandKind::GiveItem:
        if (cmd.args.empty())
            println("usage: giveapitem <ap_item_id>");
        else
        {
            std::int64_t id = 0;
            if (!parse_num(cmd.args[0], id))
                println("giveapitem: '" + cmd.args[0] + "' is not a number");
            else
            {
                sink_.give_item(id);
                println("granting item id " + cmd.args[0]);
            }
        }
        break;
    case CommandKind::RemoveLock:
        if (cmd.args.empty())
            println("usage: removelock <slot>");
        else
        {
            int slot = 0;
            if (!parse_num(cmd.args[0], slot))
                println("removelock: '" + cmd.args[0] + "' is not a number");
            else
            {
                sink_.remove_lock(slot);
                println("removing lock slot " + cmd.args[0]);
            }
        }
        break;
    case CommandKind::Modifier:
        if (cmd.args.size() < 2)
            println("usage: modifier <idx> on|off");
        else
        {
            int idx = 0;
            if (!parse_num(cmd.args[0], idx))
                println("modifier: '" + cmd.args[0] + "' is not a number");
            else
            {
                const bool on = cmd.args[1] == "on" || cmd.args[1] == "1" || cmd.args[1] == "true";
                sink_.set_modifier(idx, on);
                println("modifier " + cmd.args[0] + " " + (on ? "on" : "off"));
            }
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
            int attack = 0;
            int defense = 0;
            int sidearm = 0;
            if (!parse_num(cmd.args[0], attack) || !parse_num(cmd.args[1], defense) || !parse_num(cmd.args[2], sidearm))
                println("caps: expected three numbers");
            else
            {
                sink_.set_stat_caps(attack, defense, sidearm);
                println("stat caps set: attack=" + cmd.args[0] + " defense=" + cmd.args[1] + " sidearm=" + cmd.args[2]);
            }
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
    case CommandKind::Deathlink:
        if (cmd.args.empty())
            println("usage: deathlink on|off");
        else
        {
            const bool on = cmd.args[0] == "on" || cmd.args[0] == "1" || cmd.args[0] == "true";
            sink_.enable_deathlink(on);
            println("deathlink " + std::string(on ? "enabled" : "disabled"));
        }
        break;
    case CommandKind::LitLamps:
    {
        std::vector<int> indices;
        for (const auto &a : cmd.args)
        {
            if (a == "off" || a == "clear")
                continue; // clears (empty index list -> mask 0)
            int idx = 0;
            if (parse_num(a, idx))
                indices.push_back(idx);
            else
                println("litlamps: ignoring non-numeric arg '" + a + "'");
        }
        const std::uint32_t mask = mth::lit_mask_from_indices(indices);
        sink_.set_lit_lamps(mask);
        if (mask == 0)
            println("litlamps cleared");
        else
        {
            std::string lit;
            for (int i = 0; i < mth::kGeneratorLampCount; ++i)
                if ((mask >> static_cast<unsigned>(i)) & 1u)
                    lit += (lit.empty() ? "" : ",") + std::to_string(i);
            println("litlamps lit: " + lit);
        }
        break;
    }
    case CommandKind::SaveTest:
        if (cmd.args.empty())
            println("usage: savetest dump|write|noflush|flush");
        else
        {
            sink_.save_test(cmd.args[0]);
            println("savetest " + cmd.args[0] + " issued; see log");
        }
        break;
    case CommandKind::Trap:
        if (cmd.args.empty())
            println("usage: trap <modifier idx> [seconds]");
        else
        {
            int idx = 0;
            int secs = 0; // 0 means "use the trap table's duration"
            if (!parse_num(cmd.args[0], idx))
                println("trap: '" + cmd.args[0] + "' is not a number");
            else if (cmd.args.size() > 1 && !parse_num(cmd.args[1], secs))
                println("trap: '" + cmd.args[1] + "' is not a number");
            else
            {
                sink_.fire_trap(idx, static_cast<float>(secs));
                println("trap: modifier " + std::to_string(idx) + " queued for the next game tick (see the log for the result)");
            }
        }
        break;
    case CommandKind::Unknown:
        println("unknown command: " + cmd.verb + " (try 'help')");
        break;
    }
}

} // namespace mth
