#pragma once

#include "mth/hooks/scoped_hook.hpp"

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
    ScopedHook fixed_update_;
    ScopedHook update_;
    ScopedHook world_update_;
    ScopedHook update_queue_;
};

} // namespace mth
