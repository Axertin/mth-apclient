# mth-apclient: pure game logic (mthap_core) + final mod module (mthap).
#
#   mthap_core : pure, cross-platform logic. The unit tests link ONLY this.
#                No PAL-impl, backend, or OS headers — header includes from
#                src/pal/ are fine, but nothing that needs linking.
#   mthap      : final module (libmthap.so / mthap.dll). Composition root.
#                Added in a later task; guarded on NOT MTHAP_BUILD_TESTS_ONLY.

add_library(mthap_core STATIC
    core/build_id.cpp
)

target_include_directories(mthap_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_core PUBLIC cxx_std_23)
set_target_properties(mthap_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_git_version_info(mthap_core)
enable_strict_warnings(mthap_core)
apply_release_optimizations(mthap_core)

if(NOT MTHAP_BUILD_TESTS_ONLY)
    # mth-apclient.cpp / app.cpp call PAL impls, so they live in the module,
    # not in mthap_core (keeps the pure test link clean).
    add_library(mthap MODULE
        mth-apclient.cpp
        core/app.cpp
    )

    target_link_libraries(mthap PRIVATE
        mthap_core
        mthap_pal
    )

    target_compile_features(mthap PRIVATE cxx_std_23)
    enable_strict_warnings(mthap)
    apply_release_optimizations(mthap)

    if(WIN32)
        # Output is `version.dll`: the game statically imports VERSION.DLL, so
        # the loader maps us in its place (proxy in proxy_version.cpp forwards
        # to the real System32\version.dll). Additive drop-in next to the exe;
        # survives Steam "verify integrity" (no shipped file is renamed).
        set_target_properties(mthap PROPERTIES
            OUTPUT_NAME "version"
            PREFIX ""
            SUFFIX ".dll"
        )
        # CRT linkage is controlled by CMAKE_MSVC_RUNTIME_LIBRARY (per-preset,
        # static via the x64-windows-static triplet).
    else()
        set_target_properties(mthap PROPERTIES
            OUTPUT_NAME "mthap"
            PREFIX "lib"
            SUFFIX ".so"
        )
        # Static-link our C++ runtime so the dynamic linker can't resolve our
        # modern libstdc++ symbols against whatever the Steam runtime provides.
        target_link_options(mthap PRIVATE
            -static-libgcc
            -static-libstdc++
            "LINKER:-z,now"
            "LINKER:--exclude-libs,ALL"
        )
    endif()
endif()
