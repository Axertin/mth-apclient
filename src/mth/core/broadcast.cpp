#include "mth/core/broadcast.hpp"

#include <algorithm>

namespace mth
{

namespace
{

constexpr std::uint32_t pack_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) | (std::uint32_t{0xFF} << 24);
}

// Color names apclientpp's color2ansi emits.
std::uint32_t name_to_rgba(std::string_view name)
{
    if (name == "red")
        return pack_rgba(255, 90, 90);
    if (name == "green")
        return pack_rgba(90, 220, 90);
    if (name == "yellow")
        return pack_rgba(240, 230, 90);
    if (name == "blue")
        return pack_rgba(110, 150, 255);
    if (name == "magenta")
        return pack_rgba(245, 110, 245);
    if (name == "cyan")
        return pack_rgba(90, 220, 230);
    if (name == "plum")
        return pack_rgba(240, 175, 245);
    if (name == "slateblue")
        return pack_rgba(125, 130, 235);
    if (name == "salmon")
        return pack_rgba(250, 150, 140);
    if (name == "gray" || name == "grey")
        return pack_rgba(170, 170, 170);
    return pack_rgba(255, 255, 255); // default / unknown
}

} // namespace

bool broadcast_relevant(int our_team, int our_slot, std::optional<int> team, std::optional<int> slot, std::optional<int> receiving,
                        std::optional<int> item_player)
{
    const bool team_ok = !team.has_value() || *team == our_team;
    const bool slot_ok =
        (slot.has_value() && *slot == our_slot) || (receiving.has_value() && *receiving == our_slot) || (item_player.has_value() && *item_player == our_slot);
    return team_ok && slot_ok;
}

std::uint32_t banner_color(std::string_view type, std::string_view explicit_color, unsigned item_flags, unsigned hint_status, bool is_self)
{
    // Explicit server color wins, as render_json does for ANSI/HTML.
    if (!explicit_color.empty())
        return name_to_rgba(explicit_color);

    if (type == "player_id")
        return name_to_rgba(is_self ? "magenta" : "yellow");
    if (type == "item_id")
    {
        if (item_flags & 1u) // FLAG_ADVANCEMENT (progression)
            return name_to_rgba("plum");
        if (item_flags & 2u) // FLAG_NEVER_EXCLUDE (useful)
            return name_to_rgba("slateblue");
        if (item_flags & 4u) // FLAG_TRAP
            return name_to_rgba("salmon");
        return name_to_rgba("cyan");
    }
    if (type == "location_id")
        return name_to_rgba("blue");
    if (type == "hint_status")
    {
        switch (hint_status)
        {
        case 40: // HINT_FOUND
            return name_to_rgba("green");
        case 10: // HINT_NO_PRIORITY
            return name_to_rgba("slateblue");
        case 20: // HINT_AVOID
            return name_to_rgba("salmon");
        case 30: // HINT_PRIORITY
            return name_to_rgba("plum");
        case 0: // HINT_UNSPECIFIED
            return name_to_rgba("grey");
        default:
            return name_to_rgba("red");
        }
    }
    return name_to_rgba(""); // "text" / "color" / unknown -> white
}

void BannerQueue::push(std::vector<BannerSegment> segments)
{
    if (segments.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(segments));
}

std::optional<BannerFrame> BannerQueue::update(double now)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (showing_ && now - start_ >= kHoldSeconds + kFadeSeconds)
    {
        showing_ = false;
        current_.clear();
    }

    if (!showing_ && !pending_.empty())
    {
        current_ = std::move(pending_.front());
        pending_.pop_front();
        start_ = now;
        showing_ = true;
    }

    if (!showing_)
        return std::nullopt;

    const double elapsed = now - start_;
    float alpha = 1.0f;
    if (elapsed > kHoldSeconds)
        alpha = static_cast<float>(1.0 - (elapsed - kHoldSeconds) / kFadeSeconds);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return BannerFrame{current_, alpha};
}

} // namespace mth
