#pragma once

#include <cstddef>

#include "mth/core/build_id.hpp"
#include "pal/pal_hook.hpp"

namespace mth
{

struct IGameEvents;

// Installs detours on the engine's tick functions for the given build and
// forwards each to `sink`. RAII: removes them on destruction. If the build's
// offsets aren't known (offsets_for returns zeros), it logs and installs
// nothing. `sink` must outlive the GameHooks.
//
// Lives in the module lane (not mthap_core) because it touches pal:: impls.
class GameHooks
{
  public:
    GameHooks(Build build, IGameEvents &sink);
    ~GameHooks();

    GameHooks(const GameHooks &) = delete;
    GameHooks &operator=(const GameHooks &) = delete;

  private:
    static constexpr std::size_t kCount = 4;
    pal::HookId ids_[kCount]{};
    std::size_t installed_{0};
};

} // namespace mth
