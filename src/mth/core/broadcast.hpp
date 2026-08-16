#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mth
{

// rgba is IM_COL32 layout (R|G<<8|B<<16|A<<24) so the UI draws it without conversion; no ImGui dep here.
struct BannerSegment
{
    std::string text;
    std::uint32_t rgba{};
};

// PrintJSON types addressed to the room rather than to one slot (chat, /say, countdowns, command
// replies). They carry no slot to match against, so the slot gate below would drop them all.
[[nodiscard]] bool broadcast_is_announcement(std::string_view print_type);

// Relevant when the message is an announcement, or our slot is the message actor (`slot`), the item
// receiver (`receiving`), or the item finder (`item_player`, i.e. a check we sent). The team must match
// either way. Absent team passes (single-team default); only a present, different team filters out.
[[nodiscard]] bool broadcast_relevant(std::string_view print_type, int our_team, int our_slot, std::optional<int> team, std::optional<int> slot,
                                      std::optional<int> receiving, std::optional<int> item_player);

// AP-palette color for a PrintJSON node, mirroring apclientpp render_json (explicit color wins, else by
// type/flags). Alpha is 255; the fade scales it.
[[nodiscard]] std::uint32_t banner_color(std::string_view type, std::string_view explicit_color, unsigned item_flags, unsigned hint_status, bool is_self);

// Attribution line for an inbound deathlink. The AP convention is that `cause` is already a full
// sentence naming the dead player, so it is shown verbatim; `source` (who died, by slot name or by a
// character name from within the sender's game) only carries the line when the sender supplied no cause.
[[nodiscard]] std::string deathlink_banner_text(std::string_view source, std::string_view cause);

struct BannerFrame
{
    std::vector<BannerSegment> segments;
    float alpha{}; // 0..1
};

// Thread-safe FIFO that shows up to kMaxVisible messages stacked at once: push() on the producer thread,
// update(now) on the render thread. Each visible message keeps its own hold+fade timer; a message waits for
// both a free slot and the promotion interval, so a burst arriving in one frame staggers in rather than
// landing as a block. `now` is injected (monotonic seconds, e.g. ImGui::GetTime()) so the queue/fade logic
// stays testable.
class BannerQueue
{
  public:
    static constexpr double kHoldSeconds = 5.0;            // fully opaque
    static constexpr double kFadeSeconds = 1.0;            // then fades to gone
    static constexpr double kPromoteIntervalSeconds = 1.0; // spacing between two messages appearing
    static constexpr int kMaxVisible = 5;                  // messages shown stacked at once

    void push(std::vector<BannerSegment> segments);

    // Advances the queue against `now` and returns the active messages to draw (oldest first), each with its
    // own alpha. Empty when idle. The renderer stacks them top-to-bottom.
    [[nodiscard]] std::vector<BannerFrame> update(double now);

  private:
    struct Active
    {
        std::vector<BannerSegment> segments;
        double start{0.0};
    };
    std::mutex mutex_;
    std::deque<std::vector<BannerSegment>> pending_;
    std::deque<Active> active_;                                     // render-thread only (under mutex); oldest at front, <= kMaxVisible
    double next_promote_{-std::numeric_limits<double>::infinity()}; // first message after an idle spell is never held back
};

} // namespace mth
