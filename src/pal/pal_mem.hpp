#pragma once

#include <cstddef>

namespace pal
{

// RW-protect [addr, addr+len); not restored. Linux page-aligns internally.
bool make_writable(void *addr, std::size_t len);

} // namespace pal
