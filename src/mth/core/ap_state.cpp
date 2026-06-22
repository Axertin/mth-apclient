#include "mth/core/ap_state.hpp"

#include <type_traits>
#include <variant>

#include "pal/pal_log.hpp"

namespace mth
{

void ApState::inject_received_item(std::int64_t item_id)
{
    received_items_.push_back(ReceivedItem{item_id, console_index_--, player_slot_, 0});
    pal::logf(pal::LogLevel::Info, "ap_state: console-injected item id=%lld (total=%zu)", static_cast<long long>(item_id), received_items_.size());
}

void ApState::apply(const ApEvent &ev)
{
    std::visit(
        [this](auto &&e)
        {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ApConnected>)
            {
                seed_ = e.seed;
                slot_data_ = e.slot_data;
                player_slot_ = e.player_slot;
                ossex_start_ = e.ossex_start;
                valid_locations_.clear();
                valid_locations_.insert(e.checked_locations.begin(), e.checked_locations.end());
                valid_locations_.insert(e.missing_locations.begin(), e.missing_locations.end());
                authenticated_ = true;
                status_ = "Connected";

                // The server's location-id space. ap_loc_id(slot)=kLocBase+slot must
                // land inside [lo..hi] or is_ap_location() rejects every pickup.
                const std::int64_t lo = valid_locations_.empty() ? 0 : *valid_locations_.begin();
                const std::int64_t hi = valid_locations_.empty() ? 0 : *valid_locations_.rbegin();
                pal::logf(pal::LogLevel::Info,
                          "ap_state: CONNECTED slot=%d seed=%s slot_data=%zuB; valid_locations=%zu (checked=%zu missing=%zu) id_range=[%lld..%lld]",
                          player_slot_, seed_.c_str(), slot_data_.size(), valid_locations_.size(), e.checked_locations.size(), e.missing_locations.size(),
                          static_cast<long long>(lo), static_cast<long long>(hi));
            }
            else if constexpr (std::is_same_v<T, ApItemReceived>)
            {
                if (e.item.index > last_item_index_)
                {
                    received_items_.push_back(e.item);
                    last_item_index_ = e.item.index;
                    pal::logf(pal::LogLevel::Info, "ap_state: item received id=%lld index=%d from=%d flags=%u (total=%zu)",
                              static_cast<long long>(e.item.item_id), e.item.index, e.item.player_from, e.item.flags, received_items_.size());
                }
                else
                {
                    pal::logf(pal::LogLevel::Debug, "ap_state: item index=%d <= cursor=%d, dropped as duplicate", e.item.index, last_item_index_);
                }
            }
            else if constexpr (std::is_same_v<T, ApDisconnected>)
            {
                authenticated_ = false;
                status_ = "Disconnected";
                pal::logf(pal::LogLevel::Warn, "ap_state: DISCONNECTED");
            }
            else if constexpr (std::is_same_v<T, ApConnectionRefused>)
            {
                authenticated_ = false;
                std::string msg = "Refused:";
                for (const auto &err : e.errors)
                {
                    msg += ' ';
                    msg += err;
                }
                status_ = msg;
                pal::logf(pal::LogLevel::Error, "ap_state: connection %s", msg.c_str());
            }
            else if constexpr (std::is_same_v<T, ApStatusChanged>)
            {
                status_ = e.text;
                pal::logf(pal::LogLevel::Debug, "ap_state: status=%s", e.text.c_str());
            }
            else if constexpr (std::is_same_v<T, ApDeathReceived>)
            {
                // Intentionally state-free: deathlink is handled by the ApCoordinator on_death
                // callback (-> App applies the kill on the game thread), not by ApState.
            }
        },
        ev);
}

} // namespace mth
