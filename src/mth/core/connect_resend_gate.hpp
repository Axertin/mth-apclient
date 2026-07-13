#pragma once

namespace mth
{

// One-shot-per-connection latch for the reconnect resend. fire() returns true exactly once per
// connection, on the first call where both `connected` and `inbound_ready` hold, then re-arms when
// `connected` goes false so it fires again on the next reconnect. Order-independent across ticks:
// works whether `connected` or `inbound_ready` becomes true first. Pure; the caller (App) owns one.
class ConnectResendGate
{
  public:
    bool fire(bool connected, bool inbound_ready)
    {
        if (!connected)
        {
            latched_ = false; // re-arm for the next connection
            return false;
        }
        if (latched_ || !inbound_ready)
            return false;
        latched_ = true;
        return true;
    }

  private:
    bool latched_{false};
};

} // namespace mth
