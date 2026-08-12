#pragma once

#include <cstddef>
#include <cstdint>

// Build-specific struct offsets/strides for the Linux game binary, shared by the hook
// modules. Windows divergence stays in the PAL impls. Where a runtime self-check guards
// an offset, it is noted; update this file first when the game updates.
namespace mth::layout
{

// s_rItems: per-itemType row table.
inline constexpr int kItemTypeCount = 195;
inline constexpr int kItemEntryStride = 0x68;
inline constexpr std::ptrdiff_t kItemKindOff = 0x28;    // storage-kind (int)
inline constexpr std::ptrdiff_t kItemAtlasOff = 0x30;   // icon atlas (char*)
inline constexpr std::ptrdiff_t kItemAnimOff = 0x38;    // anim name (char*)
inline constexpr std::ptrdiff_t kItemPaletteOff = 0x58; // palette (char*)

// s_rItemCollection: per-location row table.
inline constexpr int kLocationCount = 361;
inline constexpr int kCollectionEntryStride = 0x50;
inline constexpr std::ptrdiff_t kCollectionNameKeyOff = 0x00;   // u64 name-hash key
inline constexpr std::ptrdiff_t kCollectionItemTypeOff = 0x18;  // int vanilla itemType
inline constexpr std::ptrdiff_t kCollectionBitIdxOff = 0x1c;    // u8 unlock-bit index within SaveSlot u64
inline constexpr std::ptrdiff_t kCollectionWarpRemapOff = 0x4c; // int warp remap (<0 = none)
inline constexpr int kCollectionScanCap = 0x168;                // name-scan upper bound (SetSaveUnlocked mirror)

// Component/entity idiom: ycEntity* at component+0x10, ycWorld* at entity+0x50.
inline constexpr std::ptrdiff_t kComponentEntityOff = 0x10;
inline constexpr std::ptrdiff_t kEntityWorldOff = 0x50;

// Pickup entity (verified by the startup self-check in the Pickup::Init hook).
inline constexpr std::ptrdiff_t kPickupLocIdxOff = 0x380;
inline constexpr std::ptrdiff_t kPickupItemTypeOff = 0x384;
inline constexpr std::ptrdiff_t kPickupKilledFlagOff = 0x160;      // unsigned; bit 0 = killed
inline constexpr std::ptrdiff_t kPickupSaveTrackedFlagOff = 0x3ac; // u8; nonzero = collected-state self-kill gate armed (#93)

// AP dummy item: itemType 1 (Shop_Exit, dead data) patched to kind 0 with sprite
// assets borrowed from donor row 40 (kItemType_Treasure_Smallest).
inline constexpr int kApDummyItemType = 1;
inline constexpr int kDummyAssetDonor = 40;

// The only two item types that reach Player::AddVialUpgrade, which ORs bits into the durable vial
// bitfield (#171). HealingVialFirst is the one-time story grant and ORs the whole 3-bit base.
inline constexpr int kItemHealingVialFirst = 0x12;
inline constexpr int kItemVialUpgrade = 0x47;

// KeyBlock / SaveSlot lock bits.
inline constexpr std::ptrdiff_t kKeyBlockSlotOff = 0x2d0;     // int: cached slot, -1 = name-scan (non-PairLock)
inline constexpr std::ptrdiff_t kKeyBlockEntityRefOff = 0xa8; // start of the +0xa8 -> +0x40 -> +0xd0 name-key chain
inline constexpr std::ptrdiff_t kSaveBlockUnlockOff = 0x200;  // u64 lock-unlocked bitfield in SaveSlot
inline constexpr std::ptrdiff_t kSaveKearBitsOff = 0x1f0;     // u64 kear-collected bitfield in SaveSlot
inline constexpr std::ptrdiff_t kSaveKearSpentOff = 0x1f8;    // int spent-counter (see kPlayerKearSpentOff for the mirror)
// The Player DOES mirror the kear counters: Player+0x11a8 (u64 bits) / Player+0x11b0 (int spent), synced
// by Player::RestoreSave/WriteSave. Gameplay (KeyBlock/Chest gate, HUD) reads usable = popcount(Player+0x11a8)
// - SaveSlot+0x1f8; the spend gate then SNAPS SaveSlot+0x1f8 = Player+0x11b0+1, so any edit to the key count
// must move BOTH SaveSlot+0x1f8 and Player+0x11b0 together. (Player+0x119c/+0x11a0 are the money
// current/cap fields; Player+0x1190 is the vial capacity bitmask.)
inline constexpr std::ptrdiff_t kPlayerKearBitsOff = 0x11a8;  // u64 kear-collected bitfield mirror in Player
inline constexpr std::ptrdiff_t kPlayerKearSpentOff = 0x11b0; // int spent-counter mirror in Player (runtime source of truth)

// Goal-completion SaveSlot state (polled; the bitfields are popcounted for the count goals).
inline constexpr std::ptrdiff_t kSaveGeneratorBitsOff = 0x290; // u64 generator-fixed bitfield (BossComponent::SetGeneratorFixed sets a bit per generator)
inline constexpr std::ptrdiff_t kSaveGameClearOff = 0xd30;     // u8 game-cleared flag (set by GigaLionelBoss::EndingTransition)

// Chosen starting weapon (the SaveSlot constructor writes -1 = none). Weapons::GetStarterReplacement keys
// its collection-slot remap on this, so anything but -1 rewrites that weapon's tier-3 slot to the whip's
// (16). Weapon OWNERSHIP is elsewhere (+0xc24/+0xc38 bits, name string at +0x138), so clearing this costs
// the player nothing.
inline constexpr std::ptrdiff_t kSaveStarterWeaponTypeOff = 0xc60; // int: starter weapon type, -1 = no swap

// Weapon ownership, one entry per family (Whip, Hammer, Daggers, Buster Bat, Casket). +0xc24 is the owned-tier
// bitfield Items::IsItemCollected reads back for a kind-1 item, bit (itemType - 2) % 3 per tier; +0xc38 is the
// active tier, a BIT INDEX into it and not a count (the game revokes with `0xc24[fam] &= ~(1 << (0xc38[fam] &
// 0x1f))`). Only three tiers exist, so nothing above bit 2 belongs to us.
inline constexpr std::ptrdiff_t kSaveWeaponOwnedBitsOff = 0xc24;  // u32[5]: per-family owned-tier bitfield
inline constexpr std::ptrdiff_t kSaveWeaponActiveTierOff = 0xc38; // int[5]: per-family active tier (a bit index into the above)
inline constexpr std::uint32_t kWeaponTierBits = 0x7;             // the three tier bits; any other bit of the field is left alone

// WeaponMerchant (Legovich) forge mold (#67): the pending weapon-upgrade index; -1 = none.
inline constexpr std::ptrdiff_t kSaveWeaponIndexOff = 0xc70;     // int: pending weapon index the forge keys on
inline constexpr std::ptrdiff_t kSaveWeaponMoldLatchOff = 0xc74; // byte: "mold pending pickup" latch

// KeyBlockChain (multi-block lock). Build-drift; opened by req-state 2 + commit. Slot resolves from
// the chain's SpawnPoint name-key (no cached slot).
inline constexpr std::ptrdiff_t kChainSpawnPointOff = 0x1c0;   // SpawnPoint* the chain gates (0 if none found)
inline constexpr std::ptrdiff_t kSpawnPointNameKeyOff = 0xd0;  // u64 name-hash on the SpawnPoint (fallback: *(sp)+0x28)
inline constexpr std::ptrdiff_t kChainStateCurOff = 0x18c;     // int current state (2 = opening/kill)
inline constexpr std::ptrdiff_t kChainStateReqOff = 0x194;     // int requested next state
inline constexpr std::ptrdiff_t kChainStatePendingOff = 0x198; // u8 transition-pending flag (set 1 to commit)
inline constexpr int kChainOpenState = 2;                      // state whose UpdateState Kills the chain

// Kear-locked Chest. Slot resolves via the +0xa8 -> +0x40 -> +0xd0 SpawnPoint name-key chain (no cached slot).
inline constexpr std::ptrdiff_t kChestLockedFlagOff = 0x265; // u8 (0x101 word): nonzero = needs a kear to open

// Player (deathlink).
inline constexpr std::ptrdiff_t kPlayerDeathGuardOff = 0x1380; // once-per-death guard byte (0 = fresh)

// Player (ability gating).

// HubFountain::Bulb (Ossex fountain lamp pre-light).
inline constexpr std::ptrdiff_t kBulbIndexOff = 0x10; // HubFountain::Bulb+0x10: u32 lamp index (0..6)

// TitleScreen menu. Verified identical on Linux r148821 and Windows r148905, so no PAL split.
// The three options are fixed sub-objects in one block, not an array of entries; per-option state
// is visibility only, so a disabled option has no game-side representation to reuse.
inline constexpr std::ptrdiff_t kTitleOptionBlockOff = 0x120;   // TitleScreen -> option block ptr
inline constexpr std::ptrdiff_t kTitleSelectedIndexOff = 0x160; // int, wrapped 0..2 unconditionally
inline constexpr std::ptrdiff_t kTitleOptionStride = 0x8a0;
inline constexpr int kTitleOptionStartGame = 0;
inline constexpr int kTitleOptionCount = 3;

// ProfileSelectMenu. Verified identical on Linux r148821 and Windows r148905 (Ghidra decompiles the
// Windows one against this+0x38, so add 0x38 to the offsets it reports). Its launch state is the
// only code in the game that sets the launched flag the intro cinematic polls, so a launch that
// bypasses this menu leaves the title screen up and the cutscene parked.
inline constexpr std::ptrdiff_t kProfileMenuStateOff = 0x5c;     // int: current state
inline constexpr std::ptrdiff_t kProfileMenuNextStateOff = 0x64; // int: requested state
inline constexpr std::ptrdiff_t kProfileMenuStateReqOff = 0x68;  // u8: transition-pending flag
inline constexpr std::ptrdiff_t kProfileMenuSlotOff = 0x3fc;     // int: save slot the launch reads
inline constexpr int kProfileMenuLaunchState = 8;                // activates and starts the slot
inline constexpr int kProfileMenuBackState = 2;                  // returns to the title
// Browse state; the only one safe to launch from. The earlier states run while the intro cinematic
// is still in its own scroll state (0x1a) parallaxing backdrop layers out of the resident region.
inline constexpr int kProfileMenuBrowseState = 3;
// Bounds both dispatch tables accept (UpdateState state-1 <= 9, InitState state-2 <= 8).
inline constexpr int kProfileMenuStateMin = 1;
inline constexpr int kProfileMenuStateMax = 10;

// Gamestate ids. Profile-select is a real gamestate the cinematic sets on the way in, so "gameplay
// started" must exclude it as well as the title screen.
inline constexpr int kTitleGameState = 0x54;
inline constexpr int kProfileSelectGameState = 0x69;

// Vanilla SaveSlot array: the files profile-select lists, and what its launch copies into the live
// working slot. The base pointer offset diverges per platform; these two do not.
inline constexpr std::ptrdiff_t kSaveSlotArrayOff = 0x680;
inline constexpr std::ptrdiff_t kSaveSlotStride = 0x11c8;

// TicketMachine (the station's train-pass donation machine) and the InteractComponent it owns.
// InteractComponent::IsInteractable early-returns false on the disable byte, which is what suppresses
// both the on-screen prompt and the interaction itself. It is the game's own on/off switch (the same
// one NPCBehavior_Brainless flips), and nothing in the machine ever writes it - unlike the prompt byte
// at +0x231, which TicketMachine::UpdateState rewrites every tick from IsItemCollected(149).
inline constexpr std::ptrdiff_t kTicketMachineInteractOff = 0x1b0; // TicketMachine -> InteractComponent*
inline constexpr std::ptrdiff_t kInteractDisabledOff = 0x23a;      // u8: nonzero = never interactable

// NPCEntity holds its InteractComponent at the same offset as TicketMachine (shared base). Linux-derived
// only: NPCBehavior_SewerCat is multiply inherited (four vptrs), so MSVC need not place +0x50 where
// Itanium does. Both hops are validated at use, so a Windows mismatch degrades to the gate's sibling
// fallback instead of writing somewhere wrong.
inline constexpr std::ptrdiff_t kSewerCatEntityOff = 0x50;     // NPCBehavior_SewerCat -> NPCEntity*
inline constexpr std::ptrdiff_t kNpcEntityInteractOff = 0x1b0; // NPCEntity -> InteractComponent*

// ShopItem / ShopItemDef. A slot is a chain of ShopItemDef variants (one per level, rising price)
// linked via +0x28, each carrying its own cached loc_idx. Same offsets on both platforms.
inline constexpr std::ptrdiff_t kShopItemDefOff = 0xf8;   // ShopItem -> active ShopItemDef*
inline constexpr std::ptrdiff_t kShopItemStockOff = 0xec; // ShopItem stock count; 0 renders the "sold out" box
inline constexpr std::ptrdiff_t kShopDefLocOff = 0x48;    // ShopItemDef cached GetCollectionIndex == loc_idx
inline constexpr std::ptrdiff_t kShopDefNextOff = 0x28;   // ShopItemDef -> next variant (level chain), null-terminated

// ycTextComponent text coloring. SetColor only stores a color; the render object resolves it as
// outputPalette.colors[ GetIndex(lookupPalette, storedColor) ], and GetIndex answers 0 for a color
// the palette does not contain. The render object is a base subobject at +0x40, so these
// component-relative offsets are its own plus 0x40. Same offsets on both platforms.
inline constexpr std::ptrdiff_t kTextOutputPaletteOff = 0x128;  // ycTextComponent -> ycPaletteTexture* the final RGBA is read from
inline constexpr std::ptrdiff_t kTextLookupPaletteOff = 0x130;  // ycTextComponent -> ycPaletteTexture* GetIndex runs against
inline constexpr std::ptrdiff_t kTextPaletteVersionOff = 0x1c0; // cached output-palette version; a stale value forces a re-resolve

// ycPaletteTexture. Refcount is manipulated by hand because the engine's own SetPalette is inlined
// on Linux and would have to be carved on Windows.
inline constexpr std::ptrdiff_t kPaletteRefCountOff = 0x34;
inline constexpr std::ptrdiff_t kPaletteVersionOff = 0x38;

} // namespace mth::layout
