#!/usr/bin/env python3
"""Cross-binary RE query helper for matching Linux (symbol-rich) game functions
to their stripped Windows PE counterparts by call-graph / constant fingerprint.

Local dev tooling only (NOT part of the build, NOT committed-critical). Builds
cached indexes over both binaries, then answers targeted queries that a human or
a subagent uses to identify a Windows function RVA.

  re_query.py build                     # build/refresh the cached indexes
  re_query.py elf <mangled_symbol>      # Linux fn fingerprint (size, calls, consts, strings)
  re_query.py pe-func <rva_hex>         # PE fn at/containing rva: bounds, calls, leading disasm
  re_query.py pe-callers <rva_hex>      # PE functions that call this rva
  re_query.py pe-calls-imp <name>       # PE functions that call import/export matching <name>
  re_query.py pe-const <hex> [count]    # PE functions whose body uses immediate == <hex>
  re_query.py pe-exp <name_substr>      # resolve export name -> rva(s)

RVAs are module-relative (image base 0x140000000 for the PE; ELF PIE base 0).
"""
import bisect
import os
import pickle
import struct
import sys

from iced_x86 import Code, Decoder, FlowControl, OpKind

HOME = os.environ["HOME"]
PE = f"{HOME}/minathehollower/windows/MinaTheHollower.exe"
ELF = f"{HOME}/.local/share/Steam/steamapps/common/Mina the Hollower/MinaTheHollower"
CACHE = "/tmp/re_query_cache.pkl"


# ---------- PE index ----------
def build_pe():
    import pefile
    pe = pefile.PE(PE)
    secs = {s.Name.rstrip(b"\x00").decode(): s for s in pe.sections}
    text_rva = secs[".text"].VirtualAddress
    text = secs[".text"].get_data()
    pdata = secs[".pdata"].get_data()
    funcs = []
    for off in range(0, len(pdata) - 11, 12):
        b, e, _ = struct.unpack_from("<III", pdata, off)
        if b == 0 and e == 0:
            break
        funcs.append((b, e))
    funcs.sort()
    begins = [f[0] for f in funcs]

    # export RVA -> name, and import IAT-slot RVA -> name
    exp = {}
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if s.name:
                exp[s.address] = s.name.decode()
    imp = {}
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            dll = d.dll.decode()
            for i in d.imports:
                if i.name:
                    imp[i.address - pe.OPTIONAL_HEADER.ImageBase] = f"{dll}:{i.name.decode()}"

    def fstart(rva):
        i = bisect.bisect_right(begins, rva) - 1
        return funcs[i][0] if i >= 0 and funcs[i][0] <= rva < funcs[i][1] else None

    # call graph: caller_func_rva -> list of ('fn',callee_rva)|('imp',name)
    calls = {}
    callers_of = {}  # callee_rva -> set(caller_func_rva)
    imp_callers = {}  # import/export name -> set(caller_func_rva)
    for ins in Decoder(64, bytes(text), ip=text_rva):
        if ins.code == Code.INVALID:
            continue
        if ins.flow_control != FlowControl.CALL:
            continue
        cf = fstart(ins.ip)
        if cf is None:
            continue
        if ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            tgt = ins.near_branch_target
            name = exp.get(tgt)
            calls.setdefault(cf, []).append(("exp", name) if name else ("fn", tgt))
            if name:
                imp_callers.setdefault(name, set()).add(cf)
            else:
                callers_of.setdefault(tgt, set()).add(cf)
        elif ins.is_ip_rel_memory_operand:
            name = imp.get(ins.ip_rel_memory_address)
            if name:
                calls.setdefault(cf, []).append(("imp", name))
                imp_callers.setdefault(name, set()).add(cf)
    # Leaf functions (called via E8 but absent from .pdata, since x64 leaf
    # functions need no unwind data). Treat them as real function entries too.
    leaf = sorted(t for t in callers_of if t not in set(begins))
    starts = sorted(set(begins) | set(leaf))
    return {
        "text_rva": text_rva, "text": text, "funcs": funcs, "begins": begins,
        "starts": starts, "leaf": set(leaf),
        "exp": exp, "imp": imp, "calls": calls,
        "callers_of": {k: sorted(v) for k, v in callers_of.items()},
        "imp_callers": {k: sorted(v) for k, v in imp_callers.items()},
    }


