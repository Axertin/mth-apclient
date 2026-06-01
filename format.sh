#!/usr/bin/env bash
#
# Format all first-party C/C++ sources in-place with clang-format.
# Excludes external/ (submodules) and build/. Selection matches the CI
# format gate (.github/workflows/ci.yml) exactly.
#
# Pin note: CI uses clang-format 21.1.8.
#
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found on PATH." >&2
    echo "  Linux:   sudo apt install clang-format   (or: pipx install clang-format==21.1.8)" >&2
    echo "  macOS:   brew install clang-format" >&2
    echo "  Windows: winget install LLVM.LLVM" >&2
    exit 1
fi

mapfile -d '' -t files < <(
    git ls-files -z '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.hxx' \
        | grep -zvE '^(external|build)/'
)

if [ "${#files[@]}" -eq 0 ]; then
    echo "No C/C++ sources to format."
    exit 0
fi

printf 'Formatting %d files with %s...\n' "${#files[@]}" "$(clang-format --version)"
clang-format -i -- "${files[@]}"
echo "Done."
