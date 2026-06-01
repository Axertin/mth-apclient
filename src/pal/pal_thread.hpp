#pragma once

namespace pal
{

using ThreadFn = void (*)(void *);

// Spawn a detached thread with the given name. Platform adapters set the
// native thread name where supported.
void spawn_thread(const char *name, ThreadFn fn, void *arg);

} // namespace pal