# ---------- ELF index ----------
def build_elf():
    from elftools.elf.elffile import ELFFile
    f = open(ELF, "rb")
    ef = ELFFile(f)
    t = ef.get_section_by_name(".text")
    ta, tb = t["sh_addr"], t.data()
    ro = ef.get_section_by_name(".rodata")
    ra, rd = ro["sh_addr"], ro.data()
    addr2name = {}
    name2addr = {}
    for sec in ef.iter_sections():
        if sec.name in (".symtab", ".dynsym"):
            for y in sec.iter_symbols():
                if y.name and y["st_value"]:
                    addr2name.setdefault(y["st_value"], y.name)
                    name2addr.setdefault(y.name, y["st_value"])
    # PLT resolution: .rela.plt reloc i -> .plt stub i -> dynsym name
    plt2name = {}
    plt = ef.get_section_by_name(".plt") or ef.get_section_by_name(".plt.sec")
    rela = ef.get_section_by_name(".rela.plt")
    if plt is not None and rela is not None:
        dynsym = ef.get_section_by_name(".dynsym")
        entsz = 16
        plt_base = plt["sh_addr"]
        for idx, r in enumerate(rela.iter_relocations()):
            sym = dynsym.get_symbol(r["r_info_sym"])
            if sym.name:
                # common layouts: .plt[0] is the PLT0 stub, entries start at +entsz;
                # .plt.sec has no PLT0. Record both candidate addresses.
                plt2name[plt_base + (idx + 1) * entsz] = sym.name
                plt2name[plt_base + idx * entsz] = sym.name
    return {"ta": ta, "tb": tb, "ra": ra, "rd": rd,
            "addr2name": addr2name, "name2addr": name2addr, "plt2name": plt2name}


def load(rebuild=False):
    if not rebuild and os.path.exists(CACHE):
        with open(CACHE, "rb") as f:
            return pickle.load(f)
    idx = {"pe": build_pe(), "elf": build_elf()}
    with open(CACHE, "wb") as f:
        pickle.dump(idx, f)
    return idx


# ---------- queries ----------
def elf_fn(idx, sym):
    e = idx["elf"]
    a = e["name2addr"].get(sym)
    if a is None:
        print(f"symbol not found: {sym}")
        return
    off = a - e["ta"]
    print(f"{sym}  rva={a:#x}")
    calls, consts, strings = [], [], []
    n = 0
    for ins in Decoder(64, bytes(e["tb"][off:off + 8000]), ip=a):
        if ins.code == Code.INVALID:
            break
        n += 1
        if ins.flow_control == FlowControl.CALL and ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            t = ins.near_branch_target
            nm = e["addr2name"].get(t) or e["plt2name"].get(t)
            calls.append(nm or f"sub_{t:#x}")
        if ins.is_ip_rel_memory_operand:
            tgt = ins.ip_rel_memory_address
            if e["ra"] <= tgt < e["ra"] + len(e["rd"]):
                o = tgt - e["ra"]
                z = e["rd"].find(b"\x00", o)
                s = e["rd"][o:z]
                if 4 <= len(s) <= 80 and all(32 <= c < 127 for c in s):
                    strings.append(s.decode())
        for i in range(ins.op_count):
            if ins.op_kind(i) == OpKind.IMMEDIATE32 or ins.op_kind(i) == OpKind.IMMEDIATE64:
                v = ins.immediate(i)
                if 0xFF < v < 0xFFFFFFFFFFFF:
                    consts.append(v)
        if ins.mnemonic and ins.flow_control == FlowControl.RETURN and n > 4:
            pass  # don't stop early; let the 8000-byte window bound it
    print(f"  ~{n} insns scanned (capped window)")
    print(f"  calls (named lib/internal, in order): {calls[:40]}")
    print(f"  distinctive immediates: {sorted(set(hex(c) for c in consts))[:25]}")
    print(f"  strings: {strings[:10]}")


def _func_end(p, start):
    j = bisect.bisect_right(p["begins"], start) - 1
    if j >= 0 and p["funcs"][j][0] == start:
        return p["funcs"][j][1]  # real .pdata end
    i = bisect.bisect_right(p["starts"], start) - 1  # leaf: next start
    return p["starts"][i + 1] if 0 <= i + 1 < len(p["starts"]) else start + 0x400


def _fstart(p, rva):
    """Containing function start, leaf-aware (.pdata begins ∪ E8 call targets)."""
    starts = p["starts"]
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0:
        return None
    s = starts[i]
    return s if s <= rva < _func_end(p, s) else None


