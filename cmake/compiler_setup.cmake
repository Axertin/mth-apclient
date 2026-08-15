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

# Release ships debug info
function(apply_release_optimizations target)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -O3 -flto -gline-tables-only -gz=zlib)
        target_link_options(${target} PRIVATE -flto -gz=zlib LINKER:--gc-sections LINKER:--compress-debug-sections=zlib)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # clang-cl: the cl driver silently discards GCC-style -O3 ("argument unused during
        # compilation") on every TU. CMAKE_CXX_FLAGS_RELEASE already carries /O2 /Ob2 /DNDEBUG,
        # which is the equivalent, so there is nothing to add here.
        # /Z7 keeps CodeView in the objects rather than a per-TU PDB (no writer contention under
        # ninja); /DEBUG makes the linker emit the single mod.pdb.
        target_compile_options(${target} PRIVATE /Z7 -gline-tables-only)
        target_link_options(${target} PRIVATE /DEBUG)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # No thin-LTO: it turns the static libs into LLVM-bitcode archives that a
        # non-LTO consumer linked with GNU ld can't read ("file format not
        # recognized"), and the gain is negligible at this size. If LTO is wanted
        # later, add -flto=thin here *and* -fuse-ld=lld to the link (+ install lld
        # in CI) so every link is LTO-aware. clang-cl on Windows uses lld-link and
        # would be fine, but we keep both clang frontends consistent.
        target_compile_options(${target} PRIVATE -O3 -gline-tables-only -gz=zlib)
        target_link_options(${target} PRIVATE -gz=zlib LINKER:--compress-debug-sections=zlib)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # /OPT:REF /OPT:ICF are explicit because /DEBUG otherwise flips them off.
        target_compile_options(${target} PRIVATE /O2 /Ob2 /GL /Gy /Z7)
        target_link_options(${target} PRIVATE /LTCG /DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    endif()
endfunction()
