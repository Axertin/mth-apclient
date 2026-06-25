#include "mth/ui/login_window.hpp"

#include <imgui.h>

#include "mth/core/ap_state.hpp" // ConnectionPhase
#include "mth/core/command_sink.hpp"

namespace mth
{

LoginWindow::LoginWindow(ICommandSink &sink) : sink_(sink)
{
}

void LoginWindow::draw(bool login_open)
{
    if (!login_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Archipelago Connection"))
    {
        ImGui::End();
        return;
    }

    ImGui::InputText("Server", server_.data(), server_.size()); // host:port
    ImGui::InputText("Slot", slot_.data(), slot_.size());
    ImGui::InputText("Password", password_.data(), password_.size(), ImGuiInputTextFlags_Password);

    const ConnectionStatus status = sink_.connection_status();
    switch (status.phase)
    {
    case ConnectionPhase::Connecting:
        ImGui::BeginDisabled();
        ImGui::Button("Connecting...");
        ImGui::EndDisabled();
        break;
    case ConnectionPhase::Connected:
        if (ImGui::Button("Disconnect"))
            sink_.disconnect();
        break;
    case ConnectionPhase::Disconnected:
    case ConnectionPhase::Error:
        if (ImGui::Button("Connect"))
            sink_.connect(server_.data(), slot_.data(), password_.data());
        break;
    }

    ImGui::Separator();
    switch (status.phase)
    {
    case ConnectionPhase::Disconnected:
        ImGui::TextUnformatted("Disconnected");
        break;
    case ConnectionPhase::Connecting:
        ImGui::TextUnformatted("Connecting...");
        break;
    case ConnectionPhase::Connected:
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Connected");
        break;
    case ConnectionPhase::Error:
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", status.detail.c_str());
        break;
    }

    ImGui::End();
}

} // namespace mth
