# FridaGum.cmake - fetch frida-gum devkit (Linux only) and expose as `frida::gum`.
#
# Frida is our hook backend on Linux. On Windows we use MinHook instead;
# see cmake/MinHook.cmake. The plan originally targeted Frida on both,
# but the prebuilt Windows devkit is MSVC-built and pulls in a cascade
# of MSVC-only codegen helpers that llvm-mingw can't resolve.
#
# Usage:
#   include(FridaGum)   # Linux only; include(MinHook) on Windows.
#   target_link_libraries(my_target PRIVATE frida::gum)

if(TARGET frida::gum)
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "FridaGum.cmake is Linux-only; use MinHook.cmake on Windows")
endif()

set(FRIDA_GUM_VERSION "16.5.9" CACHE STRING "Frida-gum devkit version")
set(_frida_triple "linux-x86_64")
set(_frida_archive "frida-gum-devkit-${FRIDA_GUM_VERSION}-${_frida_triple}.tar.xz")
set(_frida_url "https://github.com/frida/frida/releases/download/${FRIDA_GUM_VERSION}/${_frida_archive}")

# Optional SHA256 pinning via FRIDA_GUM_SHA256_LINUX_X86_64 cache var.
set(_frida_hash_arg "")
if(DEFINED FRIDA_GUM_SHA256_LINUX_X86_64 AND NOT "${FRIDA_GUM_SHA256_LINUX_X86_64}" STREQUAL "")
    set(_frida_hash_arg URL_HASH "SHA256=${FRIDA_GUM_SHA256_LINUX_X86_64}")
else()
    message(STATUS "FridaGum: no SHA256 pinned (set FRIDA_GUM_SHA256_LINUX_X86_64 to pin).")
endif()

include(FetchContent)
FetchContent_Declare(
    frida_gum
    URL "${_frida_url}"
    ${_frida_hash_arg}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(frida_gum)

set(_frida_lib "${frida_gum_SOURCE_DIR}/libfrida-gum.a")
if(NOT EXISTS "${_frida_lib}")
    set(_frida_lib "${frida_gum_SOURCE_DIR}/lib/libfrida-gum.a")
endif()
if(NOT EXISTS "${_frida_lib}")
    message(FATAL_ERROR "FridaGum: could not locate libfrida-gum.a under ${frida_gum_SOURCE_DIR}")
endif()

set(_frida_include "${frida_gum_SOURCE_DIR}")
if(EXISTS "${frida_gum_SOURCE_DIR}/include")
    set(_frida_include "${frida_gum_SOURCE_DIR}/include")
endif()

add_library(frida::gum STATIC IMPORTED GLOBAL)
set_target_properties(frida::gum PROPERTIES
    IMPORTED_LOCATION "${_frida_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_frida_include}"
)
target_link_libraries(frida::gum INTERFACE dl rt pthread resolv m)

message(STATUS "FridaGum: ${FRIDA_GUM_VERSION} (${_frida_triple}) -> ${_frida_lib}")
