# MinaModApi.cmake - the game's native mod API header via FetchContent.
#
# Single C header (MinaModAPI.h) consumed only by the PAL native entry TU.
#
# Pinned like every other fetched dependency. This header defines the struct layout
# the game hands to MinaMod_Init, so a stale pin would hold the mod on an old ABI --
# which is why it tracked main until there was something to report upstream moving.
# That is now .github/workflows/modapi-upstream.yml: it compares MINAMODAPI_COMMIT
# below against upstream main on a schedule and opens an issue when they diverge.
# Bump the pin from that issue; do not put a branch name back here.
#
# Exposes: mthap::minamodapi (INTERFACE, system include dir only).

include_guard(GLOBAL)
include(FetchContent)

# Parsed by .github/workflows/modapi-upstream.yml; keep it one bare 40-char sha on this line.
set(MINAMODAPI_COMMIT 26efcf46f0e6b05b8d751111929193e17230306f) # main @ 2026-08-05

# SOURCE_SUBDIR points at a path with no CMakeLists.txt so MakeAvailable
# populates but does NOT add_subdirectory (header-only, no build wanted).
FetchContent_Declare(
    minamodapi
    GIT_REPOSITORY https://github.com/YachtClubGames/MinaModAPI.git
    GIT_TAG ${MINAMODAPI_COMMIT}
    SOURCE_SUBDIR _headers_only
)
FetchContent_MakeAvailable(minamodapi)

add_library(mthap_minamodapi INTERFACE)
add_library(mthap::minamodapi ALIAS mthap_minamodapi)
target_include_directories(mthap_minamodapi SYSTEM INTERFACE "${minamodapi_SOURCE_DIR}")
