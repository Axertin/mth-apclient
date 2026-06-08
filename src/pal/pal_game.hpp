#pragma once

#include <cstdint>

namespace pal
{

// World-space player position from a PlayerTrackable*. false if unavailable.
// Windows reads base fields directly; calling the real GetPos faults off its own call sites.
bool read_player_position(const void *trackable, float out[3]);

// Pickup* base from the `this` passed to a Pickup::OnPickup hook.
// MSVC: that `this` is the PickupListener MI subobject, not the Pickup base.
void *pickup_base_from_onpickup(void *onpickup_this);

// Active SaveSlot* from the resolved g_saveManager global. Linux derefs +0x18; on Windows the
// global already holds the SaveSlot* directly. Returns nullptr if the global is 0.
void *active_save_slot(std::uintptr_t save_manager_global);

// Shop-purchase callback: given the bought slot, runs the AP collect logic and returns the itemType
// the game should store (a dummy to suppress the vanilla grant where the platform redirects, else
// the original itemType unchanged).
using ShopBuyFn = int (*)(int loc_idx, int item_type);

// Install the shop-purchase detection hook. The platform owns which game function it hooks
// (ShopMenu::ItemPresent on Linux, ShopMenu::InitState on Windows), the field layout, and the
// commit detection; on a committed buy it invokes on_buy. Returns false if not installed.
bool install_shop_purchase_hook(ShopBuyFn on_buy);
void remove_shop_purchase_hook();

} // namespace pal
