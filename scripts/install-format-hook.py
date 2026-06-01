#!/usr/bin/env python3
"""Install a git pre-commit hook that auto-formats staged C/C++ sources with clang-format.

The hook formats staged *.c/*.cc/*.cpp/*.cxx/*.h/*.hpp/*.hxx files (excluding external/ and
build/) with `clang-format -i`, then re-stages them, so commits are always formatted. Run once
per clone:

    python scripts/install-format-hook.py

Cross-platform: the emitted hook is bash, which Git for Windows runs under its bundled shell.
"""
from __future__ import annotations

import shutil
import stat
import subprocess
import sys
from pathlib import Path

# The pre-commit hook, written verbatim into .git/hooks/. Kept portable (no bash-4-only
# features) so it runs under git-bash on Windows and the older bash on macOS.
HOOK = r"""#!/usr/bin/env bash
#
# Auto-format staged C/C++ sources with clang-format, then re-stage them.
# Excludes external/ and build/. Installed by scripts/install-format-hook.py.
# Bypass once with: git commit --no-verify
#
set -eu

if ! command -v clang-format >/dev/null 2>&1; then
    echo "pre-commit: clang-format not found on PATH; install it or commit with --no-verify." >&2
    exit 1
fi

# Staged files (added/copied/modified/renamed), C/C++ only, excluding external/ and build/.
files=$(git diff --cached --name-only --diff-filter=ACMR \
    | grep -E '\.(c|cc|cpp|cxx|h|hpp|hxx)$' \
    | grep -vE '^(external|build)/' || true)

if [ -z "$files" ]; then
    exit 0
fi

printf '%s\n' "$files" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    clang-format -i -- "$f"
    git add -- "$f"
done

echo "pre-commit: clang-format applied to staged C/C++ sources."
"""


def git(*args: str) -> str:
    """Run a git command and return trimmed stdout, raising on failure."""
    return subprocess.run(
        ["git", *args], check=True, capture_output=True, text=True
    ).stdout.strip()


def main() -> int:
    try:
        toplevel = Path(git("rev-parse", "--show-toplevel"))
    except FileNotFoundError:
        print("error: git not found on PATH.", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError:
        print("error: not inside a git repository.", file=sys.stderr)
        return 1

    # Resolve the hooks directory, honoring core.hooksPath (and worktrees) if configured.
    hooks_cfg = subprocess.run(
        ["git", "config", "--get", "core.hooksPath"], capture_output=True, text=True
    ).stdout.strip()
    if hooks_cfg:
        cfg_path = Path(hooks_cfg)
        hooks_dir = cfg_path if cfg_path.is_absolute() else toplevel / cfg_path
    else:
        hooks_dir = Path(git("rev-parse", "--absolute-git-dir")) / "hooks"

    hooks_dir.mkdir(parents=True, exist_ok=True)
    hook_path = hooks_dir / "pre-commit"

    if hook_path.exists():
        backup = hook_path.with_name("pre-commit.bak")
        shutil.copy2(hook_path, backup)
        print(f"note: backed up existing pre-commit hook to {backup}")

    # Force LF newlines: a CRLF shebang would break the hook on Unix shells.
    hook_path.write_text(HOOK, encoding="utf-8", newline="\n")
    hook_path.chmod(
        hook_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
    )

    shown = hook_path.relative_to(toplevel) if hook_path.is_relative_to(toplevel) else hook_path
    print(f"installed pre-commit hook at {shown}")
    if shutil.which("clang-format") is None:
        print(
            "warning: clang-format is not on PATH; install it so the hook can run.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
