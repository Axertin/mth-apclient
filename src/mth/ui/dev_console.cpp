#include "mth/ui/dev_console.hpp"

#include <cfloat>

#include <imgui.h>

#include "mth/core/dev_commands.hpp"
#include "mth/ui/command_sink.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

DevConsole::DevConsole(ICommandSink &sink) : sink_(sink)
{
    // Mirror the live log stream into the output pane. The observer runs on
    // arbitrary threads; LogRing is thread-safe. It must never call pal::logf.
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

void DevConsole::draw()
{
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("mth dev console"))
    {
        ImGui::End();
        return;
    }

    // Auto-scroll when the log grew since last frame, so lines arriving via the
    // log observer (AP/tick output, not just typed commands) pin to the bottom.
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
            ImGui::TextUnformatted(line.c_str());
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
        ImGui::SetKeyboardFocusHere(-1); // keep focus on the input box after submit
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
