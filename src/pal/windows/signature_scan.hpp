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

// Resolves a function from the name hash of a mod hook it dispatches. RunHooks inlines that hash at
// the dispatch site as a movabs immediate, so the constant is an anchor INSIDE the target function,
// and .pdata maps the interior address back to the exact entry point. The hash derives from the hook
// name rather than from surrounding code, so unlike a carved signature it cannot drift between
// builds. Returns 0 if the constant is missing, not unique, or has no .pdata entry.
std::uintptr_t resolve_by_hook_anchor(std::uint64_t hook_hash);

} // namespace pal
