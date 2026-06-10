#include "mth/modifier_hooks.hpp"

#include <cstdint>
#include <utility>

#include "mth/core/modifier_table.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

ModifierHooks::ModifierHooks(ModifierRequest request)
{
    forced_ = std::move(request.forced);
    for (int idx : request.indices)
    {
        if (is_safe(idx) || forced_.count(idx) != 0)
            enforced_.insert(idx);
        else
            pal::logf(pal::LogLevel::Warn, "modifiers: idx=%d is deny-list (%s); ignored (prefix force: to override)", idx,
                      class_of(idx) == CheatClass::Grant        ? "grant"
                      : class_of(idx) == CheatClass::Combo      ? "combo"
                      : class_of(idx) == CheatClass::Randomizer ? "randomizer"
                                                                : "invalid");
    }
    armed_ = !enforced_.empty();

    if (!pal::modifiers_available())
    {
        pal::logf(pal::LogLevel::Warn, "modifiers: PAL unavailable; ModifierHooks inert");
        return;
    }
    pal::set_new_game_modifier_seed([this](int slot, std::uint32_t *w) { seed(slot, w); });
    pal::set_modifier_lockdown([this](int idx) { return block(idx); });
    installed_ = true;
    pal::logf(pal::LogLevel::Info, "modifiers: ModifierHooks installed (enforced=%zu armed=%d)", enforced_.size(), static_cast<int>(armed_));
}

ModifierHooks::~ModifierHooks()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        armed_ = false; // disarm first so any in-flight seed/block bails before the hooks are removed
    }
    if (installed_)
        pal::remove_modifier_hooks(); // synchronous removal, same teardown contract as DeathHooks/RandoHooks
    std::lock_guard<std::mutex> lk(mtx_);
    enforced_.clear();
    pending_live_.clear();
}

void ModifierHooks::seed(int slot_index, std::uint32_t words[8])
{
    if (!enforce_live_.load())
        return; // not an AP session (or test mode): never touch a save the mod doesn't own
    std::lock_guard<std::mutex> lk(mtx_);
    if (!armed_)
        return; // disarmed: leave the player's mask untouched
    if (ap_scoped_.load())
    {
        // AP session: enforce only the AP game's slot (captured on the first load), never a vanilla one.
        if (ap_slot_ >= 0 && slot_index != ap_slot_)
        {
            pal::logf(pal::LogLevel::Info, "modifiers: load slot_index=%d != AP slot %d; not enforcing", slot_index, ap_slot_);
            return;
        }
        if (ap_slot_ < 0 && slot_index >= 0)
        {
            ap_slot_ = slot_index;
            pal::logf(pal::LogLevel::Info, "modifiers: captured AP-game slot index=%d", ap_slot_);
        }
    }
    // Authoritative for gameplay bits: set exactly the enforced set, clear other gameplay bits,
    // never touch cosmetic bits.
    for (int idx = 0; idx < kCheatCount; ++idx)
    {
        if (!is_gameplay(idx))
            continue;
        const std::uint32_t bit = 1u << (static_cast<unsigned>(idx) & 31u);
        if (enforced_.count(idx) != 0)
            words[idx >> 5] |= bit;
        else
            words[idx >> 5] &= ~bit;
    }
}

bool ModifierHooks::block(int idx) const
{
    if (!enforce_live_.load())
        return false; // vanilla play (not connected): leave the player's cheat menu alone
    std::lock_guard<std::mutex> lk(mtx_);
    return armed_ && is_gameplay(idx);
}

void ModifierHooks::set_enforce_live(bool on)
{
    enforce_live_.store(on);
}

void ModifierHooks::set_ap_scoped(bool on)
{
    ap_scoped_.store(on);
}

void ModifierHooks::set_ap_slot(int slot)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (slot >= 0)
        ap_slot_ = slot; // a recorded slot from a prior session; skip capture
}

int ModifierHooks::captured_ap_slot() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return ap_slot_;
}

void ModifierHooks::set_armed(bool on)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        armed_ = on;
    }
    pal::logf(pal::LogLevel::Info, "modifiers: %s", on ? "armed (locked)" : "disarmed (unlocked)");
}

void ModifierHooks::set_live(int idx, bool on)
{
    if (!is_safe(idx)) // only continuous modifiers are reversible/live-settable
    {
        pal::logf(pal::LogLevel::Warn, "modifiers: live set idx=%d refused (not a continuous modifier)", idx);
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    pending_live_.emplace_back(idx, on);
}

void ModifierHooks::set_enforced(ModifierRequest request)
{
    const std::size_t requested = request.indices.size();
    std::size_t kept = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        forced_ = std::move(request.forced);
        enforced_.clear();
        for (int idx : request.indices)
            if (is_safe(idx) || forced_.count(idx) != 0)
                enforced_.insert(idx);
        armed_ = !enforced_.empty();
        kept = enforced_.size();
    }
    if (kept != requested)
        pal::logf(pal::LogLevel::Warn, "modifiers: set_enforced kept %zu of %zu requested (deny-list/duplicate indices dropped)", kept, requested);
    pal::logf(pal::LogLevel::Info, "modifiers: enforced set replaced (n=%zu armed=%d)", kept, static_cast<int>(kept != 0));
}

void ModifierHooks::drain_live()
{
    std::vector<std::pair<int, bool>> batch;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        batch.swap(pending_live_);
    }
    for (auto [idx, on] : batch)
        pal::apply_live_modifier(idx, on);
}

std::vector<std::string> ModifierHooks::status_lines() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> out;
    out.push_back(std::string("modifiers armed: ") + (armed_ ? "yes" : "no"));
    std::string list;
    for (int idx : enforced_)
        list += std::to_string(idx) + " ";
    out.push_back("enforced: " + (list.empty() ? std::string("(none)") : list));
    return out;
}

} // namespace mth
