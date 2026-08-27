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

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        ${MINHOOK_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)

# Upstream's target is just `minhook`; re-expose with a namespaced alias so
# consumer code can use minhook::minhook consistently.
if(NOT TARGET minhook::minhook)
    add_library(minhook::minhook ALIAS minhook)
endif()

message(STATUS "MinHook: ${MINHOOK_VERSION} -> target 'minhook'")
