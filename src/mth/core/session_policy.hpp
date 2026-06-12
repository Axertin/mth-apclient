#pragma once

namespace mth
{

// Tracks WHY enforcement features are active (env test mode, console use) and answers,
// given current AP authentication, whether each feature should enforce. Arming latches
// for the session; vanilla play (nothing armed, not authed) is never affected.
class SessionPolicy
{
  public:
    void arm_env_modifiers() // MTHAP_MODIFIERS set: offline modifier test mode
    {
        env_modifiers_ = true;
    }
    void arm_console_modifiers() // dev console drove modifiers this session
    {
        console_modifiers_ = true;
    }
    void arm_forced_caps() // MTHAP_STAT_CAPS or console caps: fixed caps, skip AP recompute
    {
        forced_caps_ = true;
    }

    [[nodiscard]] bool enforce_modifiers(bool ap_authenticated) const
    {
        return ap_authenticated || env_modifiers_ || console_modifiers_;
    }
    [[nodiscard]] bool enforce_caps(bool ap_authenticated) const
    {
        return ap_authenticated || forced_caps_;
    }
    [[nodiscard]] bool caps_fixed() const
    {
        return forced_caps_;
    }

  private:
    bool env_modifiers_{false};
    bool console_modifiers_{false};
    bool forced_caps_{false};
};

} // namespace mth
