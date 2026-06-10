#pragma once

#include <set>
#include <string>
#include <vector>

namespace mth
{

inline constexpr int kCheatCount = 254; // valid modifier indices are 0..253

// Result of parsing a modifier-index list. `indices` is the requested set (stable order, deduped).
// `forced` are indices the user opted into despite being on the deny-list (token prefixed `force:`).
struct ModifierRequest
{
    std::vector<int> indices;
    std::set<int> forced;
};

// Parse a comma-separated modifier list. Tokens: a bare integer, or `force:<int>`. Whitespace
// around tokens is trimmed. Out-of-range (not 0..253) and non-numeric tokens are dropped. Order
// is first-seen; duplicates collapse.
[[nodiscard]] ModifierRequest parse_modifier_indices(const std::string &csv);

} // namespace mth
