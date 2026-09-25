"""Read-only helper for inspecting call sites in the installed 1.61.1.1 executable."""

from collections import Counter
from pathlib import Path
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64


def main(path: Path) -> None:
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    section = pe + 24 + optional_size
    sections = []
    for index in range(section_count):
        entry = section + 40 * index
        name = data[entry:entry + 8].rstrip(b"\0").decode("ascii")
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
        sections.append((name, rva, virtual_size, raw, raw_size))

    def file_offset(rva: int) -> int:
        for _, start, virtual_size, raw, raw_size in sections:
            if start <= rva < start + max(virtual_size, raw_size):
                return raw + rva - start
        raise ValueError(hex(rva))

    model_load = 0x015D99F0
    text_section = next(row for row in sections if row[0] == ".text")
    _, text_rva, _, text_raw, text_size = text_section
    text = data[text_raw:text_raw + text_size]
    sites = []
    for offset in range(len(text) - 5):
        if text[offset] != 0xE8:
            continue
        relative = struct.unpack_from("<i", text, offset + 1)[0]
        caller = text_rva + offset
        if caller + 5 + relative == model_load:
            sites.append(caller)
    print("model_load direct callers:", len(sites))
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    for caller in sites[:100]:
        start = max(text_rva, caller - 80)
        code = data[file_offset(start):file_offset(caller + 160)]
        insns = list(decoder.disasm(code, start))
        targets = []
        for inst in insns:
            if inst.mnemonic == "call" and inst.op_str.startswith("0x"):
                targets.append((inst.address, int(inst.op_str, 16)))
        print(hex(caller), " ".join(f"{a:x}->{t:x}" for a, t in targets))
    for target in (0x41CC20, 0x13CC810, 0x13CC750):
        hits = []
        for offset in range(len(text) - 5):
            if text[offset] == 0xE8 and text_rva + offset + 5 + struct.unpack_from("<i", text, offset + 1)[0] == target:
                hits.append(text_rva + offset)
        print("target", hex(target), "calls", len(hits), [hex(x) for x in hits[:80]])
    functions = []
    pdata = next(row for row in sections if row[0] == ".pdata")
    raw = pdata[3]
    for pos in range(raw, raw + pdata[4] - 12, 12):
        begin, end, _ = struct.unpack_from("<III", data, pos)
        if begin < end:
            functions.append((begin, end))
    for begin, end in functions:
        if end - begin > 5000 or begin < text_rva or end > text_rva + text_size:
            continue
        code = data[file_offset(begin):file_offset(end)]
        calls = [int(insn.op_str, 16) for insn in decoder.disasm(code, begin)
                 if insn.mnemonic == "call" and insn.op_str.startswith("0x")]
        if 0x41CC20 in calls and 0x13CBD60 in calls:
            print("parameter+transform", hex(begin), [hex(x) for x in calls])
    for start, end in ((0x975AD0, 0x975DE5), (0x16C4A43, 0x16C4AFE), (0x629310, 0x629642)):
        print("\nFunction", hex(start))
        code = data[file_offset(start):file_offset(end)]
        for insn in decoder.disasm(code, start):
            if insn.mnemonic == "call":
                print(f"{insn.address:08x} {insn.mnemonic:8} {insn.op_str}")
    for start, end in ((0x15E14B0, 0x15E1540), (0x975B70, 0x975C25)):
        print("\nDetail", hex(start))
        for insn in decoder.disasm(data[file_offset(start):file_offset(end)], start):
            print(f"{insn.address:08x} {insn.mnemonic:8} {insn.op_str}")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
