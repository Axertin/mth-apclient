#pragma once

#include <cstdint>
#include <span>

#include "mth/core/sig_scan.hpp"

namespace pal
{

// Defined in win_signatures_generated.cpp (generated).
std::span<const mth::sig::Entry> sig_table();

// Defined in signature_scan.cpp. Resolves a mangled name by scanning the loaded
// game module's .text for the table entry's signature. Returns 0 on miss.
std::uintptr_t scan_resolve(const char *mangled_name);

} // namespace pal
