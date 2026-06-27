#pragma once

namespace pal
{

// Sets the handler for the native FixedUpdate hook (nullptr clears).
// Game-thread, null-checked, so late registration is safe.
void set_fixed_update_handler(void (*handler)());

} // namespace pal
