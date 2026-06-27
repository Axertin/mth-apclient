# MinaModApi.cmake - the game's native mod API header via FetchContent.
#
# Single C header (MinaModAPI.h) consumed only by the PAL native entry TU.
# Pinned to an exact upstream commit, mirroring cmake/ApClient.cmake.
#
# Exposes: mthap::minamodapi (INTERFACE, system include dir only).

include_guard(GLOBAL)
include(FetchContent)

# SOURCE_SUBDIR points at a path with no CMakeLists.txt so MakeAvailable
# populates but does NOT add_subdirectory (header-only, no build wanted).
FetchContent_Declare(
    minamodapi
    GIT_REPOSITORY https://github.com/YachtClubGames/MinaModAPI.git
    GIT_TAG        0be0a0448f941211d9409982ccf41ffe4e032c98
    SOURCE_SUBDIR  _headers_only
)
FetchContent_MakeAvailable(minamodapi)

add_library(mthap_minamodapi INTERFACE)
add_library(mthap::minamodapi ALIAS mthap_minamodapi)
target_include_directories(mthap_minamodapi SYSTEM INTERFACE "${minamodapi_SOURCE_DIR}")
