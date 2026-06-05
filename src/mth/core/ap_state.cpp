#include "mth/core/ap_state.hpp"

#include <type_traits>
#include <variant>

namespace mth
{

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
                valid_locations_.clear();
                valid_locations_.insert(e.checked_locations.begin(), e.checked_locations.end());
                valid_locations_.insert(e.missing_locations.begin(), e.missing_locations.end());
                authenticated_ = true;
                status_ = "Connected";
            }
            else if constexpr (std::is_same_v<T, ApItemReceived>)
            {
                if (e.item.index > last_item_index_)
                {
                    received_items_.push_back(e.item);
                    last_item_index_ = e.item.index;
                }
            }
            else if constexpr (std::is_same_v<T, ApDisconnected>)
            {
                authenticated_ = false;
                status_ = "Disconnected";
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
            }
            else if constexpr (std::is_same_v<T, ApStatusChanged>)
            {
                status_ = e.text;
            }
        },
        ev);
}

} // namespace mth
