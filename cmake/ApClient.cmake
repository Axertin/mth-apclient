# ApClient.cmake - header-only Archipelago client deps via FetchContent.
#
# apclientpp -> wswrap -> websocketpp form the Archipelago net stack. All three
# are header-only and were previously vendored as git submodules under external/.
# We fetch them here instead so:
#   * the patched websocketpp copy lives in the build dir (build/_deps), never
#     dirtying a tracked worktree the way an in-place `git apply` on a submodule did;
#   * the C++23 fix is a normal PATCH_COMMAND against the pinned tag, applied once
#     at populate (no idempotent reverse-check dance);
#   * external/ holds only vcpkg.
#
# Pins are the exact commits the submodules recorded. asio/OpenSSL/ZLIB/json come
# from vcpkg (find_package in src/mth), not from here.
#
# Exposes: mthap::apclient (INTERFACE, system include dirs only).

include_guard(GLOBAL)
include(FetchContent)

# CMP0097 NEW: GIT_SUBMODULES "" means *no* submodules (old behavior was "all").
# Needed so wswrap's subprojects/wsjs (emscripten backend) is not cloned.
cmake_policy(SET CMP0097 NEW)

# SOURCE_SUBDIR points at a path with no CMakeLists.txt so MakeAvailable populates
# (and runs PATCH_COMMAND) but does NOT add_subdirectory - these are header-only and
# their own CMake builds (examples/tests/install) are not wanted.
set(_apclient_no_build "_headers_only")

FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG        56123c87598f8b1dd471be83ca841ceae07f95ba # 0.8.2
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/websocketpp-0.8.2-cxx23-cdtor.patch
                       -DWORKDIR=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/apply_patch.cmake
    SOURCE_SUBDIR  ${_apclient_no_build}
)

FetchContent_Declare(
    wswrap
    GIT_REPOSITORY https://github.com/black-sliver/wswrap.git
    GIT_TAG        aeba7ac428028723fb26ce92488f260660f786b1
    GIT_SUBMODULES "" # skip subprojects/wsjs (emscripten backend, unused)
    SOURCE_SUBDIR  ${_apclient_no_build}
)

FetchContent_Declare(
    apclientpp
    GIT_REPOSITORY https://github.com/black-sliver/apclientpp.git
    GIT_TAG        79621690a3e845645f43888b0fe234a99c74892e
    SOURCE_SUBDIR  ${_apclient_no_build}
)

FetchContent_MakeAvailable(websocketpp wswrap apclientpp)

add_library(mthap_apclient INTERFACE)
add_library(mthap::apclient ALIAS mthap_apclient)
target_include_directories(mthap_apclient SYSTEM INTERFACE
    "${apclientpp_SOURCE_DIR}"
    "${wswrap_SOURCE_DIR}/include"
    "${websocketpp_SOURCE_DIR}"
)
