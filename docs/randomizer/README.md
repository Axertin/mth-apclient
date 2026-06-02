# Mina the Hollower - Randomizer Backend Reference

Canonical game-side reference for the randomizer backend. *Mina the Hollower* ships
a complete built-in item/door randomizer (exposed in-game as some of the **"Weird" modifiers**).
Rather than reimplement completely from scratch, the backend reuses the game's own location/item
model: it reads the location set, detects checks, and grants received items through
the same data the game uses.

**Provenance.** Offsets below were extracted from the Linux build, GNU Build ID
`a40f4f641e247efced331ff77e0f2c68d465bc36` (in-game `1.0.5 [r147980]`). Addresses
are **ELF virtual addresses** (a disassembler that rebases the PIE to 0x100000
shows them `+0x100000`).

**These addresses are build-specific.** Function addresses move between revisions
(confirmed: `r147980` -> `r148053`, same semver, functions shifted, data tables
byte-identical), and the shipping Windows binary is a stripped PE. So the client
does **not** resolve by symbol name -- it keys a per-build offset table on the
Steam build id (`ISteamApps::GetAppBuildId()` == the in-game `[r]` revision) and
fails closed on an unknown build; a signature scanner replaces the table later.
See `PLAN.md`. The un-stripped Linux build (and the preserved `r147980` backup) is
a development reference only.

---

## 1. Name hashing - `hashlittle2`

Every game-side identifier (locations, rooms, objects, types) is a **Bob Jenkins
lookup3 `hashlittle2`** hash with fixed seed **`pc = 13 (0x0d)`, `pb = 37 (0x25)`**.

- 64-bit identifiers are stored as `(pb << 32) | pc`.
- 32-bit identifiers store `pc` (the low word).

Reference (validated against the published vector
`hashlittle("Four score and seven years ago", 0) == 0x17770551`):

```python
u64 mina_hash(bytes key):
    pc, pb = 13, 37
    hashlittle2(key, len(key), &pc, &pb)   # standard lookup3 little-endian
    return (pb << 32) | pc
```

The engine helper `ObjectUtil::GetObjectHashName(stateIdx, objName)` builds the
string `basename(gameStateName) + "_" + objName` and hashes it with the seed above.

---

## 2. Locations

Locations are the entries of the table **`s_rItemCollection`** (the game's term for
the set of collectible slots). There are **361 entries**; ~320 are real checks
(`None` and most `Lock` entries are not pickups - see section 6).

**Canonical location id = the entry's `+0x00` u64 name hash.** It is unique across
all real locations and is `mina_hash(canonicalName)` where the canonical name is:

| Location kind                       | Canonical name                       | Example                        |
| ----------------------------------- | ------------------------------------ | ------------------------------ |
| World object (numeric)              | `<gamestate>_o_<id>`                 | `boneBeachA_o_5565`            |
| World object (named)                | `<gamestate>_<objName>`              | `cryptB_Queen`, `hub_LongNose` |
| Fish                                | species name                         | `FishPuffer`                   |
| Shop / machine / base-weapon "menu" | code-constructed (not name-hashable) | -                              |

`<gamestate>` is a level or room name (see the region map, section 7).

The full table is **[`locations.tsv`](locations.tsv)** (361 rows):

| column          | meaning                                                                |
| --------------- | ---------------------------------------------------------------------- |
| `idx`           | array index 0..360 (stable per build)                                  |
| `region`        | region id (`s_rItemCollection +0x20`), see section 7                          |
| `regionName`    | level name for the region                                              |
| `vanillaItem`   | item type placed here in the unrandomized game (`s_rItems` name)       |
| `sub`           | sub-index for structured slots; `-1` for world-unique slots            |
| `gid`           | object-name GameID (`+0x2c`); set only for world-placed unique objects |
| `h00`           | **canonical 64-bit location id** (`+0x00`)                             |
| `canonicalName` | recovered in-game name (227/360 known)                                 |
| `displayName`   | canonical name, else synthesized `region - vanillaItem #sub`           |

