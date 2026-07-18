# Reverse-engineering and game hooks

How the client locates and hooks game functions, and how to add a Windows
signature by hand when the game updates. Read [architecture.md](architecture.md)
first for the target layout and the PAL boundary.

The game runs on a custom engine (`Propeller`). It renders with Vulkan on Linux and
Direct3D 12 on Windows. Game-logic classes appear as `Player::`, `Mina::`,
`CombatCore::`, boss classes, and `*Component::`. The engine layer is `yc*`. The
update system is queue-based (`X::Update(ycUpdateQueueContext*)`).

## Two hooking mechanisms

The client intercepts game code two ways. It prefers the first.

### Native named mod hooks

The game's mod loader exposes named hook points. Register a callback with
`mm->InstallHook("<name>", ...)`. It fires wherever the game runs that hook site,
on both platforms, and from every inlined copy of the target. Documented hook
points include `FixedUpdate`, `WorldUpdate`, `WorldConstruct`, `WorldDestroy`,
`GameStateTransition`, `GameShutdown`, and `IsItemCollected`.

Prefer a named hook wherever the game exposes one. It is platform-agnostic and
survives inlining. Confirm a named hook fires in-game before trusting it over a
working detour. If `GetGameRevision()` returns 0 at startup, the native hooks are
inert on that build. Any grant or collection redirect built on them then silently
does nothing.

### Signature and symbol detours

For a function with no named hook, the client installs a detour through a hook
backend behind the `pal::IHookEngine` seam. The backend is Frida-Gum on Linux and
MinHook on Windows. The function address is resolved per platform behind
`pal::resolve_game_symbol(name)`.

On Linux the shipping build is not stripped and ships DWARF. Functions resolve by
mangled symbol name via Frida's symbol table. Frida also provides Stalker and
MemoryAccessMonitor for runtime RE against the unstripped binary.

On Windows the shipping PE is stripped. Functions are located by scanning the
loaded module's `.text` section for masked byte signatures, described below.

## Windows signatures

On Windows, `pal::resolve_game_symbol(name)` scans `.text` for a masked byte
signature instead of resolving by name. The pure matcher is
`src/mth/core/sig_scan.cpp` (`mth::sig::find_masked` / `resolve`), linked into
`mthap_core` and unit-tested in `tests/unit/sig_scan_test.cpp`. There are two
entry kinds. A `Code` entry resolves to the function address directly. A
`DataRef` entry resolves a global through a RIP-relative `disp32`.

### Identifying a signature by hand

Signatures are normally found by hand in a disassembler. Open the binary in
Ghidra and work from the function's disassembly and its raw bytes. The goal is a
byte pattern that matches the target function exactly once in `.text`.

1. Locate the function in Ghidra and confirm its identity. Uniqueness alone does
   not prove a pattern sits at the right function, so confirm it another way.
   Check its neighbors and its callers and callees. For example, `Items::OnPickup`
   should sit immediately before `Items::OnPickupDone`.
2. Read the function's bytes from the start of its prologue.
3. Mask out the volatile bytes. Any byte that moves between builds becomes a
   wildcard. That covers the `disp32` operand of relative calls and jumps
   (`E8` / `E9`), RIP-relative displacements, and absolute addresses. Keep the
   opcode bytes and any register or immediate encoding that stays fixed.
4. Keep enough fixed bytes for a unique match. The matcher anchors on the longest
   fixed-byte run, so a long unbroken run near the prologue works well.
5. For a `DataRef`, use the instruction that references the global. Record the
   offset of the `disp32` within it (`disp_off`) and the instruction's length
   (`next_insn`), so the scanner can follow the RIP-relative reference.

### The signature files

Two git-tracked files drive the Windows scan.

- `scripts/win_signatures.json` is the source of truth. Each entry has a `name`,
  a `kind` (`Code` or `DataRef`), a `pattern` (the byte array), a `mask` (1 for a
  fixed byte, 0 for a wildcard), and for a `DataRef` the `disp_off` and
  `next_insn`.
- `src/pal/windows/win_signatures_generated.cpp` is generated from that JSON and
  holds `pal::sig_table()`. It is excluded from `format.sh`.

`scripts/sig_mapping.json` records per-symbol RVAs and `DataRef` reference sites.
It is a carving and validation input, not a runtime dependency.

To add or change a signature, edit `win_signatures.json`, then regenerate the
`.cpp` from it. Run this from `scripts/`:

```bash
python3 -c 'import json,sigtool; sigtool.emit_cpp("../src/pal/windows/win_signatures_generated.cpp","sig_table",json.load(open("win_signatures.json")))'
```

Then `clang-format -i` the `.cpp` to match the committed wrapping.

### Validating uniqueness

Validation is a local step. It needs the game binary, so never run it in CI. The
gate is uniqueness. Every fingerprint must match exactly once in `.text`. A
`MISS`, `AMBIG`, or `OOB` result fails.

```bash
python3 scripts/validate_signatures.py --binary <MinaTheHollower.exe> \
  --arch pe --table scripts/win_signatures.json \
  --mapping scripts/sig_mapping.json --expect-map windows
```

`--expect-map` is informational. It prints a `DRIFT` note when a fingerprint
resolves to an address other than the mapped RVA. It does not fail the run,
because addresses move every build. Uniqueness cannot catch a pattern carved at a
lookalike function. The manual identity check in step 1 is what guards against
that.

### Tooling and the automated carver

The tooling needs a few Python packages:

```bash
pip install --user iced-x86 pefile pyelftools
```

The disassembler is `iced-x86`. Capstone's detail mode is broken on the current
Python wheel, so it is not used.

An automated carver, `scripts/gen_win_signatures.py`, can generate the whole
table from the RVAs in `sig_mapping.json`. It is non-deterministic across the
table, so it is not used to add a single symbol. Hand-editing
`win_signatures.json` is the normal path.

Why addresses go stale: function addresses move between builds even at the same
semantic version. Data tables have stayed byte-identical across a revision bump
while functions shifted. Because absolute addresses are build-specific, the
Windows resolver never keys on them at runtime. It scans for signatures instead.

## The PAL boundary

Platform divergence goes behind the `pal::` seam. Never `#ifdef _WIN32` in
`mth/`. When a function needs a different implementation per platform, extend the
PAL interface and provide the two implementations under
`src/pal/{linux,windows}/`. Do not branch inside the core or the composition
layer. This keeps `mthap_core` pure and unit-testable without a real platform.

Patching `.text` needs executable-preserving memory changes. Use
`pal::patch_code`, which restores read and execute and flushes the instruction
cache. Do not use `pal::make_writable`, which grants read and write only and
drops execute.
