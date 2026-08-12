#include "mth/features/kear_completion_tracker.hpp"

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/kear_completion.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

KearCompletionTracker::KearCompletionTracker()
{
    save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "kear: g_saveManager not resolved; the kear completion check is disabled");
}

void KearCompletionTracker::evaluate(const ApState &state)
{
    if (save_manager_ == 0 || !state.authenticated())
        return;
    // A seed that does not carry the completion location must never get a stray latch.
    if (!state.is_valid_location(ap_loc_id(kKearCompletionLocIdx)))
        return;
    void *slot = pal::active_save_slot(save_manager_);
    if (slot == nullptr)
        return; // title/menus: nothing durable to write through

    auto &flags = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + layout::kSaveKeyMiserFlagOff);
    const std::uint64_t bit = std::uint64_t{1} << layout::kSaveKeyMiserTradeBit;
    if ((flags & bit) != 0)
        return; // already traded, natively or by an earlier tick

    if (!have_all_kears(state.kear_mode(), state.received_items()))
        return;

    flags |= bit;
    pal::logf(pal::LogLevel::Info, "kear: all kears held; latched the KeyMiser trade flag (the reward spawns on the next entry to the Kear Institute)");
}

} // namespace mth
