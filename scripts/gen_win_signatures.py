#!/usr/bin/env python3
"""Carve minimal-unique masked signatures and emit a C++ table + JSON sidecar.
Run by hand at RE/regeneration time; NOT part of the CMake build.

  gen_win_signatures.py --binary BIN --mapping scripts/sig_mapping.json \
      --arch {pe,elf} --rva-map {windows,linux} \
      --out-cpp OUT.cpp --out-json OUT.json [--namespace-fn sig_table]

Exits non-zero (via carve) if any signature cannot be made unique in .text.
"""
import argparse, sys

import sigtool  # sibling module; scripts/ is on sys.path when run directly


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--mapping", required=True)
    ap.add_argument("--arch", choices=["pe", "elf"], required=True)
    ap.add_argument("--rva-map", choices=["windows", "linux"], required=True)
    ap.add_argument("--out-cpp", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--namespace-fn", default="sig_table")
    args = ap.parse_args()

    base, text_rva, text = sigtool.load_text(args.binary, args.arch)
    mapping = sigtool.load_mapping(args.mapping)
    rva_map = mapping[args.rva_map]

    entries = []
    for sym in mapping["symbols"]:
        name = sym["name"]
        if name not in rva_map:
            print(f"skip {name}: no {args.rva_map} mapping", file=sys.stderr)
            continue
        e = sigtool.carve_entry(text, text_rva, sym, rva_map[name])
        entries.append(e)
        print(f"ok {name}: {len(e['pattern'])} bytes", file=sys.stderr)

    sigtool.emit_cpp(args.out_cpp, args.namespace_fn, entries)
    sigtool.emit_json(args.out_json, entries)
    print(f"wrote {len(entries)} entries -> {args.out_cpp}, {args.out_json}", file=sys.stderr)


if __name__ == "__main__":
    main()
