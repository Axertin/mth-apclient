#pragma once

#include <cstddef>
#include <cstring>

#include "mth/core/data/game_symbols.hpp"

namespace mth::sym
{

// GetSymAddr takes the game's plain source-level name, not the mangled one every other resolution
// path here keys off. Only names the API documents as supported belong in this table; an unlisted
// one is not an error, it falls through to the platform resolver. These addresses come from the game
// itself, so they cannot drift.
struct NativeSymName
{
    const char *mangled;
    const char *plain;
};

inline constexpr NativeSymName kNativeSymNames[] = {
    {s_r_items, "s_rItems"},
    {s_r_item_collection, "s_rItemCollection"},
    {pickup_init, "Pickup::Init"},
    {shop_init_state, "ShopMenu::InitState"},
    {shop_is_out_of_stock, "Shop::IsOutOfStock"},
    {shop_get, "Shop::Get"},
    {shop_set_cursor, "ShopMenu::SetCursor"},
    {activate_save_slot, "SessionManager::ActivateSaveSlot"},
    {toggle_cheat, "CheatManager::ToggleCheat"},
    {set_cheat_applied, "CheatManager::SetCheatApplied"},
    {level_up_menu_update, "LevelUpMenu::Update"},
    {player_set_burrow_ground, "Player::SetBurrowGround"},
    {player_rope_climb_start, "Player::RopeClimbStart"},
    {bounce_plant_collide, "BouncePlant::CollideWith"},
    {bounce_plant_launch, "BouncePlant::BounceLaunch"},
    {spring_bellows_collide, "SpringBellows::CollideWith"},
    {player_pickup_carryable, "Player::PickUpAnyNearbyCarryableObject"},
    {mina_on_burrow_jump, "Mina::OnBurrowJump"},
    {train_authority_on_npc_event, "TrainAuthority::OnNPCEvent"},
    {pawn_shop_on_npc_event, "PawnShopNPC::OnNPCEvent"},
    {hub_fountain_bulb_update, "HubFountain::Bulb::Update"},
};

// Mangled symbol -> a mod hook dispatched from inside that function. The hook's name hash is inlined
// at the dispatch site, which anchors the function without a carved signature. Deriving from the
// hook NAME is what makes it durable: a mask that happens to be unique is not a substitute.
//
// An entry here is worth it whenever the mod needs the ADDRESS, which includes functions it detours
// rather than hooks. Game::FixedUpdate is anchored but still detoured, because the hook fires at the
// top of the function while the tick has to run after the original.
struct HookAnchor
{
    const char *mangled;
    const char *hook_name;
};

inline constexpr HookAnchor kHookAnchors[] = {
    // Its carved signature broke on r149150, which disabled inbound AP grants. Nothing in the API
    // grants an item by type, so the address is still needed to call it.
    {on_pickup_done, "ItemsOnPickupDone"},
    {game_fixed_update, "FixedUpdate"},
};

// Returns the hook name anchoring a mangled symbol, or nullptr when there is none.
[[nodiscard]] inline const char *hook_anchor_for(const char *mangled) noexcept
{
    if (mangled == nullptr)
        return nullptr;
    for (const HookAnchor &a : kHookAnchors)
        if (std::strcmp(a.mangled, mangled) == 0)
            return a.hook_name;
    return nullptr;
}

// Returns the GetSymAddr name for a mangled symbol, or nullptr when the API does not expose it.
[[nodiscard]] inline const char *native_sym_name(const char *mangled) noexcept
{
    if (mangled == nullptr)
        return nullptr;
    for (const NativeSymName &n : kNativeSymNames)
        if (std::strcmp(n.mangled, mangled) == 0)
            return n.plain;
    return nullptr;
}

} // namespace mth::sym
