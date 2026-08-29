cmake_minimum_required(VERSION 3.25)

# No VERSION on project(): release-please stamps prerelease versions like 1.0.1-beta.1 and project()
# rejects those outright ("VERSION format invalid"), which would break configure on every beta. The
# build's real version comes from git describe in cmake/SimpleGitVersion.cmake; the variable below is
# just the last released version, kept honest by release-please and read by nothing.
project(mth-apclient
    LANGUAGES C CXX
)

set(MTHAP_RELEASE_VERSION "1.2.0-beta.3") # x-release-please-version

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The mod is loaded into the game's process, so it must export only its entry point:
# anything else is interposition surface against the host and any other loaded mod.
# MinaMod_Init carries an explicit default-visibility attribute and is unaffected.
# --exclude-libs,ALL localizes static archives only, not the OBJECT libraries.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(MTHAP_BUILD_TESTS "Build unit tests (Catch2)" ON)
option(MTHAP_BUILD_TESTS_ONLY "Build only tests, skip mod build" OFF)
option(MTHAP_ENABLE_NET "Build the Archipelago network lane (apclientpp + TLS)" ON)

# Overlay (Dear ImGui dev UI): Vulkan/SDL on Linux, D3D12/Win32 on Windows. The
# mingw cross builds it too (its only third-party dep is system DirectX headers),
# keeping the cross a faithful compile-check of the real clang-cl build.
option(MTHAP_ENABLE_OVERLAY "Build the in-game Dear ImGui dev overlay" ON)

set(MTHAP_MIN_GAME_VERSION 148662 CACHE STRING "Minimum supported game build for mod.yc")
set(MTHAP_MAX_GAME_VERSION 999999 CACHE STRING "Maximum supported game build for mod.yc")

include(compiler_setup)
include(SimpleGitVersion)

# The LLVM-MinGW dev cross routes printf-family calls through msvcrt, which drops
# C99 specifiers like %lld/%zu unless we switch mingw to its own conformant stdio.
# Harmless on the canonical clang-cl (UCRT) and Linux builds, where MINGW is unset.
if(MINGW)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

if(NOT MTHAP_BUILD_TESTS_ONLY)
    if(WIN32)
        include(MinHook)
    else()
        include(FridaGum)
    endif()
    # Contributes a generated source to mthap_pal, so it has to run before that subdir.
    if(MTHAP_ENABLE_OVERLAY)
        include(OverlayFont)
    endif()

    add_subdirectory(src/pal)

    # Archipelago net stack (apclientpp/wswrap/websocketpp), fetched + patched
    # into the build dir. websocketpp 0.8.2 needs a small C++23 ctor/dtor fix,
    # applied as a PATCH_COMMAND in ApClient.cmake (no in-tree submodule edits).
    if(MTHAP_ENABLE_NET)
        include(ApClient)
    endif()

    if(MTHAP_ENABLE_OVERLAY)
        include(DearImGui)
    endif()
endif()

# mthap_mod (mod::) is header-only-dependent (mthap::minamodapi, a FetchContent
# INTERFACE lib with no vcpkg/net bootstrap) and pure logic, so it builds in both
# lanes; the unit tests link it directly to exercise the mod:: marshaling.
include(MinaModApi)
add_subdirectory(src/mod)

# mthap_core is pure (PAL headers only) and is linked by tests; build it in
# both lanes. The mthap MODULE target inside this subdir is guarded on
# NOT MTHAP_BUILD_TESTS_ONLY.
add_subdirectory(src/mth)

if(MTHAP_BUILD_TESTS OR MTHAP_BUILD_TESTS_ONLY)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        SYMBOLIC)
endif()
