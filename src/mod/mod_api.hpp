#pragma once

#include <cstdint>

struct MinaModAPI; // forward decl; MinaModAPI.h is included only in mod_api.cpp

namespace mod
{

// Store the live MinaModAPI* (from MinaMod_Init) or a fake (in tests). nullptr clears it.
void set_api(MinaModAPI *api);

// Game revision ("r-number") via MinaModAPI::GetGameRevision; 0 if unavailable.
std::uint32_t game_revision();

// Items::IsItemCollected override via the native "IsItemCollected" mod hook. Capacity-upgrade locations
// (itemTypes 0x44..0x48) read the same SaveSlot bitfield apply_upgrades repurposes as a capacity counter,
// so a vanilla collected-bit query for one reports "an upgrade was received" -- making boss rose-reward
// spawns (gated on !IsItemCollected(rewardLoc)) wrongly skip (issue #8). query(loc_idx, ownership_query)
// returns -1 to pass through to the game, or 0/1 to force the result. ownership_query is IsItemCollected's
// param5 (b5): true when the caller asks "do I persistently own this item" (the weapon-swap chest), false
// for location-collected queries (chest-open, pickup self-kill, boss reward-rose) -- the mth-side query
// needs it to avoid hiding a received weapon from the swap chest. Fires on both platforms and from every
// inlined copy of IsItemCollected (e.g. the MSVC-inlined Pickup-ctor self-kill on Windows), which the old
// symbol/sig detour could not.
using ItemCollectedFn = int (*)(int loc_idx, bool ownership_query);
bool install_item_collected_hook(ItemCollectedFn query);
void remove_item_collected_hook();

// World::Update pre-tick via the native "WorldUpdate" mod hook. Fires at the top of World::Update -- the
// pre-update spawn window where grants/lock-seeds must happen to avoid update-queue hangs -- so it replaces
// the old sig-detour's pre-hook (the old post-hook was an unused no-op). on_pre runs on the game thread.
// false if the modding API is unavailable.
using WorldUpdatePreFn = void (*)();
bool install_world_update_hook(WorldUpdatePreFn on_pre);
void remove_world_update_hook();

// World teardown via the native "WorldDestroy" mod hook. Fires when a World is destroyed (exit-to-menu,
// save reload, shutdown), so cached per-world game pointers (e.g. the live Player*) can be dropped before
// the game frees them. A stale one is a use-after-free the next time a tick writes through it. on_destroy
// runs on the game thread. false if the modding API is unavailable.
using WorldDestroyFn = void (*)();
bool install_world_destroy_hook(WorldDestroyFn on_destroy);
void remove_world_destroy_hook();

// Vial (health-flask) capacity and held count via the native MinaModAPI Player accessors. These are
// offset-free and self-resolving: the game reads its own current-Player and active-SaveSlot globals, so we
// pass only an int. Capacity writes are durable (they set the underlying vial bitfield UpdateStats re-reads),
// which is why vials go through here instead of a raw SaveSlot offset that drifts between builds. All no-op
// until a player exists. available() is false when the build's API lacks the vial setters.
bool vial_api_available();
int player_max_vials();
int player_vials();
void set_player_max_vials(int n);
void set_player_vials(int n);

// Kill the player via the native MinaModAPI PlayerDie (deathlink apply). Offset-free and cross-platform,
// replacing the old Player::TriggerDeath sig detour. Returns false (no-op) if the modding API or PlayerDie
// is unavailable.
bool player_die();

// Native Player health, used by deathlink to re-arm the broadcast only on a true respawn (health > 0) --
// the death-guard byte pulses during the death sequence, health does not. available() is false when the
// build's API lacks the getter; player_health() returns 0 then.
bool health_api_available();
float player_health();

// modHookCtx for "IsItemCollected"; the layout MUST mirror the game's struct exactly. The game calls
// RunHooks("IsItemCollected", &ctx) at the top of Items::IsItemCollected.
struct IsItemCollectedCtx
{
    void *collection;             // ItemCollection*
    void *save_slot;              // SaveSlot*
    std::int32_t index;           // location index
    bool include_pawn_shop;       // param4
    bool include_early_collected; // param5: the "ownership query" flag (weapon-swap chest reads true)
    bool mod_handled;             // out: true => game returns mod_ret_val instead of its own logic
    bool mod_ret_val;             // out
};

} // namespace mod
