# Platform Abstraction Layer.
#
# `mthap_pal` is an OBJECT library (not STATIC) so native_mod_entry.cpp (which
# defines the exported MinaMod_Init symbol) is pulled into the final `mthap`
# module unconditionally - a STATIC archive would let the linker drop that
# unreferenced .o file and silently break the mod.

if(WIN32)
    set(_mthap_pal_sources
        windows/log_windows.cpp
        windows/module_windows.cpp
        windows/thread_windows.cpp
        windows/hook_engine_windows.cpp
        windows/symbol_windows.cpp
        windows/signature_scan.cpp
        windows/win_signatures_generated.cpp
        windows/cert_windows.cpp
        windows/crash_handler_windows.cpp
        windows/mem_windows.cpp
        windows/game_windows.cpp
    )
else()
    set(_mthap_pal_sources
        linux/log_linux.cpp
        linux/module_linux.cpp
        linux/thread_linux.cpp
        linux/hook_engine_linux.cpp
        linux/symbol_linux.cpp
        linux/cert_linux.cpp
        linux/crash_handler_linux.cpp
        linux/mem_linux.cpp
        linux/game_linux.cpp
        linux/glibc_compat_linux.cpp
    )
endif()

# Overlay source: Linux Vulkan/SDL or Windows D3D12/Win32, compiled only when the
# overlay feature is on. (When off, App never references make_overlay.)
if(MTHAP_ENABLE_OVERLAY)
    if(WIN32)
        list(APPEND _mthap_pal_sources windows/overlay_windows.cpp)
    else()
        list(APPEND _mthap_pal_sources linux/overlay_linux.cpp)
    endif()
endif()

# llvm-mingw + static link needs an __imp__vsnprintf shim for vcpkg's OpenSSL
# (msvcrt dllimport); clang-cl/MSVC does not. Only relevant when the net lane
# (which pulls OpenSSL) is built.
if(MINGW AND MTHAP_ENABLE_NET)
    list(APPEND _mthap_pal_sources windows/mingw_crt_shim.cpp)
endif()

# AP save-slot gate: published each tick by App to gate independent game-thread detours.
list(APPEND _mthap_pal_sources ap_save_gate.cpp)

# Native MinaModAPI entry (MinaMod_Init) - shared across platforms; the export
# macro is the only platform divergence and lives inside the TU.
list(APPEND _mthap_pal_sources native_mod_entry.cpp)

add_library(mthap_pal OBJECT ${_mthap_pal_sources})

target_include_directories(mthap_pal PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_pal PUBLIC cxx_std_23)
set_target_properties(mthap_pal PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(mthap_pal PRIVATE mthap::minamodapi)

enable_strict_warnings(mthap_pal)
apply_release_optimizations(mthap_pal)

if(WIN32)
    target_link_libraries(mthap_pal PUBLIC minhook::minhook crypt32 dbghelp)
else()
    target_link_libraries(mthap_pal PUBLIC frida::gum dl pthread)
endif()

# Overlay: link the ImGui object lib and flag the build so App wires the dev
# console. PUBLIC so the final mthap module inherits the ImGui include dirs and
# the MTHAP_HAS_OVERLAY define.
if(MTHAP_ENABLE_OVERLAY)
    target_link_libraries(mthap_pal PUBLIC mthap::imgui)
    target_compile_definitions(mthap_pal PUBLIC MTHAP_HAS_OVERLAY)
endif()
