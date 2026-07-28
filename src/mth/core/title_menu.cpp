#include "mth/core/title_menu.hpp"

namespace mth
{

int skip_gated_option(int previous, int current) noexcept
{
    if (current != 0)
        return current;
    // current == 0 either by moving Up from 1 (continue upward, to 2) or wrapping Down from 2
    // (continue downward, to 1); previous == 0 (no movement) also lands on 1.
    return previous == 1 ? 2 : 1;
}

} // namespace mth