Of 360 real entries, **227 have a recovered canonical name**. The remaining 133 are
shop purchases, bonestone-machine upgrades, and base-weapon picks - "menu" locations
with no world object, so their hash inputs are code-constructed rather than derived
from a name. They remain uniquely identified by `(region, vanillaItem, sub)`.

---

## 3. Items

Item types are the table **`s_rItems`** (195 entries). The item type id is the array
index. Full data in **[`items.tsv`](items.tsv)**:

| column                | meaning                                        |
| --------------------- | ---------------------------------------------- |
| `itemTypeId`          | index 0..194                                   |
| `internalName`        | `kItemType_*`                                  |
| `assetName`           | short/asset name (`Whip`, `Key`)               |
| `nameKey` / `descKey` | localization keys for display name/description |
| `storageKind`         | how the collected bit is stored (see section 5)       |
| `iconAtlas`           | icon atlas path                                |

Category counts (by item type, across locations): Key x50, Lock x40, Bonestone
(5 tiers) x111, Health/Vial/Magic/Spark/Trinket upgrades, ~70 unique trinkets,
16 fish, weapons, sidearm/armor/fishing upgrades.

---

## 4. In-memory data structures

ELF vaddrs for this build; resolve the anonymous-namespace symbols by name.

### `s_rItemCollection` - `0x013fd030`, 361 x `0x50`
| off   | type | field                                                              |
| ----- | ---- | ------------------------------------------------------------------ |
| +0x00 | u64  | **canonical location id** (name hash)                              |
| +0x08 | u32  | room/group hash                                                    |
| +0x18 | u32  | **vanilla itemType** (indexes `s_rItems`)                          |
| +0x1c | u32  | **sub** index (`0xffffffff` = world-unique)                        |
| +0x20 | u32  | **region id**                                                      |
| +0x2c | u32  | **GameID** (object-name hash; 0 unless world-placed unique)        |
| +0x4c | u32  | **warpIdx** - randomized contents; `0` on disk, written at runtime |

### `s_rItems` - `0x01410d50`, 195 x `0x68`
+0x00 internalName*, +0x08 assetName*, +0x18 nameKey*, +0x20 descKey*,
**+0x28 storageKind**, +0x30 iconAtlas*, +0x38 icon/anim*, +0x50 pickup sfx*,
+0x58 palette*.

### `s_rWarpData` - `0x01404100`, 467 x `0x70`
Door/room connectivity graph (logic / door-shuffle). Hash-keyed: +0x00 u64, +0x08
u32, +0x28 u32 are room/door name hashes that cross-reference to encode links;
+0x20 is the same region id space as `s_rItemCollection`.

---

## 5. SaveSlot collected-bit layout

The collected state lives in the active `SaveSlot` (`*(g_saveManager + 8)`), keyed
by the item type's `storageKind` (`s_rItems +0x28`):

| kind            | storage                                                    |
| --------------- | ---------------------------------------------------------- |
| 0x01            | bitfield @ +0xc24 (upgrades)                               |
| 0x08            | u64 bitfield @ +0x1f0                                      |
| 0x0b            | int @ +itemType*4+0x258 (==1 => collected)                  |
| 0x0c            | large bitfield @ +0xc08 (<=0xdf; keys/checks)               |
| 0x11            | u64 bitfield @ +0x200                                      |
| 0x13            | bits @ +0xd70                                              |
| (itemType 0x12) | +0x18c bits 0/1/2                                          |
| 0x44-0x48       | equip/weapon bitfields @ +0x170/+0x130/+0x54/+0x18c/+0x950 |
| 0x4a-0x67       | masked flag @ +0xc68                                       |
| 0x5e            | byte @ +0x1c0                                              |
| default         | bitfield @ +0x170                                          |

---

## 6. Notes on the location set

- Index `0` is `None` (sentinel) - not a check.
- The 40 `Lock` entries correspond to door locks (consumed by keys); likely not
  pickups. Confirm before counting them as checks (~320 real checks expected).
- `warpIdx` (`+0x4c`) is `0` in the file; the native fill writes it at save-slot
  activation. A backend that drives placement itself does not rely on it.

---

## 7. Region map (`+0x20` -> level)

