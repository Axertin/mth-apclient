# mth-apclient: pure game logic (mthap_core) + final mod module (mthap).
#
#   mthap_core : pure, cross-platform logic. The unit tests link ONLY this.
#                No PAL-impl, backend, or OS headers - header includes from
#                src/pal/ are fine, but nothing that needs linking.
#   mthap      : final module (mod.so / mod.dll). Composition root.
#                Added in a later task; guarded on NOT MTHAP_BUILD_TESTS_ONLY.

add_library(mthap_core STATIC
    core/ap/ap_state.cpp
    core/ap/ap_coordinator.cpp
    core/broadcast.cpp
    core/rando_bridge.cpp
    core/dev_commands.cpp
    core/log_ring.cpp
    core/ap/ap_save_state.cpp
    core/inbound_granter.cpp
    core/sig_scan.cpp
    core/lock_registry.cpp
    core/modifier_config.cpp
    core/data/modifier_table.cpp
    core/stat_cap_state.cpp
    core/upgrade_state.cpp
    core/area_reporter.cpp
)

target_include_directories(mthap_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_core PUBLIC cxx_std_23)
set_target_properties(mthap_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_git_version_info(mthap_core)
enable_strict_warnings(mthap_core)
apply_release_optimizations(mthap_core)

if(NOT MTHAP_BUILD_TESTS_ONLY)
    if(MTHAP_ENABLE_NET)
        find_package(asio CONFIG REQUIRED)
        find_package(OpenSSL REQUIRED)
        find_package(ZLIB REQUIRED)
        find_package(nlohmann_json CONFIG REQUIRED)

        add_library(mthap_net STATIC
            net/ap_link_apclient.cpp
            net/deathlink.cpp
        )
        target_include_directories(mthap_net PUBLIC "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(mthap_net PUBLIC
            ASIO_STANDALONE
            AP_NO_SCHEMA
            _WEBSOCKETPP_CPP11_STL_
            _WEBSOCKETPP_CPP11_THREAD_
            _WEBSOCKETPP_NO_BOOST_
            WSWRAP_ASYNC_CLEANUP
        )
        target_link_libraries(mthap_net PUBLIC
            mthap_core
            mthap::apclient
            asio::asio
            OpenSSL::SSL
            OpenSSL::Crypto
            ZLIB::ZLIB
            nlohmann_json::nlohmann_json
        )
        target_compile_features(mthap_net PUBLIC cxx_std_23)
        set_target_properties(mthap_net PROPERTIES POSITION_INDEPENDENT_CODE ON)
        enable_strict_warnings(mthap_net)
        apply_release_optimizations(mthap_net)
    endif()

    # entry.cpp / app.cpp call PAL impls, so they live in the module,
    # not in mthap_core (keeps the pure test link clean).
    add_library(mthap MODULE
        app/entry.cpp
        app/app.cpp
        hooks/game_hooks.cpp
        core/data/game_tables.cpp
        features/player_tracker.cpp
        features/room_tracker.cpp
        features/item_granter.cpp
        features/location_hooks.cpp
        features/boss_hooks.cpp
        features/goal_tracker.cpp
        features/lock_hooks.cpp
        features/chest_hooks.cpp
        features/death_hooks.cpp
        features/ability_hooks.cpp
        features/pawn_shop_hooks.cpp
        features/modifier_hooks.cpp
        features/levelcap_hooks.cpp
    )

    target_link_libraries(mthap PRIVATE
        mthap_core
        mthap_pal
        mthap_mod
    )

    if(MTHAP_ENABLE_NET)
        target_link_libraries(mthap PRIVATE mthap_net)
        target_compile_definitions(mthap PRIVATE MTHAP_HAS_NET)
    endif()

    # The ImGui OBJECT lib is linked PUBLIC into mthap_pal (another OBJECT lib),
    # which propagates its usage requirements (includes/defines) but NOT its
    # object files transitively to this MODULE. The final module must link the
    # ImGui objects directly or the overlay's backend (ImGui_Impl*) / ImGui::
    # symbols stay undefined. (Both platforms, overlay-enabled only; matches src/pal.)
    if(MTHAP_ENABLE_OVERLAY)
        target_link_libraries(mthap PRIVATE mthap::imgui)
        target_sources(mthap PRIVATE ui/dev_console.cpp ui/broadcast_banner.cpp ui/overlay_root.cpp ui/login_window.cpp)
    endif()

    target_compile_features(mthap PRIVATE cxx_std_23)
    enable_strict_warnings(mthap)
    apply_release_optimizations(mthap)

    if(WIN32)
        # Output is `mod.dll`: loaded by the native MinaModAPI from mods/apclient/.
        # CRT linkage is controlled by CMAKE_MSVC_RUNTIME_LIBRARY (per-preset,
        # static via the x64-windows-static triplet).
        set_target_properties(mthap PROPERTIES
            OUTPUT_NAME "mod"
            PREFIX ""
            SUFFIX ".dll"
        )
    else()
        set_target_properties(mthap PROPERTIES
            OUTPUT_NAME "mod"
            PREFIX ""
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
        # Guard against the .so requiring glibc symbol versions newer than the
        # Steam Linux Runtime provides (which makes the mod fail to load in-game
        # silently: no log, no window). Turns that into a loud build failure.
        add_custom_command(TARGET mthap POST_BUILD
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/check-glibc-max.sh" "$<TARGET_FILE:mthap>"
            VERBATIM
            COMMENT "Checking max required glibc symbol version (Steam Runtime compat)")
    endif()

    # Mod manifest: packed semver + configured game-version range. add_git_version_info
    # (called on mthap_core above) exported MTHAP_MOD_VERSION into this scope.
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/mod.yc.in"
        "${CMAKE_BINARY_DIR}/mods/apclient/mod.yc"
        @ONLY
    )

    # Assemble the drop-in: mods/apclient/{mod.yc, mod.<dll|so>}. The library is
    # self-contained (static runtime + deps), so no sidecar libraries ship.
    add_custom_command(TARGET mthap POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:mthap>" "${CMAKE_BINARY_DIR}/mods/apclient/"
        VERBATIM
        COMMENT "Assembling mods/apclient drop-in")
endif()
