#include "pal/pal_overlay.hpp"

namespace pal
{

namespace
{
class NullOverlay final : public IOverlay
{
  public:
    void set_ui(IOverlayUi *) override
    {
    }
};
} // namespace

std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &)
{
    // D3D12 overlay not implemented yet; inert so App wiring is uniform.
    return std::make_unique<NullOverlay>();
}

} // namespace pal