```
0  astralOrrery          9  introBeach
1  bayou_overworld      10  hub_mansion
2  bayou                11  mansion
3  boneBeach_overworld  12  hub
4  boneBeachA           13  septemburg_overworld
5  crypt_overworld      14  septemburgB
6  cryptB               15  hub_overworld_south
7  hub_overworld_east   16  hub_overworld_west
8  frozenTrainyardA
```
Regions 17 and a high "upgrade-set" region had no name-resolvable entries; their
level names are not yet recovered.

---

## 8. Runtime API (functions to hook / call)

ELF vaddrs; resolve by symbol. Signatures from analysis.

| function                                                                   | addr                    | role                                                                          |
| -------------------------------------------------------------------------- | ----------------------- | ----------------------------------------------------------------------------- |
| `Items::IsItemCollected(int slot, ItemCollection*, SaveSlot*, bool, bool)` | `0xc3cdd0`              | **location check** - has this slot been taken                                 |
| `Items::SetItemCollected(int slot, bool, ItemCollection*, SaveSlot*)`      | `0xc3cb30`              | grant/clear a slot (receive item)                                             |
| `Items::OnPickup` / `Items::OnPickupDone`                                  | `0xc3adb0` / `0xc3b190` | pickup lifecycle (hook for "checked")                                         |
| `Items::GetItemCollectionItemType(int slot, bool)`                         | `0xc3d7d0`              | item type at a slot                                                           |
| `Items::FindItemCollectionIndexByGameID(uint, bool)`                       | `0xc3ca90`              | slot lookup by GameID                                                         |
| `Items::FindWarpIndex(ulong, bool)`                                        | `0xc3c8a0`              | resolve a warp redirect                                                       |
| `Items::GetFixedLocation`                                                  | `0xc3d8a0`              | non-randomized slots                                                          |
| `Items::CheckAllUpgradesObtained`                                          | `0xc3c770`              | completion/goal check (called after every Set)                                |
| `Items::RandomizeItemWarps(void*)`                                         | `0xc2ee20`              | native fill (runs from `ActivateSaveSlot`)                                    |
| `ActivateSaveSlot`                                                         | call site `0xebfe35`    | triggers the native fill; the interception point for backend-driven placement |
| `ObjectUtil::GetObjectHashName(uint, char*)`                               | `0xc48340`              | builds+hashes object instance names                                           |

Guard flag: `s_randomizeDone` `0x1415c88` (set once the native fill has run).

---

## 9. Backend integration model

1. **Location set** <- `locations.tsv`. Use the `h00` u64 as the stable location id.
2. **Item set** <- `items.tsv` (type id + names).
3. **Bypass the native fill**: intercept at the `ActivateSaveSlot` / `s_randomizeDone`
   gate so placement is backend-driven instead of the game's own shuffle.
4. **Detect checks**: `Items::IsItemCollected(slot)` (poll) and/or the `OnPickup`
   lifecycle -> report the location as checked.
5. **Grant received items**: `Items::SetItemCollected(slot, true, ...)` for the item's
   slot (writes the correct SaveSlot bit per section 5).
6. **Completion**: `Items::CheckAllUpgradesObtained` reflects goal state.

---

## 10. Grant, consume & detection mechanics