def _func_calls(p, start, end):
    """Ordered call sequence of any function (incl. leaf), disassembled on demand."""
    off = start - p["text_rva"]
    out = []
    for ins in Decoder(64, bytes(p["text"][off:end - start]), ip=start):
        if ins.code == Code.INVALID:
            break
        if ins.flow_control != FlowControl.CALL:
            continue
        if ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            t = ins.near_branch_target
            nm = p["exp"].get(t)
            out.append(("exp", nm) if nm else ("fn", t))
        elif ins.is_ip_rel_memory_operand:
            nm = p["imp"].get(ins.ip_rel_memory_address)
            if nm:
                out.append(("imp", nm))
    return out


def pe_func(idx, rva):
    p = idx["pe"]
    fs = _fstart(p, rva)
    if fs is None:
        print(f"no function contains {rva:#x}")
        return
    end = _func_end(p, fs)
    kind = "leaf" if fs in p["leaf"] else "pdata"
    seq = p["calls"].get(fs) or _func_calls(p, fs, end)
    named = [v for k, v in seq if k in ("imp", "exp")]
    sub = [f"sub_{t:#x}" for k, t in seq if k == "fn"]
    print(f"PE func {fs:#x}..{end:#x} (size {end - fs:#x}) [{kind}]  export={p['exp'].get(fs)}")
    print(f"  named calls (in order): {named[:40]}")
    print(f"  internal callees: {sub[:20]}")
    off = fs - p["text_rva"]
    print("  --- leading disasm ---")
    for i, ins in enumerate(Decoder(64, bytes(p["text"][off:off + 96]), ip=fs)):
        if ins.code == Code.INVALID or i > 14:
            break
        print(f"    {ins.ip:#x}: {ins}")


def pe_disasm(idx, rva, n=40):
    """Raw disassembly of n instructions from any rva (leaf-safe), annotating
    call targets and RIP-relative data/import references."""
    p = idx["pe"]
    off = rva - p["text_rva"]
    for i, ins in enumerate(Decoder(64, bytes(p["text"][off:off + n * 15]), ip=rva)):
        if ins.code == Code.INVALID or i >= n:
            break
        ann = ""
        if ins.is_ip_rel_memory_operand:
            t = ins.ip_rel_memory_address
            nm = p["imp"].get(t) or p["exp"].get(t)
            ann = f"   ; -> {nm or hex(t)}"
        elif ins.flow_control == FlowControl.CALL and ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            t = ins.near_branch_target
            ann = f"   ; -> {p['exp'].get(t) or f'sub_{t:#x}'}"
        print(f"  {ins.ip:#x}: {ins}{ann}")


def pe_callers(idx, rva):
    p = idx["pe"]
    fs = _fstart(p, rva) or rva
    cs = p["callers_of"].get(fs, [])
    print(f"{len(cs)} callers of {fs:#x}: {[hex(c) for c in cs[:60]]}")


def pe_calls_imp(idx, name):
    p = idx["pe"]
    hits = {k: v for k, v in p["imp_callers"].items() if name.lower() in k.lower()}
    for k in sorted(hits):
        print(f"{k}: {len(hits[k])} callers {[hex(c) for c in hits[k][:30]]}")


def pe_const(idx, val, limit=40):
    p = idx["pe"]
    found = []
    for ins in Decoder(64, bytes(p["text"]), ip=p["text_rva"]):
        if ins.code == Code.INVALID:
            continue
        for i in range(ins.op_count):
            if ins.op_kind(i) in (OpKind.IMMEDIATE32, OpKind.IMMEDIATE64, OpKind.IMMEDIATE32TO64) and ins.immediate(i) == val:
                fs = _fstart(p, ins.ip)
                if fs:
                    found.append(fs)
    uniq = sorted(set(found))
    print(f"immediate {val:#x} used in {len(uniq)} functions: {[hex(x) for x in uniq[:limit]]}")


def pe_exp(idx, sub):
    p = idx["pe"]
    for rva, nm in sorted(p["exp"].items()):
        if sub.lower() in nm.lower():
            print(f"{nm}: {rva:#x}")


