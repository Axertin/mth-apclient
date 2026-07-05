#pragma once

namespace mth
{

// Tracks WHY enforcement features are active (console use) and answers, given current AP
// authentication, whether each feature should enforce. Arming latches for the session;
// vanilla play (nothing armed, not authed) is never affected.
class SessionPolicy
{
  public:
    void arm_console_modifiers() // dev console drove modifiers this session
    {
        console_modifiers_ = true;
    }
    void arm_forced_caps() // console caps: fixed caps, skip AP recompute
    {
        forced_caps_ = true;
    }
    void arm_console_abilities() // dev console drove ability gates this session
    {
        console_abilities_ = true;
    }

    [[nodiscard]] bool enforce_modifiers(bool ap_authenticated) const
    {
        return ap_authenticated || console_modifiers_;
    }
    [[nodiscard]] bool enforce_caps(bool ap_authenticated) const
    {
        return ap_authenticated || forced_caps_;
    }
    [[nodiscard]] bool caps_fixed() const
    {
        return forced_caps_;
    }
    [[nodiscard]] bool enforce_abilities(bool ap_authenticated) const
    {
        return ap_authenticated || console_abilities_;
    }

  private:
    bool console_modifiers_{false};
    bool forced_caps_{false};
    bool console_abilities_{false};
};

} // namespace mth
