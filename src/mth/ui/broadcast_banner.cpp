#include "mth/ui/broadcast_banner.hpp"

#include <cfloat>

#include <imgui.h>

#include "mth/core/broadcast.hpp"

namespace mth
{

void BroadcastBanner::draw()
{
    const std::optional<BannerFrame> frame = queue_.update(ImGui::GetTime());
    if (!frame)
        return;

    const float a = frame->alpha;
    const auto fade = [a](int v) { return static_cast<int>(static_cast<float>(v) * a); };

    // Render a touch larger than the default UI font for legibility.
    constexpr float font_scale = 1.25f;
    ImFont *font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * font_scale;
    const auto seg_width = [&](const BannerSegment &seg) { return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, seg.text.c_str()).x; };

    float text_w = 0.0f;
    for (const BannerSegment &seg : frame->segments)
        text_w += seg_width(seg);
    const float text_h = font_size;

    constexpr float margin = 10.0f;
    constexpr float pad_x = 8.0f;
    constexpr float pad_y = 4.0f;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float right = vp->WorkPos.x + vp->WorkSize.x - margin;
    const float top = vp->WorkPos.y + margin;
    const ImVec2 box_min(right - text_w - 2.0f * pad_x, top);
    const ImVec2 box_max(right, top + text_h + 2.0f * pad_y);

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(box_min, box_max, IM_COL32(20, 20, 26, fade(205)), 5.0f);
    dl->AddRect(box_min, box_max, IM_COL32(255, 255, 255, fade(45)), 5.0f);

    ImVec2 pos(box_min.x + pad_x, box_min.y + pad_y);
    for (const BannerSegment &seg : frame->segments)
    {
        const ImU32 col = (seg.rgba & 0x00FFFFFFu) | (static_cast<ImU32>(fade(255)) << 24);
        dl->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, fade(180)), seg.text.c_str()); // shadow
        dl->AddText(font, font_size, pos, col, seg.text.c_str());
        pos.x += seg_width(seg);
    }
}

} // namespace mth
