#include "mth/core/stat_cap_state.hpp"

#include <algorithm>
#include <cstddef>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"

namespace mth
{

void StatCapState::recompute(const ApState &state)
{
    for (int s = 0; s < kStatCount; ++s)
        counts_[s] = 0;
    for (const auto &it : state.received_items())
    {
        if (!is_stat_cap_item(it.item_id))
            continue;
        if (is_stat_cap_all_item(it.item_id)) // one receipt raises every stat's cap
            for (int s = 0; s < kStatCount; ++s)
                ++counts_[s];
        else
            ++counts_[stat_cap_item_stat(it.item_id)];
    }
}

void StatCapState::set_counts(int attack, int defense, int sidearm)
{
    counts_[0] = attack;
    counts_[1] = defense;
    counts_[2] = sidearm;
}

int StatCapState::granted(int stat) const
{
    if (stat < 0 || stat >= kStatCount)
        return 0;
    return counts_[stat];
}

int StatCapState::enforced_cap(int stat, int vanilla_cap) const
{
    if (stat < 0 || stat >= kStatCount)
        return vanilla_cap;
    return std::min(vanilla_cap, counts_[stat]);
}

std::string boneup_with_cap_suffix(const std::string &text, int display_cap)
{
    if (text.empty())
        return text;

    const std::size_t eol = text.find('\n');
    std::string head = text.substr(0, eol == std::string::npos ? text.size() : eol);
    const std::string tail = eol == std::string::npos ? std::string() : text.substr(eol);
    if (head.empty())
        return text; // no title line to annotate

    // Only a bare number is treated as ours to replace, and the caller annotates the three real stats
    // only, whose first line is "<Stat> Level <n>". The bone-bank row, the one description that ends in
    // a parenthesised number, never reaches here.
    if (head.back() == ')')
    {
        const std::size_t open_paren = head.rfind(" (");
        if (open_paren != std::string::npos)
        {
            const std::size_t first = open_paren + 2;
            const std::size_t close = head.size() - 1;
            bool numeric = close > first;
            for (std::size_t i = first; i < close && numeric; ++i)
                numeric = head[i] >= '0' && head[i] <= '9';
            if (numeric)
                head.erase(open_paren);
        }
    }

    return head + " (" + std::to_string(display_cap) + ")" + tail;
}

} // namespace mth
