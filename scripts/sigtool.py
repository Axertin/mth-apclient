"""Shared signature primitives: ELF/PE .text loading, iced-x86 based byte
wildcarding, masked scanning, and minimal-unique signature carving. Imported by
gen_win_signatures.py and validate_signatures.py so carve and scan never drift.
Not referenced by any CMakeLists; pure dev-time tooling."""
import json

from iced_x86 import Code, Decoder, OpKind

# Operand kinds whose (relative) target immediate shifts between builds.
_NEAR_BRANCH = (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64)


def load_mapping(path):
    with open(path) as f:
        return json.load(f)


def load_text(path, arch):
    """Return (image_base, text_rva, text_bytes) for a PE or ELF file."""
    if arch == "pe":
        import pefile
        pe = pefile.PE(path, fast_load=True)
        for s in pe.sections:
            if s.Name.rstrip(b"\x00") == b".text":
                return pe.OPTIONAL_HEADER.ImageBase, s.VirtualAddress, s.get_data()
        raise SystemExit("no .text in PE")
    from elftools.elf.elffile import ELFFile
    with open(path, "rb") as f:
        sec = ELFFile(f).get_section_by_name(".text")
        # PIE ELF: image base 0, sh_addr is the virtual address of .text.
        return 0, sec["sh_addr"], sec.data()


def _wildcard_offsets(decoder, instr):
    """Byte offsets within an instruction whose value shifts between builds:
    the RIP-relative memory displacement and relative-branch (call/jmp/jcc)
    target immediates. Offsets come from iced's constant-offset table, so REX /
    prefixes are accounted for automatically."""
    offs = set()
    co = decoder.get_constant_offsets(instr)
    if instr.is_ip_rel_memory_operand and co.displacement_size:
        for i in range(co.displacement_size):
            offs.add(co.displacement_offset + i)
    if co.immediate_size and any(instr.op_kind(i) in _NEAR_BRANCH for i in range(instr.op_count)):
        for i in range(co.immediate_size):
            offs.add(co.immediate_offset + i)
    return offs


def count_matches(text, pattern, mask, cap=2):
    """Count masked matches of pattern in text, stopping once cap is reached."""
    n, plen = 0, len(pattern)
    for i in range(len(text) - plen + 1):
        if all((not mask[j]) or text[i + j] == pattern[j] for j in range(plen)):
            n += 1
            if n >= cap:
                break
    return n


def first_match_offset(text, pattern, mask):
    plen = len(pattern)
    for i in range(len(text) - plen + 1):
        if all((not mask[j]) or text[i + j] == pattern[j] for j in range(plen)):
            return i
    return -1


def _is_anchor(instr):
    """True if the instruction cross-references code or data: a RIP-relative
    memory operand, or a near call/branch. The opcode+modrm of such a site is
    function-identifying body content (the relative/displacement bytes are still
    masked), so including one makes the signature fail-to-match (loud) on a real
    code change instead of silently matching a lookalike prologue elsewhere."""
    if instr.is_ip_rel_memory_operand:
        return True
    return any(instr.op_kind(i) in _NEAR_BRANCH for i in range(instr.op_count))


def carve(text, text_rva, start_rva, max_bytes=80, want_anchor=True):
    """Grow a masked pattern from start_rva until it is unique in text AND (when
    want_anchor) it has captured at least one cross-reference instruction. Returns
    (pattern_bytes, mask_bytes). Falls back to the minimal-unique pattern if no
    cross-ref appears within max_bytes."""
    off = start_rva - text_rva
    if not 0 <= off < len(text):
        raise SystemExit(f"start rva {start_rva:#x} is outside .text [{text_rva:#x}, {text_rva + len(text):#x})")
    code = bytes(text[off:off + max_bytes + 16])
    decoder = Decoder(64, code, ip=start_rva)
    pattern, mask, pos = bytearray(), bytearray(), 0
    unique = anchored = False
    for instr in decoder:
        if instr.code == Code.INVALID or instr.len == 0:
            break
        wc = _wildcard_offsets(decoder, instr)
        anchored = anchored or _is_anchor(instr)
        for k in range(instr.len):
            pattern.append(code[pos + k])
            mask.append(0 if k in wc else 1)
        pos += instr.len
        unique = unique or count_matches(text, pattern, mask) == 1
        if unique and (anchored or not want_anchor):
            return bytes(pattern), bytes(mask)
        if len(pattern) >= max_bytes:
            break
    if unique:
        return bytes(pattern), bytes(mask)  # unique but no cross-ref within cap
    raise SystemExit(f"could not carve a unique pattern from rva {start_rva:#x}")


def carve_entry(text, text_rva, sym, mapinfo):
    """Carve one symbol into a JSON-serialisable entry dict.

    DataRef entries MUST supply disp_off and next_insn explicitly: they pin the
    disp32 location and the next-instruction offset the C++ runtime adds for the
    RIP-relative resolve. Defaulting them would silently emit a wrong-but-valid
    signature the runtime cannot detect, so a missing field is a hard error."""
    kind = sym["kind"]
    if kind == "DataRef":
        if "disp_off" not in mapinfo or "next_insn" not in mapinfo:
            raise SystemExit(f"{sym['name']}: DataRef mapping must specify disp_off and next_insn")
        start, disp_off, next_insn = mapinfo["ref_site_rva"], mapinfo["disp_off"], mapinfo["next_insn"]
    else:
        start, disp_off, next_insn = mapinfo["rva"], 0, 0
    pattern, mask = carve(text, text_rva, start)
    return {"name": sym["name"], "kind": kind, "pattern": list(pattern), "mask": list(mask),
            "disp_off": disp_off, "next_insn": next_insn}


def emit_json(path, entries):
    with open(path, "w") as f:
        json.dump(entries, f, indent=2)


def emit_cpp(path, fn, entries):
    def arr(name, vals):
        body = ", ".join(f"0x{v:02x}" for v in vals)
        return f"static constexpr std::uint8_t {name}[] = {{{body}}};"
    header = ["// GENERATED by scripts/gen_win_signatures.py; do not edit by hand.",
              '#include "pal/windows/signature_scan.hpp"', "", "namespace pal", "{", ""]
    # An empty entry list can't form a zero-length C array, so emit an empty span.
    if not entries:
        L = header + [f"std::span<const mth::sig::Entry> {fn}()", "{", "    return {};", "}", "", "} // namespace pal", ""]
        with open(path, "w") as f:
            f.write("\n".join(L))
        return
    L = list(header)
    for i, e in enumerate(entries):
        L.append(arr(f"kPat{i}", e["pattern"]))
        L.append(arr(f"kMask{i}", e["mask"]))
    L += ["", "static constexpr mth::sig::Entry kEntries[] = {"]
    for i, e in enumerate(entries):
        kind = "mth::sig::Kind::Code" if e["kind"] == "Code" else "mth::sig::Kind::DataRef"
        L.append(f'    {{"{e["name"]}", {kind}, kPat{i}, kMask{i}, sizeof(kPat{i}), {e["disp_off"]}, {e["next_insn"]}}},')
    L += ["};", "",
          f"std::span<const mth::sig::Entry> {fn}()", "{",
          "    return {kEntries, sizeof(kEntries) / sizeof(kEntries[0])};", "}", "",
          "} // namespace pal", ""]
    with open(path, "w") as f:
        f.write("\n".join(L))
