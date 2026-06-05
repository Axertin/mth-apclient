#pragma once

#include <cstdint>
#include <memory>

namespace pal
{

// Content sink: draw() is called every frame between NewFrame() and Render().
// console_open controls whether the interactive console window should show.
class IOverlayUi
{
  public:
    virtual ~IOverlayUi() = default;
    virtual void draw(bool console_open) = 0;
};

// Owns platform render/input hooks and the ImGui context. RAII.
class IOverlay
{
  public:
    virtual ~IOverlay() = default;
    virtual void set_ui(IOverlayUi *) = 0;
};

struct OverlayConfig
{
    std::uintptr_t process_sdl_event_addr; // absolute address of ProcessSDLEvent(SDL_Event&); 0 = input unavailable
};

// Linux: Vulkan/SDL overlay. Windows: inert stub. Never returns null.
std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &);

} // namespace pal
