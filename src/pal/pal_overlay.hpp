#pragma once

#include <cstdint>
#include <memory>

namespace mth
{
enum class Build; // opaque-enum fwd decl (defined in mth/core/build_id.hpp)
}

namespace pal
{

// Content sink: the overlay calls draw() once per presented frame, between
// ImGui's NewFrame() and Render(). Implementations issue ImGui:: widget calls.
class IOverlayUi
{
  public:
    virtual ~IOverlayUi() = default;
    virtual void draw() = 0;
};

// Owns the platform render/input hooks + ImGui context. RAII: installs hooks on
// construction, removes them on destruction. set_ui() may be called before or
// after construction completes; a null sink simply draws nothing.
class IOverlay
{
  public:
    virtual ~IOverlay() = default;
    virtual void set_ui(IOverlayUi *) = 0;
};

struct OverlayConfig
{
    mth::Build build;                      // selects the ProcessSDLEvent offset
    std::uintptr_t process_sdl_event_addr; // absolute addr (base + offset), 0 = none
};

// Linux -> Vulkan/SDL overlay. Windows -> inert stub. Never returns null.
std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &);

} // namespace pal
