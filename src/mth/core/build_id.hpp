#pragma once

#include <string_view>

namespace mth
{

enum class Build
{
    Unknown,
    Linux_v1_0,
};

// GNU BuildID of the shipped Linux MinaTheHollower binary (Ghidra recon, 2026-06-01).
inline constexpr std::string_view kLinuxV1BuildId = "a40f4f641e247efced331ff77e0f2c68d465bc36";

// Map a build identifier (Linux GNU BuildID, or Windows "TimeDateStamp:SizeOfImage")
// to a known Build. Returns Build::Unknown for anything unrecognized.
Build detect_build(std::string_view build_id);

// Human-readable label for a Build.
std::string_view build_name(Build);

} // namespace mth
