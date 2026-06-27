# mth-apclient

An [Archipelago](https://archipelago.gg/) randomizer client for **Mina the Hollower**, delivered
as an in-process mod that injects into the running game on Linux and Windows.

This project is **not affiliated with or endorsed by** Yacht Club Games. Efforts are made to not
interfere with saves more than necessary, but use at your own risk.

## What it does

- **Outbound location checks** detects when you collect a randomized pickup and reports it to the
  Archipelago server.
- **Inbound item grants** applies items the server sends you, in-game.
- **In-game dev console** a Dear ImGui overlay (toggle `F1`) for connecting, checking status,
  and inspecting state.

## How it works

The mod is a native game mod that loads into the game process via Mina the Hollower's built-in
mod loader and hooks game functions:

- **Linux**: `mod.so`, loaded by the game's native mod loader.
- **Windows**: `mod.dll`, loaded by the game's native mod loader.

Both platforms also require `mod.yc` (the mod manifest) alongside the library.

See [docs/architecture.md](docs/architecture.md) for the full design.

## Installing & running

You need the files for your platform - build them yourself (see [CONTRIBUTING.md](CONTRIBUTING.md))
or download a release artifact. The mod requires Mina the Hollower on the **experimental-modding** (password `modsmodsmods`)
with the `mod-allow-code` launch option set (this enables loading a mod's code library).

### Linux

Extract the release `.zip` (containing `apclient/mod.so` and `apclient/mod.yc`) into the game's mods
folder, which lives under its save directory (the SDL prefix path), not the install dir:

```
~/.local/share/Yacht Club Games/Mina the Hollower/mods/
```

Set Steam launch options for Mina the Hollower:

```
-mod -mod-allow-code
```

The game's mod loader writes `~/.local/share/Yacht Club Games/Mina the Hollower/mod.log` each
run (whether a mod loaded, version-check or load failures) - check it first if the mod doesn't
appear. The mod's own runtime log is `~/.local/share/mth-apclient/mthap_*.log` (one file per run).

### Windows

Extract the release `.zip` (containing `apclient/mod.so` and `apclient/mod.yc`) into:

```
%APPDATA%\Yacht Club Games\Mina the Hollower\mods\
```

Set Steam launch options for Mina the Hollower:

```
-mod -mod-allow-code
```

The game's mod loader writes `%APPDATA%\Yacht Club Games\Mina the Hollower\mod.log` each run;
the mod's own runtime log is `%LOCALAPPDATA%\mth-apclient\mthap_*.log`.

## Connecting to a server

An ImGui overlay window should appear allowing connection and disconnection to an AP server. If it
doesn't appear or you want to hide it once connected, it can be toggled by pressing `F2`.

### Other settings

| Environment Variable | Meaning                                                       |
| -------------------- | ------------------------------------------------------------- |
| `MTHAP_CONSOLE_KEY`  | console toggle key (`F1`..`F12` or `BACKQUOTE`); default `F1` |
| `MTHAP_AP_CERT`      | path to a CA certificate bundle for TLS (`wss://`) servers    |

## Building

To build on Linux:

```bash
git clone --recurse-submodules https://github.com/Axertin/mth-apclient
cd mth-apclient
cmake --preset clang-x64-debug
cmake --build --preset clang-x64-debug
```

To build on Windows:

```powershell
git clone --recurse-submodules https://github.com/Axertin/mth-apclient
cd mth-apclient
cmake --preset clang-cl-x64-debug
cmake --build --preset clang-cl-x64-debug
```

Full instructions are in
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
