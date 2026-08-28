#pragma once

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "pal/overlay_font.hpp"
#include "pal/pal_log.hpp"

namespace pal
{

// Size the overlay's font and style for a framebuffer this tall. Call once, after
// CreateContext and before the backend init, so the atlas is baked at its final size.
// Stretching a built atlas afterwards (io.FontGlobalScale, ImFont::Scale) comes out
// blurred.
//
// The reference is the framebuffer, not the OS DPI: the game scales its own UI with the
// framebuffer, and at native fullscreen the reported DPI says nothing about how large the
// game draws itself.
inline void scale_overlay_for_height(unsigned framebuffer_height)
{
    const float scale = std::clamp(static_cast<float>(framebuffer_height) / 1080.0f, 1.0f, 4.0f);

    ImFontConfig cfg;
    cfg.SizePixels = std::round(13.0f * scale);
    // Snapped positions and no oversampling, matching ImGui's own setup for this font, so
    // glyphs land on the pixel grid the way the 13px bitmap font does.
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    // Vertical centering, kept whole. RenderText truncates the draw position and the atlas rounds
    // the ascent, so a fractional offset here is the one thing left that can land a glyph between
    // two rows, where bilinear sampling smears every horizontal stroke.
    cfg.GlyphOffset.y = std::trunc(0.5f * (cfg.SizePixels / 16.0f));
    cfg.FontDataOwnedByAtlas = false; // the array has static storage; the atlas must not free it
    // At sizes between whole multiples of 13 the outlines miss the pixel grid, so each stem
    // spreads over two half-lit columns and reads as blur. Multiplying coverage pushes those
    // columns toward solid without touching the glyph shapes.
    cfg.RasterizerMultiply = 1.6f;
    int font_size = 0;
    const void *font_data = overlay_font_ttf(&font_size);
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<void *>(font_data), font_size, cfg.SizePixels, &cfg);

    ImGui::GetStyle().ScaleAllSizes(scale);

    pal::logf(pal::LogLevel::Info, "overlay: ui scale %.2f (font %dpx, framebuffer height %u)", static_cast<double>(scale), static_cast<int>(cfg.SizePixels),
              framebuffer_height);
}

} // namespace pal
