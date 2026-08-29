#pragma once

#include <string>
#include <string_view>

namespace mth
{

// Replaces anything outside [A-Za-z0-9_-] with '_' so an AP seed or slot name is usable in a
// filename; '.' is replaced too, so no key part can produce a path traversal. Empty input yields
// "unnamed" rather than an empty component.
std::string sanitize_save_key_part(std::string_view raw);

// "ap_<seed>_<slot>_<hash>.ycsave", seed/slot sanitized. Sanitization is lossy (distinct raw keys
// can collide once unsafe characters fold to '_'), so a hash of the raw parts is appended to keep
// distinct sessions from resolving to the same file.
std::string ap_save_filename(std::string_view seed, std::string_view slot);

// A serialized single slot starts with the ycData header and carries a SaveSlot body. Cheap
// structural check only; the blob is otherwise opaque to us.
bool looks_like_save_blob(std::string_view blob);

} // namespace mth
