# Player guide

Install the Archipelago client for **Mina the Hollower**, connect to a server,
and troubleshoot problems.

Not affiliated with or endorsed by Yacht Club Games. It tries not to disturb
saves more than necessary, but use it at your own risk.

## Contents

- [Prerequisites](#prerequisites)
- [Installing](#installing)
- [Connecting to a server](#connecting-to-a-server)
- [The dev console](#the-dev-console)
- [Features](#features)
- [Troubleshooting](#troubleshooting)

## Prerequisites

### 1. Opt into the modding beta

The mod loader ships only on the beta branch. In Steam, right-click **Mina the
Hollower** -> **Properties** -> **Betas**, select **experimental-modding** from
the dropdown, and let Steam update.

### 2. Set the launch options

Under **Properties** -> **General**, set:

```
-mod -mod-allow-code
```

Without `-mod-allow-code` the loader skips the mod's compiled library.

### 3. Get the mod files

Download a release archive for your platform, or build it yourself (see
[CONTRIBUTING.md](../CONTRIBUTING.md)). The archive contains an `apclient/`
folder holding the mod library (`mod.so` on Linux, `mod.dll` on Windows) and
`mod.yc`, the manifest. Keep both files together.

## Installing

Copy the `apclient/` folder into the game's `mods` directory. It lives in the
game's **save** directory (the SDL preferences path), not the Steam install
directory.

- **Linux**: `~/.local/share/Yacht Club Games/Mina the Hollower/mods/apclient/`
  (`mod.so` + `mod.yc`)
- **Windows**: `%APPDATA%\Yacht Club Games\Mina the Hollower\mods\apclient\`
  (`mod.dll` + `mod.yc`)

Launch through Steam. When the mod loads, the login window appears. If it does
not, see [Troubleshooting](#troubleshooting).

## Connecting to a server

The login window has fields for the server, slot (player name), and password,
with Connect and Disconnect buttons. Toggle it with **F2**. You can also connect
from the dev console with `connect <server> <slot> [password]`.

## The dev console

Press **F1** to open an in-game console that mirrors the mod's log and takes
typed commands. Game input is suppressed while it is open. Press **F1** again to
close it.

| Command                        | Effect                              |
| ------------------------------ | ----------------------------------- |
| `help`                         | List all commands.                  |
| `clear`                        | Clear the console output.           |
| `status`                       | Show connection and session status. |
| `items`                        | List items received this session.   |
| `connect <server> <slot> [pw]` | Connect to an Archipelago server.   |
| `disconnect`                   | Disconnect from the server.         |

`help` also lists diagnostic commands (`deathlink`, `giveapitem`, `caps`,
`ability`, `modifier`, `litlamps`) used for testing. Normal play needs none of
them.

## Features

### Locations

Chests, Trinket Boxes, Kears, Legovitch's Arms, the Trinket Bazaar, Panino's
Trinket Stand, The Emporium, Kear Institute Kears, Knitts's Atelier, Poppit's
Shops, Belvedere's Shop, Pinky's Parlor, the Swamp Shack, the Madd House, the
Crow Town Shop, the Tent Vendor, Rupert, Tupert, Health Upgrade Roses, and
Weapons (except the starting weapon chest in the ship's hold), plus the starting
item set.

### Items

Abilities (Burrow, Carry, Swim, Climb, Spring, Bounce), progressive Weapon
upgrades, Trinkets, Trinket Bags, Health / Spark / Magic (Sidearm) / Vial /
Underlab upgrades, Kear Locks (one lock per item), Bone-Up stat caps, Bonestone
and Bones (various quantities), and Magic / Health / Plasma / Vial refills.

### Goals

- N Generators completed (configurable).
- Roll Credits.

### Deathlink

Supported, and also gated by your YAML options. Toggle it at runtime with the
console `deathlink on|off` command.

## Troubleshooting

Two log files hold the diagnostics:

- **Loader log** (written by the game every run, whether or not a mod loaded):
  - Linux: `~/.local/share/Yacht Club Games/Mina the Hollower/mod.log`
  - Windows: `%APPDATA%\Yacht Club Games\Mina the Hollower\mod.log`
- **Mod runtime log** (one file per run, written only if the mod loaded):
  - Linux: `~/.local/share/mth-apclient/mthap_*.log`
  - Windows: `%LOCALAPPDATA%\mth-apclient\mthap_*.log`

### The mod does not load (no login window)

Check the loader `mod.log`. Common causes:

- **Launch options missing.** Confirm `-mod -mod-allow-code` is set. Without
  `-mod-allow-code` the loader skips the code library.
- **Wrong branch.** Confirm the game is on the **experimental-modding** beta.
- **Files in the wrong place.** The `apclient/` folder goes under the save
  directory's `mods/`, not the Steam install folder, and must contain both the
  library and `mod.yc`.
- **Wrong binary for the platform.** Linux uses `mod.so`. Windows uses `mod.dll`.
- **Game-version mismatch.** `mod.yc` declares a supported game-version range. A
  beta build outside it fails the loader's version check (logged in `mod.log`).
  Update the game, or use a matching mod release.

### Connection fails

Check the mod runtime `mthap_*.log`, then verify the server address, slot name,
password, and that the server is reachable.

### A check did not send

Known issue: dying (most often from a received deathlink) while a location check
is mid-send can grant the vanilla item locally without sending the check. See the
tracked [bug reports](https://github.com/Axertin/mth-apclient/issues?q=is%3Aissue+state%3Aopen+label%3Abug)
for current status and other known issues.
