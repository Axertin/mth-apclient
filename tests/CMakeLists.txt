# Catch2 via FetchContent - keeps the test lane off the vcpkg bootstrap.
include(FetchContent)

set(CATCH2_VERSION "v3.5.4")

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${CATCH2_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(mthap_unit_tests
    unit/smoke_test.cpp
    unit/log_test.cpp
    unit/ap_state_test.cpp
    unit/ap_coordinator_test.cpp
    unit/broadcast_test.cpp
    unit/rando_bridge_test.cpp
    unit/cert_test.cpp
    unit/dev_commands_test.cpp
    unit/log_ring_test.cpp
    unit/ap_ids_test.cpp
    unit/ap_save_state_test.cpp
    unit/inbound_granter_test.cpp
    unit/sig_scan_test.cpp
    unit/riprel_test.cpp
    unit/lock_registry_test.cpp
    unit/modifier_config_test.cpp
    unit/modifier_table_test.cpp
    unit/stat_cap_state_test.cpp
    unit/upgrade_state_test.cpp
    unit/session_policy_test.cpp
    unit/area_reporter_test.cpp
    unit/ability_gate_test.cpp
    unit/game_tables_test.cpp
    unit/goal_state_test.cpp
    unit/mod_api_test.cpp
    unit/connect_resend_gate_test.cpp
)

target_include_directories(mthap_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(mthap_unit_tests PRIVATE
    Catch2::Catch2WithMain
    mthap_core
    mthap_mod
    mthap::minamodapi
)
target_compile_features(mthap_unit_tests PRIVATE cxx_std_23)

# Exercise the real Linux file-log backend's pure entry points (ILog seam,
# set_default_log, logf) without Frida or the module.
target_sources(mthap_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/pal/linux/log_linux.cpp"
    "${CMAKE_SOURCE_DIR}/src/pal/linux/cert_linux.cpp")

enable_strict_warnings(mthap_unit_tests)

catch_discover_tests(mthap_unit_tests)
