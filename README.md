# mth-apclient

An [Archipelago](https://archipelago.gg/) randomizer client for **Mina the Hollower**,
delivered as an in-process mod that loads into the running game on Linux and Windows.

This project is **not affiliated with or endorsed by** Yacht Club Games. Efforts are made
to not interfere with saves more than necessary, but use at your own risk.

## Install & run

You need Mina the Hollower on Steam, switched to the modding beta, plus the mod files for
your platform. Build them yourself (see [CONTRIBUTING.md](CONTRIBUTING.md)) or download a
release archive.

1. **Opt into the modding beta.** In Steam, right-click the game -> **Properties** ->
   **Betas**, and select the **experimental-modding** branch.

2. **Set the launch options** (Properties -> General):

   ```
   -mod -mod-allow-code
   ```

3. **Drop in the mod files.** Copy the release's `apclient/` folder into the game's `mods`
   directory, which lives under its **save** directory (the SDL prefs path), not the install
   folder:

   - **Linux**: `~/.local/share/Yacht Club Games/Mina the Hollower/mods/apclient/`
     (contains `mod.so` and `mod.yc`)
   - **Windows**: `%APPDATA%\Yacht Club Games\Mina the Hollower\mods\apclient\`
     (contains `mod.dll` and `mod.yc`)

4. **Launch and connect.** Start the game through Steam. When the mod loads, a login window
   appears. Enter your server, slot, and password, then connect. Toggle the window any time
   with **F2**. The **F1** dev console offers the same via `connect <server> <slot> [pw]`.

The full walkthrough, the dev-console commands, the complete feature list, and troubleshooting
are in the **[player guide](docs/user-guide.md)**. If the mod does not load or a connection
fails, start with [Troubleshooting](docs/user-guide.md#troubleshooting).

## What it does

The mod hooks the game's item/location system and bridges it to an Archipelago server:
locations (chests, trinket boxes, kears, shops, health roses, weapons, and more) report as
checks, and received items are granted in-game. It supports configurable goals and deathlink.
See the [player guide](docs/user-guide.md#features) for the full feature list.

## How it works

The mod is a native game mod loaded by Mina the Hollower's built-in mod loader. The library is
`mod.so` on Linux and `mod.dll` on Windows, and both need `mod.yc` alongside. It hooks a handful
of game functions and bridges them to Archipelago. See [docs/architecture.md](docs/architecture.md)
for the full design and [docs/](docs/) for all documentation.

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

Full instructions are in [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
