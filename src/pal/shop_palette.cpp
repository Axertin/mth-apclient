#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/palette_index.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

namespace
{

// One clone serves every widget and every shop. Update resolves a second entry of this same clone
// (component+0x17c) besides the one written here, but RenderInternal consumes it only when the flag
// at component+0x1ac is set, and the ctor zeroes it.
void *g_clone = nullptr;
bool g_warned_range = false;
bool g_warned_refcount = false;
bool g_warned_plausibility = false;

std::int32_t *field_i32(void *base, std::ptrdiff_t off)
{
    return reinterpret_cast<std::int32_t *>(static_cast<char *>(base) + off);
}

void **field_ptr(void *base, std::ptrdiff_t off)
{
    return reinterpret_cast<void **>(static_cast<char *>(base) + off);
}

// Cloned once and never released. ReleasePalette only decrements and never frees, so releasing
// reclaims nothing and lets the game's release path destroy the clone under a live widget.
void *ensure_clone(void *source)
{
    if (g_clone != nullptr)
        return g_clone;
    void *clone = mod::clone_palette(source);
    if (clone == nullptr)
        return nullptr;
    mod::palette_set_group(clone, -1); // detach from the group remap chain so it resolves from its own colors
    g_clone = clone;
    return g_clone;
}

} // namespace

namespace pal
{

bool shop_apply_name_palette(void *name_widget, std::uint32_t rgba)
{
    if (!pal::pointer_looks_valid(name_widget) || !mod::palette_api_available())
        return false;

    void *lookup = *field_ptr(name_widget, mth::layout::kTextLookupPaletteOff);
    void *output = *field_ptr(name_widget, mth::layout::kTextOutputPaletteOff);
    if (!pal::pointer_looks_valid(lookup) || !pal::pointer_looks_valid(output))
        return false;

    void *clone = ensure_clone(output);
    if (clone == nullptr)
        return false;

    const auto target = mth::palette_target_index(mod::palette_get_index(lookup, rgba), mod::palette_get_width(clone));
    if (!target.has_value())
    {
        if (!g_warned_range)
        {
            g_warned_range = true;
            pal::logf(pal::LogLevel::Warn, "shop: palette entry out of range; item name color skipped");
        }
        return false;
    }
    mod::palette_write_index(clone, static_cast<std::int32_t>(*target), rgba);

    if (output != clone)
    {
        std::int32_t *out_rc = field_i32(output, mth::layout::kPaletteRefCountOff);
        const std::uint32_t out_width = mod::palette_get_width(output);
        // pointer_looks_valid above only proves output is a canonical address, not a
        // ycPaletteTexture. Implausible bounds mean the field moved between builds and the writes
        // below would land in an unrelated object.
        if (*out_rc <= 0 || *out_rc >= 0x10000 || out_width == 0 || out_width > 255)
        {
            if (!g_warned_plausibility)
            {
                g_warned_plausibility = true;
                pal::logf(pal::LogLevel::Warn,
                          "shop: output palette at +0x128 does not look like a ycPaletteTexture (refcount=%d width=%u); leaving it untouched", *out_rc,
                          out_width);
            }
            return false;
        }

        // Hand-rolled SetPalette. The widget's reference to the game's palette moves to the clone.
        if (*out_rc > 1)
        {
            *out_rc -= 1;
        }
        else if (!g_warned_refcount)
        {
            // The owning ShopMenu holds its own reference, so this should be unreachable. Leaking a
            // reference is survivable; destroying a palette the game still points at is not.
            g_warned_refcount = true;
            pal::logf(pal::LogLevel::Warn, "shop: name palette refcount would reach zero; leaving it held");
        }

        *field_ptr(name_widget, mth::layout::kTextOutputPaletteOff) = clone;
        *field_i32(clone, mth::layout::kPaletteRefCountOff) += 1;
        *field_i32(name_widget, mth::layout::kTextPaletteVersionOff) = *field_i32(clone, mth::layout::kPaletteVersionOff) - 1;
    }
    return true;
}

} // namespace pal
