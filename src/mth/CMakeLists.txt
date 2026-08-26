# mth-apclient: pure game logic (mthap_core) + final mod module (mthap).
#
#   mthap_core : pure, cross-platform logic. The unit tests link this without the
#                module or a hook backend, so: no PAL-impl, backend, or OS headers
#                - header includes from src/pal/ are fine. Portable third-party
#                libraries are allowed provided they resolve with no vcpkg feature
#                selected, since the tests lane requests none.
#                (See tests/CMakeLists.txt for the rest of the test link.)
#   mthap_net  : Archipelago link, guarded on MTHAP_ENABLE_NET.
#   mthap      : final module (mod.so / mod.dll). Composition root.
#                Guarded on NOT MTHAP_BUILD_TESTS_ONLY.

# Header-only, and mthap_core parses the server's slot_data blob, so it is a core
# dependency of both lanes rather than part of the net feature.
find_package(nlohmann_json CONFIG REQUIRED)

# crc32 + raw deflate for the save container. Base vcpkg dependency, so the tests
# lane resolves it with no features selected.
find_package(ZLIB REQUIRED)

# The save container writes on its own thread so the game thread never waits on disk.
find_package(Threads REQUIRED)

add_library(mthap_core STATIC
    core/ap/ap_state.cpp
    core/ap/slot_data.cpp
    core/ap_gate.cpp
    core/ap/ap_coordinator.cpp
    core/broadcast.cpp
    core/rando_bridge.cpp
    core/dev_commands.cpp
    core/log_ring.cpp
    core/login_prefs.cpp
    core/ap/ap_save_state.cpp
    core/inbound_granter.cpp
    core/sig_scan.cpp
    core/lock_registry.cpp
    core/modifier_config.cpp
    core/data/modifier_table.cpp
    core/data/trap_table.cpp
    core/stat_cap_state.cpp
    core/title_menu.cpp
    core/trap_state.cpp
    core/upgrade_state.cpp
    core/wallet_cap_state.cpp
    core/area_reporter.cpp
    core/save/ap_save_store.cpp
    core/save/zip_archive.cpp
    core/save/ap_save_bundle.cpp
    core/save/takeover_state.cpp
)

