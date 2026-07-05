# Game-provided MinaModAPI surface (cross-platform). Static so it links into both the
# mthap module and the unit tests; the real MinaModAPI* is injected via mod::set_api.
add_library(mthap_mod STATIC mod_api.cpp)

target_include_directories(mthap_mod PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(mthap_mod PUBLIC cxx_std_23)
set_target_properties(mthap_mod PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(mthap_mod PRIVATE mthap::minamodapi)

enable_strict_warnings(mthap_mod)
apply_release_optimizations(mthap_mod)
