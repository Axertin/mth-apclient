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

// GNU BuildID of the live Steam Linux MinaTheHollower binary, 1.0.5 (2026-06-02
// rebuild). Offsets in offsets.cpp target this build; re-derive when Steam pushes
// a new revision. (Prior builds: 45919e54... = earlier 1.0.5 [r148053];
// a40f4f64... = 1.0.5 [r147980], the original RE recon target.)
inline constexpr std::string_view kLinuxV1BuildId = "958b6568117d394bc8daae1da44ec9f8f260f3c8";

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
