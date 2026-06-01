# Platform Abstraction Layer.
#
# `mthap_pal` is an OBJECT library (not STATIC) so loader-injected entry points
# (the Linux constructor in entry_linux.cpp, the Windows DllMain) are pulled
# into the final `mthap` module unconditionally — a STATIC archive would let
# the linker drop those unreferenced .o files and silently break the mod.

if(WIN32)
    set(_mthap_pal_sources
        windows/entry_windows.cpp
        windows/log_windows.cpp
        windows/module_windows.cpp
        windows/thread_windows.cpp
        windows/hook_engine_windows.cpp
    )
else()
    set(_mthap_pal_sources
        linux/entry_linux.cpp
        linux/log_linux.cpp
        linux/module_linux.cpp
        linux/thread_linux.cpp
        linux/hook_engine_linux.cpp
    )
endif()

add_library(mthap_pal OBJECT ${_mthap_pal_sources})

target_include_directories(mthap_pal PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_pal PUBLIC cxx_std_23)
set_target_properties(mthap_pal PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_strict_warnings(mthap_pal)
apply_release_optimizations(mthap_pal)

if(WIN32)
    target_link_libraries(mthap_pal PUBLIC minhook::minhook)
else()
    target_link_libraries(mthap_pal PUBLIC frida::gum dl pthread)
endif()
