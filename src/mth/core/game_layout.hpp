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

// KeyBlockChain (the multi-block lock). All build-drift; guarded by a runtime slot-resolution
// self-check + per-spawn logging in the KeyBlockChain::Update hook. Open = req-state 2 + commit,
// mirroring Chest::Unlock's chain-open. Slot resolves from the chain's associated SpawnPoint.
inline constexpr std::ptrdiff_t kChainSpawnPointOff = 0x1c0;   // SpawnPoint* the chain gates (0 if none found)
inline constexpr std::ptrdiff_t kSpawnPointNameKeyOff = 0xd0;  // u64 name-hash on the SpawnPoint (fallback: *(sp)+0x28)
inline constexpr std::ptrdiff_t kChainStateCurOff = 0x18c;     // int current state (2 = opening/kill)
inline constexpr std::ptrdiff_t kChainStateReqOff = 0x194;     // int requested next state
inline constexpr std::ptrdiff_t kChainStatePendingOff = 0x198; // u8 transition-pending flag (set 1 to commit)
inline constexpr int kChainOpenState = 2;                      // state whose UpdateState Kills the chain

// Player (deathlink).
inline constexpr std::ptrdiff_t kPlayerDeathGuardOff = 0x1380; // once-per-death guard byte (0 = fresh)

} // namespace mth::layout
