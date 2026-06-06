# Contributing

Thanks for your interest in mth-apclient. This document covers building, testing, and the
conventions the project follows.

## Prerequisites

- CMake ≥ 3.25, Ninja, and a C++23 compiler (the presets use `clang`/`clang++` (`clang-cl` / `clang++-cl` on Windows)).
- Dependencies are pulled automatically:
  - **vcpkg** — the only git submodule (`external/vcpkg`); provides the networking deps (asio,
    OpenSSL, zlib, nlohmann-json) and the overlay deps (Vulkan headers, SDL2).
  - **Frida-Gum** (Linux hook backend), **MinHook** (Windows hook backend), **Catch2** (tests),
    and the Archipelago client headers (apclientpp / wswrap / websocketpp) are fetched at configure
    time via CMake `FetchContent`.

## Getting the source

```bash
git clone --recurse-submodules https://github.com/Axertin/mth-apclient
cd mth-apclient
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

## Building

The project is preset-driven. Presets are compiler-led; `cmake --list-presets` shows only the ones
valid for your host.

### Linux mod (`libmthap.so`)

```bash
cmake --preset clang-x64-debug      # or clang-x64-release
cmake --build --preset clang-x64-debug
```

### Tests only (fastest loop)

The unit tests link only the pure-logic core, so this needs none of the game-adjacent
dependencies:

```bash
cmake --preset clang-x64-tests
cmake --build --preset clang-x64-tests
ctest --preset clang-x64-tests --output-on-failure
```

### Windows mod (`version.dll`)

The canonical Windows build uses native `clang-cl` with a static CRT (run on Windows or CI):

```bash
cmake --preset clang-cl-x64-release
cmake --build --preset clang-cl-x64-release
```

A LLVM-MinGW cross preset (`mingw-x64-debug`) is available for a fast Windows compile-check from a
Linux box. It is a development aid only; it produces a MinGW-ABI binary, not a shippable artifact.

## Smoke-testing the Linux build

You can load the `.so` without launching the game:

```bash
MTHAP_FORCE_INIT=1 LD_PRELOAD=$PWD/build/clang-x64-debug/src/mth/libmthap.so /bin/sleep 1
cat ~/.local/share/mth-apclient/mthap_*.log
```

`MTHAP_FORCE_INIT=1` bypasses the process-name check the loader constructor normally uses to
restrict itself to the real game.

## Formatting

Formatting is enforced (Allman style, `.clang-format`). Run it before committing:

```bash
bash format.sh
```

…or install the pre-commit hook once:

```bash
python scripts/install-format-hook.py
```

CI gates merges on formatting with a pinned clang-format version.

## Code layout & the platform boundary

The codebase is split into three targets (see [docs/architecture.md](docs/architecture.md)):

- `mthap_core` — pure, cross-platform logic. **It must not include platform, OS, or hook-backend
  headers that require linking**, because the unit tests link only this target.
- `mthap_pal` — the platform abstraction layer (process entry points + hook backend) under
  `src/pal/{linux,windows}/`.
- `mthap` — the final module that composes the two.

When you add platform-specific behavior, put it behind a PAL interface rather than `#ifdef`-ing it
into the core or the higher-level logic.

## Continuous integration

Pull requests run a formatting check, the Linux unit tests, and the Linux and Windows mod builds —
keep them green. Tagged releases (`v*` on `master`) build and publish artifacts automatically.

## Commit conventions

- Conventional-commit prefixes: `feat`, `fix`, `build`, `refactor`, `docs`, `test`, `chore`.
- Keep commit bodies short and to the point.
- Keep history linear and rebase-friendly.

## License

By contributing, you agree that your contributions are licensed under the project's
[MIT License](LICENSE).
