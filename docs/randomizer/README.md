# Mina the Hollower — Randomizer Backend Reference

Canonical game-side reference for the randomizer backend. *Mina the Hollower* ships
a complete built-in item/door randomizer (exposed in-game as some of the **"Weird" modifiers**).
Rather than reimplement completely from scratch, the backend reuses the game's own location/item
model: it reads the location set, detects checks, and grants received items through
the same data the game uses.

**Provenance.** All offsets and data below were extracted from the Linux build,
GNU Build ID `a40f4f641e247efced331ff77e0f2c68d465bc36`. The binary is un-stripped
with mangled symbols — **resolve functions/data by symbol name at runtime**; the
literal addresses are for this build only. Addresses are **ELF virtual addresses**
(a disassembler that rebases the PIE to 0x100000 will show them `+0x100000`).

---

## 1. Name hashing — `hashlittle2`

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
(`None` and most `Lock` entries are not pickups — see §6).

**Canonical location id = the entry's `+0x00` u64 name hash.** It is unique across
all real locations and is `mina_hash(canonicalName)` where the canonical name is:

| Location kind                       | Canonical name                       | Example                        |
| ----------------------------------- | ------------------------------------ | ------------------------------ |
| World object (numeric)              | `<gamestate>_o_<id>`                 | `boneBeachA_o_5565`            |
| World object (named)                | `<gamestate>_<objName>`              | `cryptB_Queen`, `hub_LongNose` |
| Fish                                | species name                         | `FishPuffer`                   |
| Shop / machine / base-weapon "menu" | code-constructed (not name-hashable) | —                              |

`<gamestate>` is a level or room name (see the region map, §7).

The full table is **[`locations.tsv`](locations.tsv)** (361 rows):

| column          | meaning                                                                |
| --------------- | ---------------------------------------------------------------------- |
| `idx`           | array index 0..360 (stable per build)                                  |
| `region`        | region id (`s_rItemCollection +0x20`), see §7                          |
| `regionName`    | level name for the region                                              |
| `vanillaItem`   | item type placed here in the unrandomized game (`s_rItems` name)       |
| `sub`           | sub-index for structured slots; `-1` for world-unique slots            |
| `gid`           | object-name GameID (`+0x2c`); set only for world-placed unique objects |
| `h00`           | **canonical 64-bit location id** (`+0x00`)                             |
| `canonicalName` | recovered in-game name (227/360 known)                                 |
| `displayName`   | canonical name, else synthesized `region - vanillaItem #sub`           |

Of 360 real entries, **227 have a recovered canonical name**. The remaining 133 are
shop purchases, bonestone-machine upgrades, and base-weapon picks — "menu" locations
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
| `storageKind`         | how the collected bit is stored (see §5)       |
| `iconAtlas`           | icon atlas path                                |

Category counts (by item type, across locations): Key ×50, Lock ×40, Bonestone
(5 tiers) ×111, Health/Vial/Magic/Spark/Trinket upgrades, ~70 unique trinkets,
16 fish, weapons, sidearm/armor/fishing upgrades.

---

## 4. In-memory data structures

ELF vaddrs for this build; resolve the anonymous-namespace symbols by name.

### `s_rItemCollection` — `0x013fd030`, 361 × `0x50`
| off   | type | field                                                              |
| ----- | ---- | ------------------------------------------------------------------ |
| +0x00 | u64  | **canonical location id** (name hash)                              |
| +0x08 | u32  | room/group hash                                                    |
| +0x18 | u32  | **vanilla itemType** (indexes `s_rItems`)                          |
| +0x1c | u32  | **sub** index (`0xffffffff` = world-unique)                        |
| +0x20 | u32  | **region id**                                                      |
| +0x2c | u32  | **GameID** (object-name hash; 0 unless world-placed unique)        |
| +0x4c | u32  | **warpIdx** — randomized contents; `0` on disk, written at runtime |

### `s_rItems` — `0x01410d50`, 195 × `0x68`
+0x00 internalName*, +0x08 assetName*, +0x18 nameKey*, +0x20 descKey*,
**+0x28 storageKind**, +0x30 iconAtlas*, +0x38 icon/anim*, +0x50 pickup sfx*,
+0x58 palette*.

### `s_rWarpData` — `0x01404100`, 467 × `0x70`
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
| 0x0b            | int @ +itemType*4+0x258 (==1 ⇒ collected)                  |
| 0x0c            | large bitfield @ +0xc08 (≤0xdf; keys/checks)               |
| 0x11            | u64 bitfield @ +0x200                                      |
| 0x13            | bits @ +0xd70                                              |
| (itemType 0x12) | +0x18c bits 0/1/2                                          |
| 0x44–0x48       | equip/weapon bitfields @ +0x170/+0x130/+0x54/+0x18c/+0x950 |
| 0x4a–0x67       | masked flag @ +0xc68                                       |
| 0x5e            | byte @ +0x1c0                                              |
| default         | bitfield @ +0x170                                          |

---

## 6. Notes on the location set

- Index `0` is `None` (sentinel) — not a check.
- The 40 `Lock` entries correspond to door locks (consumed by keys); likely not
  pickups. Confirm before counting them as checks (~320 real checks expected).
- `warpIdx` (`+0x4c`) is `0` in the file; the native fill writes it at save-slot
  activation. A backend that drives placement itself does not rely on it.

---

## 7. Region map (`+0x20` → level)

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
| `Items::IsItemCollected(int slot, ItemCollection*, SaveSlot*, bool, bool)` | `0xc3cdd0`              | **location check** — has this slot been taken                                 |
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

1. **Location set** ← `locations.tsv`. Use the `h00` u64 as the stable location id.
2. **Item set** ← `items.tsv` (type id + names).
3. **Bypass the native fill**: intercept at the `ActivateSaveSlot` / `s_randomizeDone`
   gate so placement is backend-driven instead of the game's own shuffle.
4. **Detect checks**: `Items::IsItemCollected(slot)` (poll) and/or the `OnPickup`
   lifecycle → report the location as checked.
5. **Grant received items**: `Items::SetItemCollected(slot, true, …)` for the item's
   slot (writes the correct SaveSlot bit per §5).
6. **Completion**: `Items::CheckAllUpgradesObtained` reflects goal state.

---

## 10. Open questions

- Canonical names for the 133 menu locations (shop / bonestone machine / base
  weapons) — would require per-system analysis of those purchase paths.
- Level names for regions 17 / 18.
- Decode of `s_rItemCollection +0x10`, `+0x24`, and the `+0x30` property flags.
- Confirm `Lock` entries are non-checks; finalize the real check count.
- `s_rWarpData` decode for full logic/reachability (door-shuffle).