target_include_directories(mthap_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(mthap_core PUBLIC nlohmann_json::nlohmann_json ZLIB::ZLIB Threads::Threads)
target_compile_features(mthap_core PUBLIC cxx_std_23)
set_target_properties(mthap_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_git_version_info(mthap_core)
enable_strict_warnings(mthap_core)
apply_release_optimizations(mthap_core)

if(NOT MTHAP_BUILD_TESTS_ONLY)
    if(MTHAP_ENABLE_NET)
        find_package(asio CONFIG REQUIRED)
        find_package(OpenSSL REQUIRED)

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
        app/grant_pipeline.cpp
        app/gate_probes.cpp
        app/ap_session.cpp
        app/hook_manager.cpp
        hooks/game_hooks.cpp
        core/data/game_tables.cpp
        features/player_tracker.cpp
        features/room_tracker.cpp
        features/item_granter.cpp
        features/location_hooks.cpp
        features/boss_tracker.cpp
        features/goal_tracker.cpp
        features/kear_completion_tracker.cpp
        features/lock_hooks.cpp
        features/chest_hooks.cpp
        features/death_hooks.cpp
        features/ability_hooks.cpp
        features/ticket_machine_gate.cpp
        features/pawn_shop_hooks.cpp
        features/sewer_cat_gate.cpp
        features/intro_chest_gate.cpp
        features/modifier_hooks.cpp
        features/trap_hooks.cpp
        features/levelcap_hooks.cpp
        features/status_menu_caps.cpp
        features/fountain_lamp_hooks.cpp
        features/title_gate.cpp
        features/save_takeover.cpp
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
        set(_mthap_mod_name "mod.dll")
        set_target_properties(mthap PROPERTIES
            OUTPUT_NAME "mod"
            PREFIX ""
            SUFFIX ".dll"
            # Without this the PDB is named after the target (mthap.pdb) and mod.dll's debug
            # directory points at the wrong file.
            PDB_NAME "mod"
        )
    else()
        set(_mthap_mod_name "mod.so")
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
        # Guard against the .so requiring glibc symbol versions newer than the oldest
        # supported host distro (which makes the mod fail to load in-game silently:
        # no log, no window). Turns that into a loud build failure. See the script's
        # header for why the ceiling is not the sniper container's own glibc.
        add_custom_command(TARGET mthap POST_BUILD
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/check-glibc-max.sh" "$<TARGET_FILE:mthap>"
            VERBATIM
            COMMENT "Checking max required glibc symbol version (host glibc floor)")
    endif()

    # Mod manifest: packed semver + configured game-version range. add_git_version_info
    # (called on mthap_core above) exported MTHAP_MOD_VERSION into this scope. Staged outside the
    # drop-in so the copy rules below, not configure, own everything that lands in mods/apclient/.
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/mod.yc.in"
        "${CMAKE_BINARY_DIR}/generated/mod.yc"
        @ONLY
    )

    # Assemble the drop-in: mods/apclient/{mod.yc, mod.<dll|so>, LICENSE,
    # third_party_notices/}. The library is self-contained (static runtime + deps), so no
    # sidecar libraries ship, but the licences of what is linked in travel with it.
    # These are declared OUTPUTs of a target rather than a POST_BUILD step because a POST_BUILD is
    # invisible to the generator: deleting mods/ (the directory you copy into the game) left the
    # build reporting "no work to do" and shipping nothing. As real outputs they get restored.
    set(_mthap_dropin "${CMAKE_BINARY_DIR}/mods/apclient")
    add_custom_command(
        OUTPUT "${_mthap_dropin}/${_mthap_mod_name}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mthap_dropin}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:mthap>" "${_mthap_dropin}/${_mthap_mod_name}"
        DEPENDS mthap
        VERBATIM
        COMMENT "Assembling mods/apclient drop-in")
    add_custom_command(
        OUTPUT "${_mthap_dropin}/mod.yc"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mthap_dropin}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_BINARY_DIR}/generated/mod.yc" "${_mthap_dropin}/mod.yc"
        DEPENDS "${CMAKE_BINARY_DIR}/generated/mod.yc"
        VERBATIM
        COMMENT "Staging mods/apclient/mod.yc")
    add_custom_command(
        OUTPUT "${_mthap_dropin}/LICENSE"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${_mthap_dropin}/LICENSE"
        DEPENDS "${CMAKE_SOURCE_DIR}/LICENSE"
        VERBATIM
        COMMENT "Staging mods/apclient/LICENSE")

    # mod.pdb ships beside mod.dll so the crash handler's SymFromAddr finds it and resolves frames
    # in-process. MSVC-like frontends only: the MinGW cross-check emits DWARF and no PDB, so
    # TARGET_PDB_FILE would not resolve there.
    set(_mthap_pdb "")
    if(WIN32 AND MSVC)
        add_custom_command(
            OUTPUT "${_mthap_dropin}/mod.pdb"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mthap_dropin}"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_PDB_FILE:mthap>" "${_mthap_dropin}/mod.pdb"
            DEPENDS mthap
            VERBATIM
            COMMENT "Staging mods/apclient/mod.pdb")
        set(_mthap_pdb "${_mthap_dropin}/mod.pdb")
    endif()

    include(Notices)
    mthap_stage_notices("${_mthap_dropin}" _mthap_notices)

    add_custom_target(mthap_dropin ALL
        DEPENDS "${_mthap_dropin}/${_mthap_mod_name}" "${_mthap_dropin}/mod.yc" "${_mthap_dropin}/LICENSE"
        ${_mthap_pdb} ${_mthap_notices})
endif()
