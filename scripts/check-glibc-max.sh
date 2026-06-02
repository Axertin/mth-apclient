#!/usr/bin/env bash
#
# Fail the build if the produced .so requires a glibc symbol version newer than
# the Steam Linux Runtime provides. We build on a newer host than the deployment
# target, and binding to too-new versioned symbols makes the mod fail to load
# into the game SILENTLY -- the dynamic loader rejects the .so before our
# constructor runs, so there is no log and the game window never opens.
#
# The runtime floor is GLIBC 2.38 (the base mod already requires __isoc23_*@2.38
# via the net lane and loads fine in-game). If this check trips, find the new
# symbols with `objdump -T <so> | grep GLIBC_2.<n>` and pin them to an old
# version (see cmake/glibc_compat.h).
set -euo pipefail

so="${1:?usage: check-glibc-max.sh <path-to-.so>}"
max_allowed_minor=38

command -v objdump >/dev/null 2>&1 || { echo "check-glibc-max: objdump not found; skipping" >&2; exit 0; }

mapfile -t bad < <(objdump -T "$so" 2>/dev/null \
    | grep -oE 'GLIBC_2\.[0-9]+' | sort -u \
    | awk -F. -v m="$max_allowed_minor" '$2 > m {print}')

if [ "${#bad[@]}" -gt 0 ]; then
    {
        echo "ERROR: $(basename "$so") requires glibc symbol versions newer than GLIBC_2.${max_allowed_minor}:"
        printf '  %s\n' "${bad[@]}"
        echo "These will not resolve in the Steam Linux Runtime, so the mod fails to load"
        echo "in-game with no log. Pin the offending symbols in cmake/glibc_compat.h."
    } >&2
    exit 1
fi

echo "glibc check OK: $(basename "$so") requires <= GLIBC_2.${max_allowed_minor}"
