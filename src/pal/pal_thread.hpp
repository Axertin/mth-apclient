#pragma once

namespace pal
{

using ThreadFn = void (*)(void *);

void spawn_thread(const char *name, ThreadFn fn, void *arg);

} // namespace pal
