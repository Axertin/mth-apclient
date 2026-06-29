#pragma once

#include <cstddef>

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
inline constexpr std::ptrdiff_t kPickupKilledFlagOff = 0x160; // unsigned; bit 0 = killed

// AP dummy item: itemType 1 (Shop_Exit, dead data) patched to kind 0 with sprite
// assets borrowed from donor row 40 (kItemType_Treasure_Smallest).
inline constexpr int kApDummyItemType = 1;
inline constexpr int kDummyAssetDonor = 40;

// BossComponent (verified by the index-range self-check in the death funnels).
inline constexpr std::ptrdiff_t kBossIndexOff = 0x68;

// KeyBlock / SaveSlot lock bits.
inline constexpr std::ptrdiff_t kKeyBlockSlotOff = 0x2d0;     // int: cached slot, -1 = name-scan (non-PairLock)
inline constexpr std::ptrdiff_t kKeyBlockEntityRefOff = 0xa8; // start of the +0xa8 -> +0x40 -> +0xd0 name-key chain
inline constexpr std::ptrdiff_t kSaveBlockUnlockOff = 0x200;  // u64 lock-unlocked bitfield in SaveSlot
inline constexpr std::ptrdiff_t kSaveKearBitsOff = 0x1f0;     // u64 kear-collected bitfield in SaveSlot
inline constexpr std::ptrdiff_t kSaveKearSpentOff = 0x1f8;    // int spent-counter; usable keys = popcount(SaveSlot+0x1f0) - this
// NOTE: no Player-side key mirror exists. Player+0x1190/+0x1198 are the money current/cap fields
// (see Player::AddMoney); the game reads usable keys from the SaveSlot above, never from the Player.

// Goal-completion SaveSlot state (polled; the bitfields are popcounted for the count goals).
inline constexpr std::ptrdiff_t kSaveBossDefeatedBitsOff = 0x280; // u64 boss-defeated bitfield (BossComponent::GetDefeatedCount popcounts this)
inline constexpr std::ptrdiff_t kSaveGeneratorBitsOff = 0x290;    // u64 generator-fixed bitfield (BossComponent::SetGeneratorFixed sets a bit per generator)
inline constexpr std::ptrdiff_t kSaveGameClearOff = 0xd30;        // u8 game-cleared flag (set by GigaLionelBoss::EndingTransition)

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
inline constexpr std::ptrdiff_t kPlayerLowRoofFlagOff = 0x12f0; // carry-disabled "low roof" pose flag (#37)

} // namespace mth::layout
