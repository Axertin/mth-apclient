// The runtime cheat mirror (CheatManager) behind the modifier traps. It is reached through the mod
// API's g_cheatManager symbol rather than a save-manager offset, so nothing here is per-platform; the
// save-mask half of modifier control stays in the platform files, where the g_saveManager layout differs.

#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/cheat_mask.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

namespace
{

// CheatManager layout (distinct from the SaveSlot mask, which stays per-platform). +0x01 is an "activated" bool that
// every read site checks before consulting the mask, and +0x20 holds a u64[4] covering indices
// 0..63, 64..127, 128..191, 192..255. CheatManager::ActivateSaveCheats writes that byte to 1 at the
// top of its body and is its only writer, so a set byte means the game has activated a save slot.
constexpr std::size_t kCheatMgrActivatedOff = 0x01;
constexpr std::size_t kCheatMgrMaskOff = 0x20;

void *g_cheat_mgr_sym = nullptr;
bool g_cheat_mgr_outcome_logged = false; // one-shot: the manager resolved and validated
bool g_cheat_mgr_miss_logged = false;    // one-shot: symbol not (yet) resolved

// A validated CheatManager, resolved once so a caller needing both the activated byte and the mask
// does not walk +0x20 twice. A null base means there is no usable manager.
struct CheatManagerView
{
    std::uint8_t *base = nullptr;
    std::uint64_t *mask = nullptr;
};

// A candidate CheatManager* is trusted only if it looks like a real, naturally aligned pointer, its
// "activated" byte reads as an actual bool, and the mask pointer it carries at +0x20 is itself a
// valid, aligned pointer.
// pal::pointer_looks_valid is a range test, not a mapping check (pal/pal_mem.hpp), so alignment and
// the layout's own field values catch a candidate that merely landed in the valid range.
std::uint64_t *cheat_mask_at(void *candidate)
{
    const auto addr = reinterpret_cast<std::uintptr_t>(candidate);
    if (!pal::pointer_looks_valid(candidate) || (addr & 7u) != 0)
        return nullptr;
    auto *base = reinterpret_cast<std::uint8_t *>(candidate);
    if (base[kCheatMgrActivatedOff] > 1)
        return nullptr;
    auto *mask = *reinterpret_cast<std::uint64_t **>(base + kCheatMgrMaskOff);
    const auto mask_addr = reinterpret_cast<std::uintptr_t>(mask);
    if (!pal::pointer_looks_valid(mask) || (mask_addr & 7u) != 0)
        return nullptr;
    return mask;
}

// Resolves g_cheatManager through the game's own GetSymAddr. Only a successful resolve is cached:
// a miss is retried on every call, because a caller asking this from its own constructor can race
// the App constructor body publishing the game's text range (mod::sym_addr fails closed until
// then), and caching that transient miss would make traps look permanently unavailable for the rest
// of the session. The "not yet available" log is still one-shot, so a slow race does not spam it.
void ensure_cheat_mgr_symbol()
{
    if (g_cheat_mgr_sym != nullptr)
        return;
    g_cheat_mgr_sym = mod::sym_addr("g_cheatManager");
    if (g_cheat_mgr_sym != nullptr)
    {
        pal::logf(pal::LogLevel::Info, "traps: g_cheatManager resolved");
        return;
    }
    if (!g_cheat_mgr_miss_logged)
    {
        g_cheat_mgr_miss_logged = true;
        pal::logf(pal::LogLevel::Warn, "traps: g_cheatManager NOT available (yet)");
    }
}

// GetSymAddr hands back the CheatManager itself, already dereferenced: the symbol table's entry for
// g_cheatManager loads the global rather than taking its address. The validation below still earns
// its place, since cheat_mask_at is all that stands between a resolve gone wrong and a write into
// arbitrary game memory. The outcome logs once, since a human reads that line in-game.
CheatManagerView cheat_manager()
{
    ensure_cheat_mgr_symbol();
    if (g_cheat_mgr_sym == nullptr)
        return {};
    std::uint64_t *mask = cheat_mask_at(g_cheat_mgr_sym);
    if (mask == nullptr)
        return {};
    if (!g_cheat_mgr_outcome_logged)
    {
        g_cheat_mgr_outcome_logged = true;
        pal::logf(pal::LogLevel::Info, "traps: g_cheatManager resolved (mask at +0x20 looked valid)");
    }
    return {reinterpret_cast<std::uint8_t *>(g_cheat_mgr_sym), mask};
}

} // namespace

namespace pal
{

bool runtime_modifiers_available()
{
    ensure_cheat_mgr_symbol();
    return g_cheat_mgr_sym != nullptr;
}

bool runtime_modifier_ready()
{
    const CheatManagerView mgr = cheat_manager();
    return mgr.base != nullptr && mgr.base[kCheatMgrActivatedOff] != 0;
}

bool set_runtime_modifier(int idx, bool on)
{
    if (idx < 0 || idx >= 254)
        return false;
    const CheatManagerView mgr = cheat_manager();
    if (mgr.base == nullptr)
        return false;
    // Every read site checks this byte before the mask, so a bit written while it is clear does
    // nothing at all. Only the game's own ActivateSaveCheats sets it, so a clear byte means no save
    // is active yet, and the write is worth attempting again only after the next activation.
    if (mgr.base[kCheatMgrActivatedOff] == 0)
        return false;
    mth::cheat_mask_set(mgr.mask, idx, on);
    return true;
}

} // namespace pal