def _elf_sdl_calls(idx, sym):
    """Multiset of SDL_* names a Linux function calls (the vocabulary shared
    verbatim with the PE's SDL exports)."""
    from collections import Counter
    e = idx["elf"]
    a = e["name2addr"].get(sym)
    if a is None:
        return None, None
    off = a - e["ta"]
    c = Counter()
    for ins in Decoder(64, bytes(e["tb"][off:off + 12000]), ip=a):
        if ins.code == Code.INVALID:
            break
        if ins.flow_control == FlowControl.CALL and ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            nm = e["addr2name"].get(ins.near_branch_target) or e["plt2name"].get(ins.near_branch_target)
            if nm and nm.startswith("SDL_"):
                c[nm] += 1
    return a, c


def _pe_sdl_index(idx):
    from collections import Counter
    p = idx["pe"]
    per_func = {}
    freq = Counter()
    for f, seq in p["calls"].items():
        c = Counter(n for k, n in seq if k == "exp" and n and n.startswith("SDL_"))
        if c:
            per_func[f] = c
            for n in c:
                freq[n] += 1
    return per_func, freq


def elf_seq(idx, sym):
    """Full ordered call sequence of a Linux function (named where possible).
    Use to positionally align against pe-seq of a candidate PE function."""
    e = idx["elf"]
    a = e["name2addr"].get(sym)
    if a is None:
        print(f"symbol not found: {sym}")
        return
    off = a - e["ta"]
    i = 0
    for ins in Decoder(64, bytes(e["tb"][off:off + 12000]), ip=a):
        if ins.code == Code.INVALID:
            break
        if ins.flow_control == FlowControl.CALL and ins.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
            t = ins.near_branch_target
            nm = e["addr2name"].get(t) or e["plt2name"].get(t)
            print(f"  [{i:2}] @{ins.ip:#x} -> {nm or f'sub_{t:#x}'}")
            i += 1


def pe_seq(idx, rva):
    """Full ordered call sequence of a PE function (named exports/imports where
    possible, else internal sub_<rva>). Align positionally against elf-seq."""
    p = idx["pe"]
    fs = _fstart(p, rva)
    if fs is None:
        print(f"no function contains {rva:#x}")
        return
    seq = p["calls"].get(fs) or _func_calls(p, fs, _func_end(p, fs))
    for i, (k, v) in enumerate(seq):
        tgt = v if k in ("imp", "exp") else f"sub_{v:#x}"
        print(f"  [{i:2}] {tgt}")


def pe_match(idx, sym):
    a, want = _elf_sdl_calls(idx, sym)
    if a is None:
        print(f"symbol not found: {sym}")
        return
    if not want:
        print(f"{sym}: calls no SDL_* exports — not matchable by SDL fingerprint (use call-graph/structure).")
        return
    per_func, freq = _pe_sdl_index(idx)
    nfuncs = len(per_func)
    # weight each shared SDL name by rarity (idf-style); rare calls discriminate.
    import math
    scored = []
    for f, have in per_func.items():
        s = 0.0
        shared = 0
        for nm, cnt in want.items():
            if nm in have:
                shared += 1
                s += min(cnt, have[nm]) * math.log(nfuncs / freq[nm] + 1.0)
        if shared:
            scored.append((s, shared, f))
    scored.sort(reverse=True)
    print(f"{sym} (rva {a:#x}) calls SDL: {dict(want)}")
    print(f"  top PE candidates (score, #shared SDL names, rva):")
    for s, sh, f in scored[:6]:
        print(f"    {f:#010x}  score={s:6.1f}  shared={sh}/{len(want)}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    if cmd == "build":
        load(rebuild=True)
        print(f"cached -> {CACHE}")
        return
    idx = load()
    if cmd == "elf":
        elf_fn(idx, sys.argv[2])
    elif cmd == "pe-func":
        pe_func(idx, int(sys.argv[2], 16))
    elif cmd == "pe-callers":
        pe_callers(idx, int(sys.argv[2], 16))
    elif cmd == "pe-calls-imp":
        pe_calls_imp(idx, sys.argv[2])
    elif cmd == "pe-const":
        pe_const(idx, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 40)
    elif cmd == "pe-exp":
        pe_exp(idx, sys.argv[2])
    elif cmd == "match":
        pe_match(idx, sys.argv[2])
    elif cmd == "elf-seq":
        elf_seq(idx, sys.argv[2])
    elif cmd == "pe-seq":
        pe_seq(idx, int(sys.argv[2], 16))
    elif cmd == "pe-disasm":
        pe_disasm(idx, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 40)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
