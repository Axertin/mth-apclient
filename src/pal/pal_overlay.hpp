#pragma once

#include <cstdint>
#include <memory>

namespace pal
{

// Per-frame visibility flags, published by the platform overlay from its toggle-key atomics.
struct OverlayVisibility
{
    bool console_open{false};
    bool login_open{false};
};

// Content sink: draw() is called every frame between NewFrame() and Render().
class IOverlayUi
{
  public:
    virtual ~IOverlayUi() = default;
    virtual void draw(const OverlayVisibility &vis) = 0;
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
