#pragma once

#include <cstddef>

#include "pal/pal_hook.hpp"

namespace mth
{

struct IGameEvents;

// RAII detours on engine tick functions (symbol-resolved). `sink` must outlive this.
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
