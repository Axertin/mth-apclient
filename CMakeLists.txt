cmake_minimum_required(VERSION 3.25)

project(mth-apclient
    VERSION 0.1.0
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MTHAP_BUILD_TESTS "Build unit tests (Catch2)" ON)
option(MTHAP_BUILD_TESTS_ONLY "Build only tests, skip mod build" OFF)

include(compiler_setup)
include(SimpleGitVersion)

# The LLVM-MinGW dev cross routes printf-family calls through msvcrt, which drops
# C99 specifiers like %lld/%zu unless we switch mingw to its own conformant stdio.
# Harmless on the canonical clang-cl (UCRT) and Linux builds, where MINGW is unset.
if(MINGW)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

if(NOT MTHAP_BUILD_TESTS_ONLY)
    if(WIN32)
        include(MinHook)
    else()
        include(FridaGum)
    endif()
    add_subdirectory(src/pal)
endif()

# mthap_core is pure (PAL headers only) and is linked by tests; build it in
# both lanes. The mthap MODULE target inside this subdir is guarded on
# NOT MTHAP_BUILD_TESTS_ONLY.
add_subdirectory(src/mth)

if(MTHAP_BUILD_TESTS OR MTHAP_BUILD_TESTS_ONLY)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        SYMBOLIC)
endif()
