# Contributing

Thanks for your interest in mth-apclient. This document covers building, testing, and the
conventions the project follows.

## Prerequisites

- CMake >= 3.25, Ninja, and a C++23 compiler (the presets use `clang`/`clang++` (`clang-cl` / `clang++-cl` on Windows)).
- Dependencies are pulled automatically:
  - **vcpkg**: the only git submodule (`external/vcpkg`). It provides nlohmann-json, the
    networking deps (asio, OpenSSL, zlib), and, on Linux only, the overlay deps (Vulkan headers,
    SDL2 headers). The Windows overlay needs no vcpkg packages: its Dear ImGui backends build
    against the D3D12/Win32 headers the platform SDK already ships.
  - **Dear ImGui** (overlay), **Frida-Gum** (Linux hook backend), **MinHook** (Windows hook
    backend), **Catch2** (tests), and the Archipelago client headers (apclientpp / wswrap /
    websocketpp) are fetched at configure time via CMake `FetchContent`.

## Getting the source

```bash
git clone --recurse-submodules https://github.com/Axertin/mth-apclient
cd mth-apclient
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

## Building

The project is preset-driven. Presets are compiler-led, so `cmake --list-presets` shows only the
ones valid for your host.

### Linux mod (`mod.so`)

```bash
cmake --preset clang-x64-debug      # or clang-x64-release
cmake --build --preset clang-x64-debug
```

### Tests only (fastest loop)

The unit tests skip the module and the hook backends, so this needs none of the game-adjacent
dependencies. They do compile in a couple of Linux PAL sources, so the test lane is Linux-only:

```bash
cmake --preset clang-x64-tests
cmake --build --preset clang-x64-tests
ctest --preset clang-x64-tests --output-on-failure
```

### Windows mod (`mod.dll`)

The canonical Windows build uses native `clang-cl` with a static CRT (run on Windows or CI):

```bash
cmake --preset clang-cl-x64-release
cmake --build --preset clang-cl-x64-release
```

A LLVM-MinGW cross preset (`mingw-x64-debug`) is available for a fast Windows compile-check from a
Linux box. It is a development aid only. It produces a MinGW-ABI binary, not a shippable artifact.

## Testing the Linux build

Copy `build/clang-x64-debug/mods/apclient/` (`mod.so` and `mod.yc`) into the game's mods
directory at `~/.local/share/Yacht Club Games/Mina the Hollower/mods/apclient/` (the SDL pref
path, not the install dir). Launch via Steam with the `-mod -mod-allow-code` options. The game loader
writes `~/.local/share/Yacht Club Games/Mina the Hollower/mod.log` with load diagnostics. The
mod's own runtime log is `~/.local/share/mth-apclient/mthap_*.log`.

## Formatting

Formatting is enforced (Allman style, `.clang-format`). Run it before committing:

```bash
bash format.sh
```

...or install the pre-commit hook once:

```bash
python scripts/install-format-hook.py
```

CI gates merges on formatting with a pinned clang-format version.

## Code layout & the platform boundary

The codebase is split into these targets (see [docs/architecture.md](docs/architecture.md)):

- `mthap_core`: pure, cross-platform logic. **It must not include platform, OS, or hook-backend
  headers**, because the unit tests link it without the module or a backend. Portable third-party
  libraries are allowed provided they resolve with no vcpkg feature selected, since the tests lane
  requests none.
- `mthap_mod`: the wrapper over the game's own mod API, under `src/mod/`. Also test-linked.
- `mthap_net`: the Archipelago link, built only when networking is enabled.
- `mthap_pal`: the platform abstraction layer (process entry points and hook backend) under
  `src/pal/{linux,windows}/`.
- `mthap`: the final module that composes them.

When you add platform-specific behavior, put it behind a PAL interface rather than `#ifdef`-ing it
into the core or the higher-level logic.

See [docs/reverse-engineering.md](docs/reverse-engineering.md) for how game functions are
hooked and resolved on each platform. It covers the native mod hooks, the Frida/MinHook detour
seam, and the Windows signature-table workflow.

## Continuous integration

Pull requests run a formatting check, the Linux unit tests, and the Linux and Windows mod builds.
Keep them green. Tagged releases (`v*` on `master`) build and publish artifacts automatically.

## Commit conventions

- Conventional-commit prefixes: `feat`, `fix`, `build`, `refactor`, `docs`, `test`, `chore`.
- Keep commit bodies short and to the point.
- Keep history linear and rebase-friendly.

## License

By contributing, you agree that your contributions are licensed under the project's
[MIT License](LICENSE).
