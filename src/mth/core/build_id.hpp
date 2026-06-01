#pragma once

#include <string_view>

namespace mth
{

enum class Build
{
    Unknown,
    Linux_v1_0,
    Windows_v1_0,
};

// GNU BuildID of the shipped Linux MinaTheHollower binary (Ghidra recon, 2026-06-01).
inline constexpr std::string_view kLinuxV1BuildId = "a40f4f641e247efced331ff77e0f2c68d465bc36";

// Windows build id, formatted "TimeDateStamp:SizeOfImage" exactly as
// pal::game_build_id() emits it on Windows (see pal/windows/module_windows.cpp).
// Read from MinaTheHollower.exe's PE header (Ghidra recon, 2026-06-01).
inline constexpr std::string_view kWindowsV1BuildId = "6a1c62c7:0543d000";

// Map a build identifier (Linux GNU BuildID, or Windows "TimeDateStamp:SizeOfImage")
// to a known Build. Returns Build::Unknown for anything unrecognized.
Build detect_build(std::string_view build_id);

// Human-readable label for a Build.
std::string_view build_name(Build);

} // namespace mth
