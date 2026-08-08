# apply_patch.cmake - idempotent, cross-platform `git apply` for FetchContent.
#
# FetchContent's PATCH_COMMAND can re-run against an already-patched source tree
# (any reconfigure that invalidates the populate stamp), and a raw `git apply`
# is not idempotent - it errors with "patch does not apply" the second time.
# This wrapper reverse-checks first and no-ops if the patch is already applied.
#
# It also has to survive the patch CONTENT changing. FetchContent's stamps do not
# track that, so the tree can be carrying an older revision of the same patch, which
# fails both the reverse-check and the apply and used to wedge the build directory
# permanently (the only recovery being `rm -rf _deps/<dep>-src`). When that happens
# the tracked files are restored to their pristine checkout and the patch retried.
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
    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
if(_rc EQUAL 0)
    message(STATUS "apply_patch: applied ${PATCH}")
    return()
endif()

# Neither pristine nor patched-with-this-patch: almost always a stale revision of the same
# patch left by an earlier configure. Only meaningful if this really is a git checkout.
execute_process(
    COMMAND git -C "${WORKDIR}" rev-parse --git-dir
    RESULT_VARIABLE _is_git OUTPUT_QUIET ERROR_QUIET)
if(NOT _is_git EQUAL 0)
    message(FATAL_ERROR "apply_patch: failed to apply ${PATCH} in ${WORKDIR} (not a git checkout)")
endif()

# WARNING, not STATUS: this discards every local modification under WORKDIR. Note FetchContent
# drops the patch step's output entirely when it succeeds, so this is only visible when the script
# is run directly; the FATAL_ERROR paths below still surface, because they fail the sub-build.
message(WARNING "apply_patch: ${WORKDIR} carries a different patch; discarding local changes there and re-applying")
execute_process(
    COMMAND git -C "${WORKDIR}" checkout -- .
    RESULT_VARIABLE _restore OUTPUT_QUIET ERROR_QUIET)
if(NOT _restore EQUAL 0)
    message(FATAL_ERROR "apply_patch: could not restore ${WORKDIR}; delete it and reconfigure")
endif()

execute_process(
    COMMAND git -C "${WORKDIR}" apply "${PATCH}"
    RESULT_VARIABLE _rc2)
if(NOT _rc2 EQUAL 0)
    message(FATAL_ERROR "apply_patch: failed to apply ${PATCH} in ${WORKDIR} even after restoring it")
endif()
message(STATUS "apply_patch: applied ${PATCH} (after restore)")
