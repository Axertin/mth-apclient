#include "mth/features/ability_hooks.hpp"

#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

AbilityHooks::AbilityHooks(std::function<bool(std::int64_t item_id)> is_granted) : is_granted_(std::move(is_granted))
{
    g_save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (g_save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "AbilityHooks: g_saveManager not resolved; train enforcement disabled");

    const bool installed = pal::install_ability_hooks([this](int ordinal)
                                                      { return gate_.blocked(static_cast<Ability>(ordinal), AbilityGate::GrantQuery{enforce_, is_granted_}); });
    if (!installed)
        pal::logf(pal::LogLevel::Warn, "AbilityHooks: no ability chokepoints resolved; gating disabled");

    // Track the burrow lifetime from the two commit sites the mod already hooks, so the boundary poll knows
    // which mode the player is in without reading the mode field (its offset drifts between builds) (#163).
    pal::set_burrow_observers([this](bool deep) { burrow_.arm(deep); }, [this] { burrow_.disarm(); });
}

AbilityHooks::~AbilityHooks()
{
    pal::remove_ability_hooks();
}

void AbilityHooks::set_randomized(Ability a, bool on)
{
    gate_.set_randomized(a, on);
}

void AbilityHooks::set_enforce(bool on)
{
    enforce_ = on;
}

void AbilityHooks::set_train_gate(bool rando_active, std::uint32_t line_mask)
{
    train_rando_active_ = rando_active;
    train_mask_ = line_mask;
    pal::set_train_destination_gate(line_mask, rando_active); // published to the OnNPCEvent detour
}

void AbilityHooks::set_ticket_machine(bool is_ap_location, bool checked, std::uint32_t progress_seed)
{
    // A machine that is a live check keeps its prompt; one that is not, or has already been donated to,
    // loses it. Reporting the location collected instead would read as "the pass exists" to the conductor,
    // which activates it without the pass ever being granted.
    ticket_machine_suppress_ = !is_ap_location || checked;
    ticket_machine_seed_ = progress_seed;
}

void AbilityHooks::enforce_train_tick()
{
    // train_rando: boarding requires the generic Train Pass (#98), and each destination is gated on its AP
    // ticket - the +0x1e0 clamp undoes the footfall auto-unlock (box UX), while the OnNPCEvent detour (fed
    // by set_train_gate) refuses the warp for un-granted lines. Otherwise fall back to the whole-train
    // ability gate (console-driven Train ability), which hides the conductor while blocked.
    if (train_rando_active_)
    {
        pal::enforce_train_boarding(g_save_manager_);
        pal::enforce_train_destinations(g_save_manager_, train_mask_);
        // The donation machine hands out that same pass (#162): while it is a live check the seed makes it
        // affordable, and once it is not the gate stops it prompting at all.
        //
        // The two are deliberately gated differently. The seed is a DURABLE write, so it waits for the bound
        // AP save - seeding an unrelated one would leave a near-free train pass behind in vanilla play. The
        // disable is in-memory and dies with the room, and failing to apply it is the worse outcome of the
        // two: an interactible machine takes 10000 bones and (via the backstop) returns nothing. So it runs
        // unbound on purpose. Do not "even these up".
        if (ticket_machine_suppress_)
        {
            ticket_machine_.tick();
        }
        else if (enforce_)
        {
            pal::seed_ticket_machine_progress(g_save_manager_, ticket_machine_seed_);
        }
        else if (!warned_seed_withheld_)
        {
            // Otherwise slot_data's train_pass_cost is silently ignored and the machine keeps asking vanilla
            // price, with nothing in the log to say why.
            warned_seed_withheld_ = true;
            pal::logf(pal::LogLevel::Warn, "train: donation cost left at vanilla; the live save is not the bound AP slot (#162)");
        }
    }
    else
    {
        pal::enforce_train_presence(g_save_manager_, gate_.blocked(Ability::Train, AbilityGate::GrantQuery{enforce_, is_granted_}));
    }
}

void AbilityHooks::enforce_burrow_tick(void *player)
{
    // Any change of player, or none at all, drops the arm: acting on a player who is not burrowing is worse
    // than the sequence break this closes.
    if (player == nullptr || player != last_player_)
    {
        last_player_ = player;
        burrow_.disarm();
        return;
    }
    if (mod::pause_api_available() && mod::world_is_paused())
        return; // frozen frame: hold the confirmation rather than advancing it on a stale reading
    const bool burrowing = pal::player_is_burrowing(player);
    burrow_.observe_burrowing(burrowing);
    if (!burrowing || !enforce_ || !burrow_.armed())
        return;
    const int water = pal::burrow_water_state(player);
    if (water < 0)
    {
        burrow_.reading_unavailable();
        return;
    }

    const AbilityGate::GrantQuery q{enforce_, is_granted_};
    switch (burrow_.observe(water == 1, gate_.blocked(Ability::Burrow, q), gate_.blocked(Ability::Swim, q)))
    {
    case BoundaryAction::FallIn:
        pal::logf(pal::LogLevel::Info, "abilities: burrowed into deep water without Swim -> falling in (%d)", pal::request_deep_water_fall(player) ? 1 : 0);
        break;
    case BoundaryAction::Emerge:
        pal::logf(pal::LogLevel::Info, "abilities: swam onto land without Burrow -> surfacing (%d)", pal::force_burrow_emerge(player) ? 1 : 0);
        break;
    case BoundaryAction::None:
        break;
    }
}

void AbilityHooks::on_world_destroy()
{
    burrow_.disarm();
    last_player_ = nullptr;
    ticket_machine_.on_world_destroy(); // the next room's machine should not stay live for a walk cadence
}

} // namespace mth
