#pragma once

#include <cstdint>

struct MinaModAPI; // forward decl; MinaModAPI.h is included only in mod_api.cpp

namespace mod
{

// Store the live MinaModAPI* (from MinaMod_Init) or a fake (in tests). nullptr clears it.
void set_api(MinaModAPI *api);

// Game revision ("r-number") via MinaModAPI::GetGameRevision; 0 if unavailable.
std::uint32_t game_revision();

// Items::IsItemCollected override via the native "IsItemCollected" mod hook. query(loc_idx,
// ownership_query) returns -1 to pass through to the game, or 0/1 to force the result. The hook
// fires on both platforms and from every inlined copy of IsItemCollected.
using ItemCollectedFn = int (*)(int loc_idx, bool ownership_query);
bool install_item_collected_hook(ItemCollectedFn query);
void remove_item_collected_hook();

// World::Update pre-tick via the native "WorldUpdate" mod hook (fires at the top of World::Update,
// before the update queue). on_pre runs on the game thread. false if the modding API is unavailable.
using WorldUpdatePreFn = void (*)();
bool install_world_update_hook(WorldUpdatePreFn on_pre);
void remove_world_update_hook();

// modHookCtx for "IsItemCollected"; layout MUST mirror the game's struct exactly.
struct IsItemCollectedCtx
{
    void *collection;
    void *save_slot;
    std::int32_t index;
    bool include_pawn_shop;
    bool include_early_collected; // param5: the "ownership query" flag
    bool mod_handled;             // out: true => game returns mod_ret_val
    bool mod_ret_val;             // out
};

} // namespace mod
