# apply_patch.cmake - idempotent, cross-platform `git apply` for FetchContent.
#
# FetchContent's PATCH_COMMAND can re-run against an already-patched source tree
# (any reconfigure that invalidates the populate stamp), and a raw `git apply`
# is not idempotent - it errors with "patch does not apply" the second time.
# This wrapper reverse-checks first and no-ops if the patch is already applied.
#
# Usage:
#   ${CMAKE_COMMAND} -DPATCH=<file> -DWORKDIR=<dir> -P apply_patch.cmake

if(NOT DEFINED PATCH OR NOT DEFINED WORKDIR)
    message(FATAL_ERROR "apply_patch.cmake requires -DPATCH and -DWORKDIR")
endif()

# Already applied? `git apply --reverse --check` succeeds only on a patched tree.
execute_process(
    COMMAND git -C "${WORKDIR}" apply --reverse --check "${PATCH}"
    RESULT_VARIABLE _already OUTPUT_QUIET ERROR_QUIET)
if(_already EQUAL 0)
    message(STATUS "apply_patch: already applied: ${PATCH}")
    return()
endif()

execute_process(
    COMMAND git -C "${WORKDIR}" apply "${PATCH}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "apply_patch: failed to apply ${PATCH} in ${WORKDIR}")
endif()
message(STATUS "apply_patch: applied ${PATCH}")
