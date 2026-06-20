# FridaGum.cmake - build a slim, x86-only frida-gum devkit from source and
# expose it as `frida::gum`.
#
# Frida is our hook backend on Linux. On Windows we use MinHook instead;
# see cmake/MinHook.cmake. The plan originally targeted Frida on both,
# but the prebuilt Windows devkit is MSVC-built and pulls in a cascade
# of MSVC-only codegen helpers that llvm-mingw can't resolve.
#
# Why a source build instead of the prebuilt devkit: upstream's devkit bundles
# Capstone with ALL ~16 disassembler architectures, of which only x86 is ever
# used (the Interceptor relocator + Stalker). Capstone's arch-dispatch table
# references every backend, so the linker can't garbage-collect the unused ones
# -- ~7 MB of dead ARM/AArch64/MIPS/PPC/... disassembler ships in libmthap.so.
# Building frida-gum with `-Dcapstone:archs=x86` drops it. frida-gum's own
# `-Ddevkits=gum` emits the same single `frida-gum.h` + `libfrida-gum.a` shape
# the prebuilt devkit had, so consumers (`#include <frida-gum.h>`) are unchanged.
#
# Build requirements: a C/C++ compiler (have), `python3` + `git` + `make`, and
# network at configure time. frida-gum's `configure` self-bootstraps a vendored
# meson (releng/meson submodule) and downloads its own toolchain (incl. ninja) +
# SDK glib -- so this adds NO system `meson` requirement. The from-source build
# runs once per clean build dir (guarded below); reconfigures reuse it.
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

set(FRIDA_GUM_VERSION "16.5.9" CACHE STRING "Frida-gum version (git tag)")
# Pin to the exact commit the tag points at, for immutability.
set(FRIDA_GUM_GIT_TAG "e49e63c8dd1961d1490d11b44e86166a6e776c14"
    CACHE STRING "Frida-gum git commit (== tag ${FRIDA_GUM_VERSION})")

include(FetchContent)
FetchContent_Declare(
    frida_gum
    GIT_REPOSITORY "https://github.com/frida/frida-gum.git"
    GIT_TAG "${FRIDA_GUM_GIT_TAG}"
    GIT_SHALLOW TRUE
    # Submodules (releng + the vendored meson) are fetched by frida's own
    # `configure` via `git submodule update --init --recursive`; skip FetchContent's
    # handling so we don't also pull heavy test-data submodules.
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(frida_gum)  # no root CMakeLists -> populate only

set(_frida_devkit "${frida_gum_SOURCE_DIR}/build/gum/devkit")
set(_frida_lib "${_frida_devkit}/libfrida-gum.a")

# Build the x86-only devkit once (guarded on the produced archive). A clean build
# dir re-fetches the source and rebuilds; a reconfigure reuses the existing devkit.
if(NOT EXISTS "${_frida_lib}")
    message(STATUS "FridaGum: building x86-only devkit from source (downloads frida toolchain+SDK; first time only)...")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
                ./configure "--prefix=${frida_gum_SOURCE_DIR}/_unused-prefix" --
                -Ddevkits=gum -Ddefault_library=static
                -Dcapstone:archs=x86 --force-fallback-for=capstone
        WORKING_DIRECTORY "${frida_gum_SOURCE_DIR}"
        RESULT_VARIABLE _frida_cfg_res
    )
    if(NOT _frida_cfg_res EQUAL 0)
        message(FATAL_ERROR "FridaGum: frida-gum ./configure failed (exit ${_frida_cfg_res})")
    endif()

    execute_process(
        COMMAND make
        WORKING_DIRECTORY "${frida_gum_SOURCE_DIR}"
        RESULT_VARIABLE _frida_build_res
    )
    if(NOT _frida_build_res EQUAL 0)
        message(FATAL_ERROR "FridaGum: frida-gum build failed (exit ${_frida_build_res})")
    endif()

    if(NOT EXISTS "${_frida_lib}")
        message(FATAL_ERROR "FridaGum: build succeeded but ${_frida_lib} is missing")
    endif()
endif()

add_library(frida::gum STATIC IMPORTED GLOBAL)
set_target_properties(frida::gum PROPERTIES
    IMPORTED_LOCATION "${_frida_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_frida_devkit}"
)
target_link_libraries(frida::gum INTERFACE dl rt pthread resolv m)

message(STATUS "FridaGum: ${FRIDA_GUM_VERSION} x86-only devkit -> ${_frida_lib}")
