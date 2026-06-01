# compiler_setup.cmake - warning flags and release optimizations

function(enable_strict_warnings target)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow
            -Wno-missing-field-initializers
            -Wno-unused-parameter
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # clang-cl is noisy with C++ back-compat diagnostics that are irrelevant
        # to a C++23 project. Silence them. No-ops on GCC and on Linux clang where
        # they aren't enabled.
        target_compile_options(${target} PRIVATE
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-c++17-compat
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endfunction()

function(apply_release_optimizations target)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -O3 -flto)
        target_link_options(${target} PRIVATE -flto LINKER:--gc-sections)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # No thin-LTO: it turns the static libs into LLVM-bitcode archives that a
        # non-LTO consumer linked with GNU ld can't read ("file format not
        # recognized"), and the gain is negligible at this size. If LTO is wanted
        # later, add -flto=thin here *and* -fuse-ld=lld to the link (+ install lld
        # in CI) so every link is LTO-aware. clang-cl on Windows uses lld-link and
        # would be fine, but we keep both clang frontends consistent.
        target_compile_options(${target} PRIVATE -O3)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /O2 /Ob2 /GL /Gy)
        target_link_options(${target} PRIVATE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    endif()
endfunction()
