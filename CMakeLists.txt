cmake_minimum_required(VERSION 3.25)

project(mth-apclient
    VERSION 0.1.0
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MTHAP_BUILD_TESTS "Build unit tests (Catch2)" ON)
option(MTHAP_BUILD_TESTS_ONLY "Build only tests, skip mod build" OFF)
option(MTHAP_ENABLE_NET "Build the Archipelago network lane (apclientpp + TLS)" ON)

# Overlay (Dear ImGui dev UI): Vulkan/SDL on Linux, D3D12/Win32 on Windows. The
# mingw cross builds it too (its only third-party dep is system DirectX headers),
# keeping the cross a faithful compile-check of the real clang-cl build.
option(MTHAP_ENABLE_OVERLAY "Build the in-game Dear ImGui dev overlay" ON)

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
