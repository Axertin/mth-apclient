// Detours that suppress or force a menu-adjacent behavior and read nothing platform-specific: the pawn-shop
// NPC veto, the fountain bulb pre-light, the title StartGame backstop, and the new-file starting-kit zeroing.
// The TitleScreen::UpdateState gate stays in the platform files, because Windows first has to walk back from
// the secondary state-machine base its detour is handed.

#include <cstddef>
#include <cstdint>
#include <utility>

#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/fountain_lamps.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

// Pawnty (PawnShopNPC::OnNPCEvent) disable. event 0x1f is InteractComponent::IsInteractable's veto
// query: a nonzero float at info+0x8 means "not interactable" -> no prompt. No-op everything else so
// no dialogue line is set and the sell menu never opens.
constexpr unsigned kPawnInteractableQueryEvent = 0x1f;
constexpr std::ptrdiff_t kPawnVetoFloatOff = 0x8; // InteractEventInfo veto float
pal::PawnShopBlockFn g_pawn_disable;
pal::HookId g_pawn_hook = pal::kInvalidHookId;
void (*g_orig_pawn_npc)(void *, unsigned, void *) = nullptr;

void repl_pawn_npc(void *self, unsigned event, void *info)
{
    if (g_pawn_disable && g_pawn_disable())
    {
        if (event == kPawnInteractableQueryEvent && info != nullptr)
            *reinterpret_cast<float *>(static_cast<char *>(info) + kPawnVetoFloatOff) = 1.0f;
        return; // swallow dialogue/menu/can-interact; never call the original
    }
    if (g_orig_pawn_npc)
        g_orig_pawn_npc(self, event, info);
}

// HubFountain::Bulb::Update(float dt, bool lit): forces lit=true for AP-granted generator lamps.
// Prototype order (self, dt, lit) matches the mangled Update(float,bool) on both ABIs; never reorder.
pal::FountainLampFn g_fountain_mask_fn;
pal::HookId g_fountain_hook = pal::kInvalidHookId;
void (*g_orig_bulb_update)(void *, float, bool) = nullptr;

void repl_bulb_update(void *self, float dt, bool lit)
{
    if (g_fountain_mask_fn && self != nullptr)
    {
        const std::uint32_t idx = *reinterpret_cast<std::uint32_t *>(static_cast<char *>(self) + mth::layout::kBulbIndexOff);
        if (idx < static_cast<std::uint32_t>(mth::kGeneratorLampCount) && ((g_fountain_mask_fn() >> idx) & 1u))
            lit = true; // force this generator lamp lit; never writes SaveSlot+0x290
    }
    if (g_orig_bulb_update)
        g_orig_bulb_update(self, dt, lit);
}

// TitleScreen::StartGame(): backstop to the cursor gate, which stays per-platform. UpdateState performs the cursor write
// AND the confirm dispatch in the same call, so correcting the cursor after the original returns is
// too late once StartGame has already run; suppress it outright while disconnected instead.
pal::StartGameSuppressFn g_start_game_suppress_fn;
pal::HookId g_start_game_hook = pal::kInvalidHookId;
void (*g_orig_title_start_game)(void *) = nullptr;

void repl_title_start_game(void *self)
{
    if (g_start_game_suppress_fn && g_start_game_suppress_fn())
        return; // disconnected: the option is not selectable, so the vanilla path must not run
    if (g_orig_title_start_game)
        g_orig_title_start_game(self);
}

// ---- new-file starting-kit suppression. MSVC's SaveSlot layout matches Linux on depot_1875582, so the
// upgrade-field offsets are identical (verified against Windows SetItemCollected's case map). ----
constexpr std::ptrdiff_t kSparkUpgOff = 0x54;    // Spark_Upgrade   (itemType 0x46)
constexpr std::ptrdiff_t kHealthUpgOff = 0x130;  // Health_Upgrade  (itemType 0x45) bitfield (0xff = 8)
constexpr std::ptrdiff_t kMagicUpgOff = 0x170;   // Magic_Upgrade   (itemType 0x44)
constexpr std::ptrdiff_t kTrinketUpgOff = 0x950; // Trinket_Upgrade (itemType 0x48)
// Vials are not zeroed here: their bitfield offset drifts (#97). App::enforce_vial_capacity re-asserts the
// AP vial count via the mod API each tick instead, which also survives the run re-seeding the base (#171).

