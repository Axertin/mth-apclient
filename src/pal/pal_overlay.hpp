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

// Linux: Vulkan/SDL overlay. Windows: D3D12/Win32 overlay. Never returns null.
// Each platform resolves whatever game symbols it needs itself: ProcessSDLEvent is Linux-only, and
// routing it through here made Windows carry a signature for a symbol it never reads.
std::unique_ptr<IOverlay> make_overlay();

} // namespace pal
