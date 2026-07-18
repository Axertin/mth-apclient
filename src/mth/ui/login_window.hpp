#pragma once

#include <array>

namespace mth
{

class ICommandSink;

// Dedicated AP connect window: Server/Slot/Password fields and a Connect<->Disconnect
// button. Reads connection truth from the sink each frame; owns no connection state.
class LoginWindow
{
  public:
    explicit LoginWindow(ICommandSink &sink);

    LoginWindow(const LoginWindow &) = delete;
    LoginWindow &operator=(const LoginWindow &) = delete;

    void draw(bool login_open);

  private:
    void prefill_once(); // pull the remembered server/slot the first time the window is drawn

    ICommandSink &sink_;
    std::array<char, 256> server_{}; // "host:port"
    std::array<char, 128> slot_{};
    std::array<char, 128> password_{};
    bool prefilled_{false};
};

} // namespace mth
