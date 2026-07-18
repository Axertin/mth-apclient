#pragma once

#include <atomic>
#include <cstdint>

namespace mth
{

// Owns the HubFountain::Bulb::Update detour that forces chosen Ossex fountain lamps lit (visual only;
// never writes SaveSlot+0x290). set_lit_mask publishes the force-lit bitmask (bit i => lamp i). Installs
// on construction, removes on destruction.
class FountainLampHooks
{
  public:
    FountainLampHooks();
    ~FountainLampHooks();

    FountainLampHooks(const FountainLampHooks &) = delete;
    FountainLampHooks &operator=(const FountainLampHooks &) = delete;

    void set_lit_mask(std::uint32_t mask)
    {
        mask_.store(mask, std::memory_order_relaxed);
    }

  private:
    std::atomic<std::uint32_t> mask_{0};
};

} // namespace mth