**Grant path.** `Items::OnPickupDone(slot, itemType, Player*, ..., mode, ...)` (`0xc3b190`)
applies an item's real effect - `mode == 3` removes, else adds - *and* sets the
location-collected bit (indexed by the slot's `sub`). `Items::GetItem(name)`
(`0xc3a980`) = `hashlittle(name)` -> itemType is a name->id helper.

**Conflation.** For **keys and all upgrades**, "item owned" and "location collected"
are the *same* sub-indexed bit. Bonestones (currency `+0x1ec`) and money are fungible.

**Stats are popcount-derived.** `Player::UpdateStats` computes effective stats from
`popcount` of each category field. So set bits in *unused* sub positions act as "+N"
of that item without corresponding to any real location.

**Keys are a count, not a per-bit consume.** Available keys =
`popcount(SaveSlot+0x1f0) - (SaveSlot+0x1f8 spent-count)`. `KeyBlock::UpdateState`
(`0xc45360`) gates opening on `popcount(player+0x1190, the mirror of +0x1f0) <= spent`;
unlocking increments the spent counter (`+0x1f8`) - bits are never cleared. Vial-blocks
gate on vial counts the same way.

**Doors / Locks.** `KeyBlock::SetSaveUnlocked` (`0xc47870`) sets the door's own **Lock**
bit at `+0x200` (by the door's `sub`). So `Lock` entries are per-location "door opened"
flags - they *are* checkable locations, not mere bookkeeping.

**AP grant recipe (partition the bit space -> no conflation in practice):**
- bonestones -> add to currency `+0x1ec`; money -> `AddMoney`.
- keys -> set a **reserved** free bit in `+0x1f0` (popcount +1 = +1 usable key; which
  bit is irrelevant). When a real key *location* is checked (its real bit set ->
  popcount +1), bump `+0x1f8` by 1 to neutralize the free key.
- upgrades (health/vial/magic/spark/trinket) -> set a **reserved** free bit in the
  category field; `UpdateStats` turns popcount into +1 stat. Real-location subs stay
  reserved for detection (headroom: key 14, health 14, magic/vial 22, spark 28,
  trinket-upg 26, lock 24 free bits).
- uniques (trinkets/weapons/fish) -> set their single bit.

**Detection vs. grant - the one design fork.** Polling real-location bits detects
checks cleanly *provided we never set those real bits ourselves* (AP grants use reserved
bits/counters). The residual case is the **vanilla-on-check grant**: collecting an
upgrade location natively sets its (real) bit, which is both "collected" and "+1 stat".
Two handlings:
- (A) override the fill so locations contain an inert token (sets the collected bit,
  grants no stat); or
- (B) hook `Items::OnPickup` (`0xc3adb0`) to set the collected bit but skip the grant.
Keys/consumables don't need this (compensate via the spent-counter / currency).

## 11. Detection, suppression & hook map (locked design)

The native pickup/chest "already taken?" gate is
`Items::IsItemCollected(slot, 0, 0, true, false)` -- confirmed in `Chest::Chest`
(`0xb5d0b0`, spawns the chest already-open when true) and `Pickup::Pickup`
(`0xdafe50`, `Kill()`s the pickup when true). But ownership/logic checks call the
*same* function with overlapping flags -- `HasCollectedTrinket` also uses
`(true, false)`, `SaveSlot::GetWeaponLevelFromPickup` uses `(true, true)`. So
"is this chest collected?" and "does the player own item X?" cannot be told apart
by arguments, only by **caller**. In vanilla they are identical (1 location : 1
item); AP breaks that 1:1, so they must diverge.

There is no single chokepoint above the leaf: `SpawnPoint::Spawn` (`0xf006c0`) is the
spawn layer but uses its own persistence (`IsEntityPermanentKilled`/`RefSaveData`) and
is NOT an `IsItemCollected` caller. The item-collected gate lives in the pickup
constructors, which run after `SpawnPoint` creates the object. So the spawn-gate set is
the ~6 constructors (`Chest::Chest` `0xb5d0b0`, `Pickup::Pickup` `0xdafe50`, `Upgrade`,
`SpawnBonestone`, `SpawnHealthUpgrade`, `KeyBlock`) -- small and unambiguous vs. the
~25 ownership/NPC callers.

The leaf hook can't be dropped, because of uniques: AP-granting a unique sets its
canonical sub-bit (how `HasCollectedTrinket` reads ownership), which is the SAME bit as
that unique's own location's spawn-gate. After a shuffle, that location usually holds
someone else's item and is unchecked -- yet the bit is set, so its constructor would
kill its own pickup and the location becomes uncheckable. The spawn-gate must read
"checked" (sidecar) while ownership reads the bit: same slot, two answers, separable
only by caller.

**Hooks (option B):**
- **Pickup constructors (the ~6 above) - scope the gate.** On enter set a thread-local
  `spawnGate(slot)` flag; clear on leave. This makes "this is a spawn decision" explicit
  without return-address ranges.
- **`Items::IsItemCollected` (`0xc3cdd0`) - flag-gated.** Return the **checked-locations
  sidecar** answer *only while* `spawnGate` is set; otherwise call through to the
  original (ownership/logic reads the real, AP-driven inventory, untouched).
- **`Items::OnPickupDone` (`0xc3b190`) - suppress + record.** On a randomized-location
  pickup: mark the slot checked in the sidecar, send `LocationChecks`, and return
  WITHOUT the native grant/bit-set. Upstream `Items::OnPickup` presentation still runs
  (the pickup visual plays).

Keys/upgrades come along for free: their real-location bits are never set (AP grants go
to reserved bits), so their gates natively read 0 ("always spawn") and the same
flag/sidecar suppresses only the checked ones.

**Inventory** (AP `ReceivedItems`) is written to the real save fields, never via the
location path:
- uniques (trinkets/weapons/fish) -> the item's **canonical bit** (so ownership
  queries like `HasCollectedTrinket` see it);
- keys/upgrades -> **reserved** free bits (popcount = stat / key count; real-location
  subs are never touched);
- bonestones -> currency `+0x1ec`; money -> `AddMoney`.

**Invariants:** real-location sub-bits are never set by us; "checked" lives only in the
sidecar; "owned" lives only in AP-driven real bits/counters. Caller-filtering is what
keeps the two from colliding when an item's canonical slot is also a world location.

**World representation:** keep the native item objects (correct Mina sprites); only a
foreign-game item at a location needs a dummy sprite.

**Validated at runtime (read-only MVP, build r148053):** real chests call
`OnPickupDone` with a **unique `slot` in `0..360`** in vanilla play (native rando off),
confirming per-location detection and the `ap_loc_id = base + idx` scheme. Ad-hoc
pickups (subweapon ammo, magic/treasure drops, e.g. itemType 21/37/38/40/41) arrive with
`slot = -1` and are correctly dropped by the `slot < 0` guard. The suppression/sidecar
hooks above are NOT yet built -- the read-only MVP only observes + sends; the decoupling
(suppress native grant, AP-driven grants, spawnGate flag) is the next phase.

## 12. Open questions

- Canonical names for the 133 menu locations (shop / bonestone machine / base weapons).
- Level names for regions 17 / 18.
- Decode of `s_rItemCollection +0x10`, `+0x24`, and the `+0x30` property flags.
- Exact site that increments the key spent-counter (`+0x1f8`).
- Enumerate the precise spawn/init caller set for the `IsItemCollected` filter; confirm
  `OnPickupDone` vs `OnPickup` as the cleanest suppress point that preserves presentation.
- Sidecar persistence format (per seed+slot) for checked locations + AP received index.
- `s_rWarpData` decode for full logic/reachability (door-shuffle).

## 13. Other engine surfaces (no built-in console)

The engine embeds **no ImGui and no dev/text console** to reuse. Two systems are
still worth knowing:

- **Debug draw** -- `ycDrawUtil` plus globals `g_debugDrawWorld` /
  `g_debugDrawWorldPersist` / `g_debugDrawHud` and gate `(anon)::s_debugDraw`; the
  managers expose `*Manager::UpdateDebugDraw(ycUpdateQueueContext*)`. It renders
  through the game's own render pass (`ycDrawUtil::Render` / `ycRenderPass`), so
  populating it needs **no present hook**. It is **geometry only** (RectSolid,
  BoxSolid, ellipsoids, lines) -- **no text API**. Good for in-world spatial markers
  (e.g. highlight AP-location objects), not a text HUD. Confirm `s_debugDraw` is
  runtime-flippable before relying on it.
- **CheatManager** -- `IsCheat`, `ToggleCheat`, `ApplyBackerCheats`, menu hooks
  (`OptionsMenuControl_ApplyCheat`). The cheats/modifiers system; the "Weird"
  randomizer is one of these modifiers. Relevant for detecting or forcing the
  rando-modifier state, not as a UI surface.

A text debug console therefore needs a bundled ImGui overlay (present hook:
`vkQueuePresentKHR` on Linux / the D3D12 present on Windows) or an external tool --
there is nothing in-engine to piggyback for text. The display server difference is
moot here: an overlay renders into the game's existing swapchain on both platforms,
so no separate OS window is ever spawned.
