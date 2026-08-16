# FAQ

Common questions about the Archipelago client for **Mina the Hollower**. For
step-by-step setup see the [player guide](user-guide.md).

## Contents

- [General](#general)
- [Setup](#setup)
- [Saves](#saves)
- [Playing](#playing)
- [Problems](#problems)

## General

### What is this?

A native mod that loads into Mina the Hollower and turns it into an Archipelago world. Your chests, kears, shop slots, and other pickups become location checks sent to the server, and items other players find for you are granted in-game.

### Is this made by Yacht Club Games?

No. It is an unofficial community project, not affiliated with or endorsed by Yacht Club Games. Do not report its bugs to them.

### Do other players need the game?

No. Archipelago is cross-game. The people you are randomized with can be playing anything. Only you need Mina the Hollower.

### Can I play it solo?

Yes. A single-player (solo) multiworld generates and plays exactly the same way.

### Does it work on Windows and Linux?

Both. Windows uses `mod.dll`, Linux uses `mod.so`. There is no macOS build.

### Does it work on Steam Deck?

It is the Linux build, and it runs, but there is no on-screen keyboard support for the mod's own windows, so you need a keyboard to type the server address and to use **F1** / **F2**.

## Setup

### Where do the mod files go?

Into the game's **save** directory, not the Steam install folder:

- **Linux**: `~/.local/share/Yacht Club Games/Mina the Hollower/mods/apclient/`
- **Windows**: `%APPDATA%\Yacht Club Games\Mina the Hollower\mods\apclient\`

The folder must hold both the library and `mod.yc`. See [Installing](user-guide.md#installing).

### Why do I need the beta branch?

The mod loader itself only ships on the **experimental-modding** Steam branch. On the default branch there is nothing to load the mod, so it will not run at all.

### Why do I need `-mod -mod-allow-code`?

`-mod` turns the loader on. `-mod-allow-code` permits loading compiled libraries; without it the loader reads `mod.yc` and skips `mod.dll` / `mod.so`, so the game starts looking completely normal and nothing happens.

### Where do I get the apworld and a YAML?

Download the APWorld and an example YAML [on the APWorld releases page](https://github.com/FyreDay/Archipelago-MinaTheHollower/releases).

## Saves

### Does this touch my normal save files?

No. While connected, the mod owns its own save files and suppresses the game's writes to the vanilla slots, so your existing profiles are left alone.

### Where do the randomizer saves live?

- **Linux**: `~/.local/share/mth-apclient/saves/`
- **Windows**: `%LOCALAPPDATA%\mth-apclient\saves\`

One `ap_<seed>_<slot>.zip` per seed and slot, holding both the game save and the randomizer's progress so the two can never disagree. It is an ordinary zip: you can open it to inspect or back it up, but the mod rewrites it as a whole, so edit it at your own risk. A `.zip.bak` alongside it holds the previous version.

Saves from before this layout are picked up automatically the first time a run saves, and the old loose files are left where they are.

### Can I use an existing save file?

No. A run starts from a fresh randomizer save. The mod does not convert or read vanilla saves.

### How do I resume a run?

Launch the game, connect with the same server, slot name, and password, and start. The save is picked by seed plus slot name, so reconnecting to the same seed and slot picks up where you left off.

### I deleted my save file. Can I recover the run?

Not locally. The server still knows every check you sent, and reconnecting to the same seed and slot re-sends the items you had received, but the game world itself starts over: rooms, bosses, and anything not tracked as an Archipelago item or location is gone.

## Playing

### Start Game says "Disconnected" and will not let me in

That is intended. The title screen is gated until the mod is connected to a server, because the run has to know its seed before a save exists. Press **F2**, connect, and the option returns.

### How do I connect?

Press **F2** for the login window, fill in server, slot, and password, and hit Connect. The **F1** console does the same with `connect <server> <slot> [password]`.

### Can I connect after I have already started playing?

No. The server and slot are bound at the title screen, by design. If you get dropped, the client reconnects to the same session on its own; you do not need to re-enter anything mid-run.

### How do I know it is working?

Press **F1** and run `status` for the connection and session state, or `items` for what you have received this session. The console also mirrors the mod's live log.

### What is randomized?

Chests, trinket boxes, kears, shops, health upgrade roses, weapons, and more on the location side; abilities, progressive weapons, trinkets, upgrades, kear locks, bones, and refills on the item side. The full lists are in [Features](user-guide.md#features).

### How do I turn death link on or off?

It follows your YAML, and you can flip it at runtime with `deathlink on` or `deathlink off` in the **F1** console.

### What is the goal?

Whatever your YAML selected: completing a configurable number of generators, or rolling credits. The client watches for it and reports completion on its own.

### How do I know which generators I need to do?

If you have the "N Generators" goal selected, the fountain in the center of Ossex will show the generators you need to do (their lights are still broken). They are laid out in rough direction to their generators, and you can read the inscription on the fountain and look at the colored sections to check which ones are which.

### Are trackers supported?

The client sends standard Archipelago location and item events, so any tracker that speaks the Archipelago protocol works (the APWorld is compatible with Universal Tracker). There is no in-game tracker window.

### Can I use the console commands to give myself items?

`help` lists diagnostic commands (`giveapitem`, `caps`, `ability`, `modifier`, and others). They exist for testing. Using them during a real run will desync you from the server, so do not.

### What sends deathlinks?

A deathlink is sent when you die with no sparks, i.e. a "Sparkless Demise". Dying with an active spark does not send a deathlink.

## Problems

### The game starts but nothing happens

The mod did not load. Check the loader log (`mod.log` in the game's save directory) and work through [The mod does not load](user-guide.md#the-mod-does-not-load-no-login-window). The usual causes are missing launch options, the wrong Steam branch, or files in the install directory instead of the save directory. If you've tried everything and it's still not working, try validating your game files.

### The game crashes on launch

The mod is generally pretty stable, and launch crashes are generally quite rare or the result of very specific circumstances. That said, there are a few known things that could cause launch crashes, so:

- If the game updated recently, make sure you've updated to a mod version which supports the new game revision.
- If you have certain overlay tools like RivaTuner Statistics Server (RTSS) running, they can conflict with the mod's overlays and cause some odd behavior and crashes. A functional workaround for RTSS is to set Application Detection to None for `MinaTheHollower.exe`. See [this post on the AP Discord](https://discord.com/channels/731205301247803413/1538032356546318346/1538413744617689138) for more details.

### It worked yesterday and broke after a game update

`mod.yc` declares a minimum supported game build and the loader refuses anything older, but the upper bound is deliberately left open, so a newer beta build always loads whether or not the mod was tested against it. The mod checks itself at startup instead: it reports the verdict to its log (the `gate:` lines) and, when something it depends on has moved, shows an in-game banner. That report is not enforced by default, so the client keeps running in a degraded state rather than switching itself off. The beta branch moves; grab a matching mod release, and check `mod.log` for the loader's version-check line.

### How do I report a bug?

Open an issue at [github.com/Axertin/mth-apclient/issues](https://github.com/Axertin/mth-apclient/issues) and attach the mod runtime log for the run (`mthap_*.log`, one per launch, under `~/.local/share/mth-apclient/` or `%LOCALAPPDATA%\mth-apclient\`). Say what you were doing, the room, and your seed and slot.
