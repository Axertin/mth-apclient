# DearImGui.cmake - Dear ImGui via FetchContent, built as an OBJECT lib selecting
# the core TUs + the Vulkan backend. Vendored third-party: warnings silenced, not
# held to our -Werror policy. SDL input is hand-rolled (no imgui_impl_sdl2), so
# only the Vulkan backend is compiled here. Linux-only (the Windows overlay is a
# no-op stub needing none of this).
include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(
    dearimgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dearimgui)

find_package(VulkanHeaders CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED) # headers only; we never link the SDL2 library

add_library(mthap_imgui OBJECT
    "${dearimgui_SOURCE_DIR}/imgui.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_draw.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_tables.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_demo.cpp"
    "${dearimgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

# We resolve Vulkan entry points at runtime from the game's loader; the backend
# must not assume linked prototypes.
target_compile_definitions(mthap_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

target_include_directories(mthap_imgui SYSTEM PUBLIC
    "${dearimgui_SOURCE_DIR}"
    "${dearimgui_SOURCE_DIR}/backends"
)
target_link_libraries(mthap_imgui PUBLIC Vulkan::Headers)

# SDL2 headers only (for the SDL_Event struct + keycodes used by our hand-rolled
# input translator). Never link the SDL2 library - the game has its own static SDL.
#
# vcpkg's SDL2 config package (sdl2:x64-linux) exposes SDL2::SDL2 as an imported
# target with INTERFACE_INCLUDE_DIRECTORIES pointing at
# vcpkg_installed/x64-linux/include/SDL2. SDL2_INCLUDE_DIRS is empty for config-mode
# packages, so we extract the include dir from SDL2::SDL2 directly. SDL2::SDL2-static
# is the fallback if SDL2::SDL2 doesn't exist (some triplets only install the static
# variant).
get_target_property(_sdl2_inc SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _sdl2_inc)
    get_target_property(_sdl2_inc SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
endif()
target_include_directories(mthap_imgui SYSTEM PUBLIC ${_sdl2_inc})

set_target_properties(mthap_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Vendored third-party: silence its warnings (we are -Werror elsewhere).
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(mthap_imgui PRIVATE /W0)
else()
    target_compile_options(mthap_imgui PRIVATE -w)
endif()

# Pin ImGui's libm float calls (sqrtf/acosf/atan2f/...) to ancient glibc symbol
# versions so the LD_PRELOAD .so still loads under the older Steam Linux Runtime
# glibc. Building on a modern host otherwise binds them to e.g. @GLIBC_2.43, which
# the runtime lacks -> the mod fails to load with no log. See cmake/glibc_compat.h.
if(UNIX AND NOT APPLE)
    target_compile_options(mthap_imgui PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/glibc_compat.h")
endif()

add_library(mthap::imgui ALIAS mthap_imgui)
