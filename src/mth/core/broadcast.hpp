#pragma once

#include <cstdint>
#include <deque>
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

// Relevant when our slot is the message actor (`slot`), the item receiver (`receiving`), or the item
// finder (`item_player`, i.e. a check we sent) and the team matches. Absent team passes (single-team
// default); only a present, different team filters out.
[[nodiscard]] bool broadcast_relevant(int our_team, int our_slot, std::optional<int> team, std::optional<int> slot, std::optional<int> receiving,
                                      std::optional<int> item_player);

// AP-palette color for a PrintJSON node, mirroring apclientpp render_json (explicit color wins, else by
// type/flags). Alpha is 255; the fade scales it.
[[nodiscard]] std::uint32_t banner_color(std::string_view type, std::string_view explicit_color, unsigned item_flags, unsigned hint_status, bool is_self);

struct BannerFrame
{
    std::vector<BannerSegment> segments;
    float alpha{}; // 0..1
};

// Thread-safe FIFO that shows up to kMaxVisible messages stacked at once: push() on the producer thread,
// update(now) on the render thread. Each visible message keeps its own hold+fade timer; a fresh push shows
// immediately while a slot is free, otherwise waits until an active one fades out. `now` is injected
// (monotonic seconds, e.g. ImGui::GetTime()) so the queue/fade logic stays testable.
class BannerQueue
{
  public:
    static constexpr double kHoldSeconds = 3.0; // fully opaque
    static constexpr double kFadeSeconds = 1.0; // then fades to gone
    static constexpr int kMaxVisible = 3;       // messages shown stacked at once

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
    std::deque<Active> active_; // render-thread only (under mutex); oldest at front, <= kMaxVisible
};

} // namespace mth
