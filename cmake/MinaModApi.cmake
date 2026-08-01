# MinaModApi.cmake - the game's native mod API header via FetchContent.
#
# Single C header (MinaModAPI.h) consumed only by the PAL native entry TU.
#
# Deliberately tracks upstream main rather than a pinned commit, unlike every
# other fetched dependency. This header defines the struct layout the game hands
# to MinaMod_Init, and a stale pin would hold the mod on an old ABI with nothing
# to signal it. Pin it once there is tooling that reports when upstream moves.
#
# Exposes: mthap::minamodapi (INTERFACE, system include dir only).

include_guard(GLOBAL)
include(FetchContent)

# SOURCE_SUBDIR points at a path with no CMakeLists.txt so MakeAvailable
# populates but does NOT add_subdirectory (header-only, no build wanted).
FetchContent_Declare(
    minamodapi
    GIT_REPOSITORY https://github.com/YachtClubGames/MinaModAPI.git
    GIT_TAG main
    SOURCE_SUBDIR _headers_only
)
FetchContent_MakeAvailable(minamodapi)

add_library(mthap_minamodapi INTERFACE)
add_library(mthap::minamodapi ALIAS mthap_minamodapi)
target_include_directories(mthap_minamodapi SYSTEM INTERFACE "${minamodapi_SOURCE_DIR}")
