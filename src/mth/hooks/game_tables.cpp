#include "mth/hooks/game_tables.hpp"

#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

std::uintptr_t g_s_r_items = 0;
std::uintptr_t g_s_r_item_collection = 0;
bool g_resolved = false;
bool g_dummy_patched = false;

} // namespace

namespace mth::tables
{

void resolve()
{
    if (g_resolved)
        return;
    g_resolved = true;
    g_s_r_items = pal::resolve_game_symbol(sym::s_r_items);
    if (g_s_r_items == 0)
        pal::logf(pal::LogLevel::Warn, "tables: s_rItems not found; item kind will log as -1 (grants still work)");
    g_s_r_item_collection = pal::resolve_game_symbol(sym::s_r_item_collection);
    if (g_s_r_item_collection == 0)
        pal::logf(pal::LogLevel::Warn, "tables: s_rItemCollection not found; location-kind lookups disabled");
}

bool collection_resolved()
{
    return g_s_r_item_collection != 0;
}

int storage_kind(int item_type)
{
    if (item_type < 0 || item_type >= layout::kItemTypeCount || g_s_r_items == 0)
        return -1; // out of range or unclassifiable
    return *reinterpret_cast<const int *>(g_s_r_items + static_cast<std::uintptr_t>(item_type) * layout::kItemEntryStride + layout::kItemKindOff);
}

int native_location_kind(int loc_idx)
{
    if (loc_idx < 0 || loc_idx >= layout::kLocationCount || g_s_r_item_collection == 0)
        return -1;
    const int item_type = *reinterpret_cast<const int *>(g_s_r_item_collection + static_cast<std::uintptr_t>(loc_idx) * layout::kCollectionEntryStride +
                                                         layout::kCollectionItemTypeOff);
    return storage_kind(item_type);
}

bool is_durable_bit_kind(int kind)
{
    return kind == 8 || kind == 12 || kind == 19;
}

// Name-scan accessors bound at kCollectionScanCap (0x168), intentionally NOT kLocationCount (361): mirrors SetSaveUnlocked's scan.
std::uint64_t collection_name_key(int idx)
{
    if (idx < 0 || idx >= layout::kCollectionScanCap || g_s_r_item_collection == 0)
        return 0;
    return *reinterpret_cast<const std::uint64_t *>(g_s_r_item_collection + static_cast<std::uintptr_t>(idx) * layout::kCollectionEntryStride +
                                                    layout::kCollectionNameKeyOff);
}

int collection_warp_remap(int idx)
{
    if (idx < 0 || idx >= layout::kCollectionScanCap || g_s_r_item_collection == 0)
        return -1;
    return *reinterpret_cast<const int *>(g_s_r_item_collection + static_cast<std::uintptr_t>(idx) * layout::kCollectionEntryStride +
                                          layout::kCollectionWarpRemapOff);
}

std::uint8_t collection_bit_index(int slot)
{
    if (slot < 0 || slot >= layout::kLocationCount || g_s_r_item_collection == 0)
        return 0;
    return *reinterpret_cast<const std::uint8_t *>(g_s_r_item_collection + static_cast<std::uintptr_t>(slot) * layout::kCollectionEntryStride +
                                                   layout::kCollectionBitIdxOff);
}

void repurpose_dummy_item()
{
    if (g_dummy_patched)
        return;
    if (g_s_r_items == 0)
    {
        pal::logf(pal::LogLevel::Warn, "dummy: s_rItems unresolved, AP pickups keep vanilla visual");
        return;
    }
    const std::uintptr_t dst = g_s_r_items + static_cast<std::uintptr_t>(layout::kApDummyItemType) * layout::kItemEntryStride;
    const std::uintptr_t src = g_s_r_items + static_cast<std::uintptr_t>(layout::kDummyAssetDonor) * layout::kItemEntryStride;

    if (!pal::make_writable(reinterpret_cast<void *>(dst), static_cast<std::size_t>(layout::kItemEntryStride)))
    {
        pal::logf(pal::LogLevel::Error, "dummy: make_writable failed; s_rItems[%d] NOT patched", layout::kApDummyItemType);
        return;
    }

    *reinterpret_cast<int *>(dst + layout::kItemKindOff) = 0; // storage-kind None -> no grant
    *reinterpret_cast<const char **>(dst + layout::kItemAtlasOff) = *reinterpret_cast<const char **>(src + layout::kItemAtlasOff);
    *reinterpret_cast<const char **>(dst + layout::kItemAnimOff) = *reinterpret_cast<const char **>(src + layout::kItemAnimOff);
    *reinterpret_cast<const char **>(dst + layout::kItemPaletteOff) = *reinterpret_cast<const char **>(src + layout::kItemPaletteOff);

    g_dummy_patched = true;
    pal::logf(pal::LogLevel::Info, "dummy: s_rItems[%d] -> kind 0, assets from [%d] (atlas=%s anim=%s)", layout::kApDummyItemType, layout::kDummyAssetDonor,
              *reinterpret_cast<const char **>(dst + layout::kItemAtlasOff), *reinterpret_cast<const char **>(dst + layout::kItemAnimOff));
}

} // namespace mth::tables
