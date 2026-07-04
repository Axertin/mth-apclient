#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mth/core/ap/ap_state.hpp" // ConnectionPhase
#include "mth/core/data/ability_ids.hpp"

namespace mth
{

struct ConnectionStatus
{
    ConnectionPhase phase{ConnectionPhase::Disconnected};
    std::string detail; // error/status text for display
};

// Console effect interface. Implemented by App; called on the render thread; must not block.
class ICommandSink
{
  public:
    virtual ~ICommandSink() = default;

    virtual void connect(const std::string &server, const std::string &slot, const std::string &password) = 0;
    virtual void disconnect() = 0;
    [[nodiscard]] virtual ConnectionStatus connection_status() const = 0;

    [[nodiscard]] virtual std::vector<std::string> status_lines() const = 0;
    [[nodiscard]] virtual std::vector<std::string> item_lines() const = 0;

    virtual void give_item(std::int64_t ap_item_id) = 0;                       // manual test path; bypasses dedup
    virtual void remove_lock(int slot) = 0;                                    // pre-open/remove a KeyBlock by slot (AP runtime path)
    virtual void set_modifier(int idx, bool on) = 0;                           // live toggle a continuous modifier
    virtual void lock_modifiers(bool armed) = 0;                               // arm/disarm gameplay-modifier lockdown
    virtual void set_stat_caps(int attack, int defense, int sidearm) = 0;      // force per-stat level caps (offline test)
    virtual void set_ability_randomized(Ability ability, bool randomized) = 0; // offline test: mark randomized + arm enforcement
    virtual void enable_deathlink(bool on) = 0;                                // enable/disable deathlink
};

} // namespace mth
