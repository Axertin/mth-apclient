# Platform Abstraction Layer.
#
# `mthap_pal` is an OBJECT library (not STATIC) so loader-injected entry points
# (the Linux constructor in entry_linux.cpp, the Windows DllMain) are pulled
# into the final `mthap` module unconditionally - a STATIC archive would let
# the linker drop those unreferenced .o files and silently break the mod.

if(WIN32)
    set(_mthap_pal_sources
        windows/entry_windows.cpp
        windows/log_windows.cpp
        windows/module_windows.cpp
        windows/thread_windows.cpp
        windows/hook_engine_windows.cpp
        windows/proxy_version.cpp
        windows/cert_windows.cpp
        windows/overlay_windows.cpp
    )
else()
    set(_mthap_pal_sources
        linux/entry_linux.cpp
        linux/log_linux.cpp
        linux/module_linux.cpp
        linux/thread_linux.cpp
        linux/hook_engine_linux.cpp
        linux/cert_linux.cpp
    )
endif()

# Linux overlay (Vulkan/SDL) is built only when the overlay feature is on; the
# Windows overlay stub is always compiled (it needs no third-party deps).
if(NOT WIN32 AND MTHAP_ENABLE_OVERLAY)
    list(APPEND _mthap_pal_sources linux/overlay_linux.cpp)
endif()

add_library(mthap_pal OBJECT ${_mthap_pal_sources})

target_include_directories(mthap_pal PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_pal PUBLIC cxx_std_23)
set_target_properties(mthap_pal PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_strict_warnings(mthap_pal)
apply_release_optimizations(mthap_pal)

if(WIN32)
    # winver.h forward-declares VerLanguageName etc. without dllexport; our
    # proxy redeclares them with dllexport, which clang-cl flags on every
    # PROXY_FWD line. Expected - suppress on this TU only.
    set_source_files_properties(windows/proxy_version.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-dll-attribute-on-redeclaration")

    target_link_libraries(mthap_pal PUBLIC minhook::minhook crypt32)
else()
    target_link_libraries(mthap_pal PUBLIC frida::gum dl pthread)
endif()

# Overlay: link the ImGui object lib (Linux only) and flag the build so App wires
# the dev console. PUBLIC so the final mthap module inherits both the ImGui
# include dirs (to compile DevConsole later) and the MTHAP_HAS_OVERLAY define.
# Windows does NOT define MTHAP_HAS_OVERLAY: its make_overlay is an inert stub
# that App never instantiates.
if(NOT WIN32 AND MTHAP_ENABLE_OVERLAY)
    target_link_libraries(mthap_pal PUBLIC mthap::imgui)
    target_compile_definitions(mthap_pal PUBLIC MTHAP_HAS_OVERLAY)
endif()
