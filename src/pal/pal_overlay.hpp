#pragma once

#include <cstdint>
#include <memory>

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
    std::uintptr_t process_sdl_event_addr; // absolute addr of ProcessSDLEvent(SDL_Event&), 0 = input unavailable
};

// Linux -> Vulkan/SDL overlay. Windows -> inert stub. Never returns null.
std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &);

} // namespace pal
