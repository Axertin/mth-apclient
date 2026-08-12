#!/usr/bin/env bash
#
# Fail the build if the produced .so requires a glibc symbol version newer than the glibc
# the game resolves against at runtime. We build on a newer host than the deployment
# target, and binding to too-new versioned symbols makes the mod fail to load
# into the game SILENTLY: the dynamic loader rejects the .so before our
# constructor runs, so there is no log and the game window never opens.
#
# The ceiling is NOT the sniper container's own glibc (steamrt3 ships 2.31). Steam's
# pressure-vessel uses whichever of the host and container glibc is newer, so what the
# mod actually has to fit inside is the oldest HOST distro we support. The game binary
# itself maxes out at GLIBC_2.34.
# Floor is 2.35 today (covers Ubuntu 22.04): src/pal/linux/glibc_compat_linux.cpp provides
# __isoc23_* and arc4random locally; the remaining _dl_find_object@2.35 comes from the static
# unwinder, so reaching 2.34 would need it provided too. If this check trips, find the symbols
# with `objdump -T <so> | grep GLIBC_2.<n>` and pin ours (glibc_compat_linux.cpp / glibc_compat.h).
set -euo pipefail

so="${1:?usage: check-glibc-max.sh <path-to-.so>}"
max_allowed_minor=35

command -v objdump >/dev/null 2>&1 || { echo "check-glibc-max: objdump not found; skipping" >&2; exit 0; }

mapfile -t bad < <(objdump -T "$so" 2>/dev/null \
    | grep -oE 'GLIBC_2\.[0-9]+' | sort -u \
    | awk -F. -v m="$max_allowed_minor" '$2 > m {print}')

if [ "${#bad[@]}" -gt 0 ]; then
    {
        echo "ERROR: $(basename "$so") requires glibc symbol versions newer than GLIBC_2.${max_allowed_minor}:"
        printf '  %s\n' "${bad[@]}"
        echo "These will not resolve on the oldest supported host distro, so the mod fails to load"
        echo "in-game with no log. Pin the offending symbols in cmake/glibc_compat.h."
    } >&2
    exit 1
fi

echo "glibc check OK: $(basename "$so") requires <= GLIBC_2.${max_allowed_minor}"
