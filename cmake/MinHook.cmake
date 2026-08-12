# MinHook.cmake - fetch MinHook from upstream and expose as `minhook::minhook`.
#
# MinHook is our hook backend on Windows (MinGW cross-compile friendly).
# Upstream: https://github.com/TsudaKageyu/minhook - has its own CMakeLists.txt
# so FetchContent_MakeAvailable gives us a usable static library directly.

if(TARGET minhook::minhook)
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "MinHook.cmake is Windows-only; use FridaGum.cmake on Linux")
endif()

set(MINHOOK_VERSION "v1.3.4" CACHE STRING "MinHook release tag")
# Commit ${MINHOOK_VERSION} resolves to; checked after populate so an upstream
# retag fails configure instead of silently changing what ships.
set(MINHOOK_COMMIT "c3fcafdc10146beb5919319d0683e44e3c30d537")

include(FetchContent)
include(VerifyGitPin)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        ${MINHOOK_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)
mthap_verify_git_pin(MinHook "${minhook_SOURCE_DIR}" "${MINHOOK_COMMIT}")

# Upstream's target is just `minhook`; re-expose with a namespaced alias so
# consumer code can use minhook::minhook consistently.
if(NOT TARGET minhook::minhook)
    add_library(minhook::minhook ALIAS minhook)
endif()

message(STATUS "MinHook: ${MINHOOK_VERSION} -> target 'minhook'")
