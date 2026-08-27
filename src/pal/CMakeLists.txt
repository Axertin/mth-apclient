# Platform Abstraction Layer.
#
# `mthap_pal` is an OBJECT library (not STATIC) so native_mod_entry.cpp (which
# defines the exported MinaMod_Init symbol) is pulled into the final `mthap`
# module unconditionally - a STATIC archive would let the linker drop that
# unreferenced .o file and silently break the mod.

if(WIN32)
    set(_mthap_pal_platform
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
    set(_mthap_pal_platform
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
        set(_mthap_pal_overlay windows/overlay_windows.cpp)
    else()
        set(_mthap_pal_overlay linux/overlay_linux.cpp)
    endif()
endif()

# llvm-mingw + static link needs an __imp__vsnprintf shim for vcpkg's OpenSSL
# (msvcrt dllimport); clang-cl/MSVC does not. Only relevant when the net lane
# (which pulls OpenSSL) is built.
if(MINGW AND MTHAP_ENABLE_NET)
    set(_mthap_pal_mingw windows/mingw_crt_shim.cpp)
endif()

# The unqualified TUs are platform-neutral: each one reads only struct offsets that match on both
# builds, or reaches a divergent offset through a pal:: seam such as active_save_slot. Anything whose
# offsets differ stays in linux/ or windows/ above. An unset optional variable expands to nothing.
set(_mthap_pal_sources
    ${_mthap_pal_platform}
    ${_mthap_pal_overlay}
    ${_mthap_pal_mingw}
    log.cpp                # file logger; only log_dir() is per-platform
    native_mod_entry.cpp   # MinaMod_Init; the export macro is the only divergence, inside the TU
    shop_palette.cpp       # palette-mapped text coloring
    boneup_desc.cpp        # bone-up cap annotation; every vtable hop is checked, so drift is rejected
    game_burrow.cpp        # Player burrow state machine
    game_cheat_manager.cpp # runtime cheat mirror behind the AP traps; the save-mask half is per-platform
    game_menu_hooks.cpp    # pawn shop, fountain bulb, title StartGame, new-file kit detours
    game_save_takeover.cpp # mod-save flush trigger, symbol-resolved through the PAL
    game_shop_ui.cpp       # ShopMenu accessors plus the SetCursor and Shop::Get detours
    game_train.cpp         # train and weapon enforcement
    game_upgrades.cpp      # capacity-upgrade grants
)

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
