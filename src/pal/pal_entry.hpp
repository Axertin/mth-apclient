#pragma once

// Defined in src/mth/mth-apclient.cpp.
// Called once by the platform adapter on a worker thread shortly after
// the game process starts. Must not be called from DllMain / a constructor
// attribute directly - platform adapters spawn a thread first.

namespace pal
{

void apclient_main();

} // namespace pal
