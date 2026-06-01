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