pal::NewfileKitSuppressFn g_kit_suppress = nullptr;
pal::HookId g_kit_hook = pal::kInvalidHookId;
void (*g_orig_save_slot_clear)(void *, bool) = nullptr;
void repl_save_slot_clear(void *self, bool arg)
{
    if (g_orig_save_slot_clear)
        g_orig_save_slot_clear(self, arg);
    if (self == nullptr || !g_kit_suppress || !g_kit_suppress())
        return;
    auto field = [self](std::ptrdiff_t off) -> std::uint32_t & { return *reinterpret_cast<std::uint32_t *>(static_cast<char *>(self) + off); };
    pal::logf(pal::LogLevel::Info, "newfile-kit: zeroing default upgrades (health=%#x magic=%#x spark=%#x trinket=%#x)", field(kHealthUpgOff),
              field(kMagicUpgOff), field(kSparkUpgOff), field(kTrinketUpgOff));
    field(kHealthUpgOff) = 0;
    field(kMagicUpgOff) = 0;
    field(kSparkUpgOff) = 0;
    field(kTrinketUpgOff) = 0;
}

} // namespace

namespace pal
{

bool install_pawn_shop_hook(PawnShopBlockFn disable)
{
    g_pawn_disable = std::move(disable);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::pawn_shop_on_npc_event);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "pawnty: PawnShopNPC::OnNPCEvent not resolved; pawn-shop disable off");
        g_pawn_disable = nullptr;
        return false;
    }
    g_pawn_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_pawn_npc), reinterpret_cast<void **>(&g_orig_pawn_npc));
    if (g_pawn_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "pawnty: failed to hook PawnShopNPC::OnNPCEvent");
        g_pawn_disable = nullptr;
        return false;
    }
    logf(LogLevel::Info, "pawnty: hooked PawnShopNPC::OnNPCEvent (id=%llu)", static_cast<unsigned long long>(g_pawn_hook));
    return true;
}

void remove_pawn_shop_hook()
{
    if (g_pawn_hook != kInvalidHookId)
        hook_engine().remove_hook(g_pawn_hook);
    g_pawn_hook = kInvalidHookId;
    g_pawn_disable = nullptr;
}

bool install_fountain_lamp_hook(FountainLampFn lit_mask)
{
    g_fountain_mask_fn = std::move(lit_mask);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::hub_fountain_bulb_update);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "fountain: HubFountain::Bulb::Update not resolved; lamp pre-light off");
        g_fountain_mask_fn = nullptr;
        return false;
    }
    g_fountain_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_bulb_update), reinterpret_cast<void **>(&g_orig_bulb_update));
    if (g_fountain_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "fountain: failed to hook HubFountain::Bulb::Update");
        g_fountain_mask_fn = nullptr;
        return false;
    }
    logf(LogLevel::Info, "fountain: hooked HubFountain::Bulb::Update (id=%llu)", static_cast<unsigned long long>(g_fountain_hook));
    return true;
}

void remove_fountain_lamp_hook()
{
    if (g_fountain_hook != kInvalidHookId)
        hook_engine().remove_hook(g_fountain_hook);
    g_fountain_hook = kInvalidHookId;
    g_fountain_mask_fn = nullptr;
}

bool install_start_game_suppress_hook(StartGameSuppressFn suppress)
{
    g_start_game_suppress_fn = std::move(suppress);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::title_screen_start_game);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "title: TitleScreen::StartGame not resolved; start-game backstop off");
        g_start_game_suppress_fn = nullptr;
        return false;
    }
    g_start_game_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_title_start_game),
                                                   reinterpret_cast<void **>(&g_orig_title_start_game));
    if (g_start_game_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "title: failed to hook TitleScreen::StartGame");
        g_start_game_suppress_fn = nullptr;
        return false;
    }
    logf(LogLevel::Info, "title: hooked TitleScreen::StartGame (id=%llu)", static_cast<unsigned long long>(g_start_game_hook));
    return true;
}

void remove_start_game_suppress_hook()
{
    if (g_start_game_hook != kInvalidHookId)
        hook_engine().remove_hook(g_start_game_hook);
    g_start_game_hook = kInvalidHookId;
    g_start_game_suppress_fn = nullptr;
}

bool install_newfile_kit_suppressor(NewfileKitSuppressFn should_suppress)
{
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::save_slot_clear);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "newfile-kit: SaveSlot::Clear not resolved; starting-kit suppression disabled");
        return false;
    }
    g_kit_suppress = std::move(should_suppress);
    g_kit_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_save_slot_clear),
                                            reinterpret_cast<void **>(&g_orig_save_slot_clear));
    if (g_kit_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "newfile-kit: failed to hook SaveSlot::Clear");
        g_kit_suppress = nullptr;
        return false;
    }
    logf(LogLevel::Info, "newfile-kit: hooked SaveSlot::Clear (id=%llu)", static_cast<unsigned long long>(g_kit_hook));
    return true;
}

void remove_newfile_kit_suppressor()
{
    if (g_kit_hook != kInvalidHookId)
        hook_engine().remove_hook(g_kit_hook);
    g_kit_hook = kInvalidHookId;
    g_kit_suppress = nullptr;
}

} // namespace pal
