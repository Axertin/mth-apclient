#!/usr/bin/env python3
"""Local-only signature validator (no CMake, no compile step). Reads a JSON
signature table and asserts each pattern matches EXACTLY ONCE in a binary's
.text -- that uniqueness is the pass/fail gate. A fingerprint resolving to the
wrong-but-lookalike function is caught by verifying the function MANUALLY in
Ghidra at carve time, not here.

--expect-map prints each Code entry's mapped vs resolved address as an
informational DRIFT note when they differ; it does NOT fail the run, because
absolute addresses shift every game build (the recorded RVAs and the binary on
disk are routinely different builds). It only lines up when validating against
the exact build the RVAs were carved from.

  validate_signatures.py --binary BIN --arch {pe,elf} --table scripts/win_signatures.json \
      [--mapping scripts/sig_mapping.json --expect-map {windows,linux}]
"""
import argparse, json, struct, sys

import sigtool  # sibling module; scripts/ is on sys.path when run directly


def resolve_va(text, text_rva, base, e, off):
    """Resolved virtual address, or None if a DataRef disp32 runs past .text
    (mirrors the C++ runtime's bounds check, which returns 0)."""
    if e["kind"] == "Code":
        return base + text_rva + off
    disp_at = off + e["disp_off"]
    if disp_at + 4 > len(text):
        return None
    disp = struct.unpack_from("<i", bytes(text), disp_at)[0]
    return base + text_rva + off + e["next_insn"] + disp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--arch", choices=["pe", "elf"], required=True)
    ap.add_argument("--table", required=True)
    ap.add_argument("--mapping")
    ap.add_argument("--expect-map", choices=["windows", "linux"])
    args = ap.parse_args()

    base, text_rva, text = sigtool.load_text(args.binary, args.arch)
    with open(args.table) as f:
        entries = json.load(f)
    expect = {}
    if args.mapping and args.expect_map:
        expect = sigtool.load_mapping(args.mapping)[args.expect_map]

    ok = 0
    failures = 0
    drift = 0
    for e in entries:
        name = e["name"]
        n = sigtool.count_matches(text, e["pattern"], e["mask"], cap=2)
        if n == 0:
            print(f"MISS   {name}"); failures += 1; continue
        if n > 1:
            print(f"AMBIG  {name}"); failures += 1; continue
        off = sigtool.first_match_offset(text, e["pattern"], e["mask"])
        va = resolve_va(text, text_rva, base, e, off)
        if va is None:
            print(f"OOB    {name} (disp32 runs past .text)"); failures += 1; continue
        exp = expect.get(name, {}).get("rva") if e["kind"] == "Code" else None
        if exp is not None and va != base + exp:
            # Informational only: absolute addresses move every build (see module docstring).
            print(f"DRIFT  {name} resolved {va:#x} (mapped {base + exp:#x}; different build)"); drift += 1; ok += 1; continue
        print(f"OK     {name} -> {va:#x}")
        ok += 1

    tail = f", {drift} address-drift note(s)" if drift else ""
    print(f"\n{ok} ok, {failures} failure(s){tail}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
