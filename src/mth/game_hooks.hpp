#pragma once

#include <cstddef>

#include "pal/pal_hook.hpp"

namespace mth
{

struct IGameEvents;

// Installs detours on the engine's tick functions (resolved by symbol name) and
// forwards each to `sink`. RAII: removes them on destruction. If a symbol can't
// be resolved, it logs and skips that hook. `sink` must outlive the GameHooks.
//
// Lives in the module lane (not mthap_core) because it touches pal:: impls.
class GameHooks
{
  public:
    GameHooks(IGameEvents &sink);
    ~GameHooks();

    GameHooks(const GameHooks &) = delete;
    GameHooks &operator=(const GameHooks &) = delete;

  private:
    static constexpr std::size_t kCount = 4;
    pal::HookId ids_[kCount]{};
    std::size_t installed_{0};
};

} // namespace mth
