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

The mod is a single shared library that loads into the game process and hooks game functions:

- **Linux**: `libmthap.so`, loaded via `LD_PRELOAD`; hooks via Frida-Gum.
- **Windows**: — ships as a `version.dll` proxy placed next to `MinaTheHollower.exe` (the game
  imports `VERSION.DLL`); hooks via MinHook. The proxy forwards the real `version.dll` exports, so
  nothing else breaks, and it survives Steam's "verify integrity of game files".

See [docs/architecture.md](docs/architecture.md) for the full design.

## Installing & running

You need the binary for your platform — build it yourself (see [CONTRIBUTING.md](CONTRIBUTING.md))
or download a release artifact.

### Linux

Copy `libmthap.so` into the game's install folder, next to `MinaTheHollower`.

Set a Steam launch option for Mina the Hollower:

```bash
LD_PRELOAD=libmthap.so %command%
```

Logs are written to `~/.local/share/mth-apclient/mthap_*.log` (one file per run).

### Windows

Copy `version.dll` into the game's install folder, next to `MinaTheHollower.exe`.

Logs are written to `%LOCALAPPDATA%\mth-apclient\mthap_*.log`.

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
