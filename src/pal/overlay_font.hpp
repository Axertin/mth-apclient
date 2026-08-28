#pragma once

namespace pal
{

// ProggyForever, fetched and turned into a byte array by cmake/OverlayFont.cmake. Static
// storage, so the atlas must reference it rather than take ownership.
const void *overlay_font_ttf(int *out_size);

} // namespace pal
