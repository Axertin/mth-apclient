# VerifyGitPin.cmake - assert a FetchContent checkout is the commit we expect.
#
# Dependencies pinned by tag stay readable, but a tag is a mutable ref: upstream
# can move it and the next clean fetch silently ships different code. Recording
# the commit the tag resolved to and checking it after populate turns that into a
# hard configure failure instead of a silent swap.
include_guard(GLOBAL)

find_package(Git QUIET)

function(mthap_verify_git_pin name source_dir expected_sha)
    if(NOT GIT_FOUND)
        message(WARNING "${name}: git not found, cannot verify pin ${expected_sha}")
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
        OUTPUT_VARIABLE _actual
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _rc
    )

    # Not a git checkout: a FETCHCONTENT_SOURCE_DIR_* override pointing at a local
    # tree is a deliberate dev workflow, so warn rather than fail.
    if(NOT _rc EQUAL 0)
        message(WARNING "${name}: '${source_dir}' is not a git checkout, pin ${expected_sha} unverified")
        return()
    endif()

    if(NOT _actual STREQUAL expected_sha)
        message(FATAL_ERROR
            "${name}: pinned commit mismatch.\n"
            "  expected: ${expected_sha}\n"
            "  actual:   ${_actual}\n"
            "The upstream tag has moved, or the checkout was modified. Confirm the "
            "change upstream, then update the recorded commit.")
    endif()

    message(STATUS "${name}: verified at ${expected_sha}")
endfunction()
