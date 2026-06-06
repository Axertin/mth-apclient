# DearImGui.cmake - Dear ImGui via FetchContent, built as an OBJECT lib. The
# backend pair is selected per platform: Vulkan + hand-rolled SDL input on Linux,
# the stock D3D12 + Win32 backends on Windows. Vendored third-party: warnings
# silenced, not held to our -Werror policy.
include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(
    dearimgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dearimgui)

if(WIN32)
    # Windows: stock D3D12 + Win32 backends. They include only <d3d12.h>,
    # <dxgi*.h>, <windows.h> - all provided by the MSVC SDK and by mingw-w64,
    # so no third-party find_package is needed (unlike the Vulkan/SDL branch).
    add_library(mthap_imgui OBJECT
        "${dearimgui_SOURCE_DIR}/imgui.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_draw.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_tables.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_demo.cpp"
        "${dearimgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
        "${dearimgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    )
    target_include_directories(mthap_imgui SYSTEM PUBLIC
        "${dearimgui_SOURCE_DIR}"
        "${dearimgui_SOURCE_DIR}/backends"
    )
    # Skip the XInput gamepad path (avoids the xinput import lib + runtime dep).
    target_compile_definitions(mthap_imgui PUBLIC IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    # d3dcompiler: imgui_impl_dx12 compiles its shaders at init via D3DCompile.
    # dxguid: COM IID_* symbols. dwmapi: imgui_impl_win32's alpha-compositing
    # helper calls Dwm*. clang-cl picks these up via #pragma comment(lib) too,
    # but mingw ignores those pragmas, so list them explicitly for both.
    target_link_libraries(mthap_imgui PUBLIC d3d12 dxgi dxguid d3dcompiler dwmapi user32)
else()
    # Linux: Vulkan backend; SDL2 headers only (input is hand-rolled). We never
    # link the SDL2 library - the game has its own static SDL.
    find_package(VulkanHeaders CONFIG REQUIRED)
    find_package(SDL2 CONFIG REQUIRED)

    add_library(mthap_imgui OBJECT
        "${dearimgui_SOURCE_DIR}/imgui.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_draw.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_tables.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${dearimgui_SOURCE_DIR}/imgui_demo.cpp"
        "${dearimgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    )

    # We resolve Vulkan entry points at runtime from the game's loader; the
    # backend must not assume linked prototypes.
    target_compile_definitions(mthap_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

    target_include_directories(mthap_imgui SYSTEM PUBLIC
        "${dearimgui_SOURCE_DIR}"
        "${dearimgui_SOURCE_DIR}/backends"
    )
    target_link_libraries(mthap_imgui PUBLIC Vulkan::Headers)

    # SDL2 config package exposes the include dir via the imported target; the
    # *_INCLUDE_DIRS var is empty for config-mode packages.
    get_target_property(_sdl2_inc SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _sdl2_inc)
        get_target_property(_sdl2_inc SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
    endif()
    target_include_directories(mthap_imgui SYSTEM PUBLIC ${_sdl2_inc})

    # Pin ImGui's libm float calls to ancient glibc symbol versions so the
    # LD_PRELOAD .so still loads under the older Steam Linux Runtime glibc.
    target_compile_options(mthap_imgui PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/glibc_compat.h")
endif()

set_target_properties(mthap_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Vendored third-party: silence its warnings (we are -Werror elsewhere).
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(mthap_imgui PRIVATE /W0)
else()
    target_compile_options(mthap_imgui PRIVATE -w)
endif()

add_library(mthap::imgui ALIAS mthap_imgui)
