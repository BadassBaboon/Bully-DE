#!/usr/bin/env python3
"""Check that every reference to the corona table is relocated.

The corona table is moved from 0x00C660A0 to unused space and expanded from 56
slots to 1024. That only works if EVERY instruction referencing the table is
repointed. Miss one and it keeps reading the old location, which nothing writes
any more, so it reads zeros -- the symptom is corona effects silently
disappearing rather than anything crashing.

That is exactly what happened in 1.1.0: `fld dword ptr [esi+0C660BCh]` at
0x00511774 was missed, and lampposts lost their 2DFX.

Usage:
    python tools/verify_corona_coverage.py [path/to/Bully_unpacked.exe]

The dump is a raw memory image loaded at 0x400000, so a virtual address maps to
a file offset by subtracting the base. Produce one by setting
DumpUnpackedBinary = 1 in the INI.

Exits non-zero if an uncovered reference is found.
"""

import os
import re
import struct
import sys

IMAGE_BASE = 0x400000
TEXT_LO, TEXT_HI = 0x00401000, 0x00900000

# The table is 56 entries of 0x30 bytes -- the stride is proven by the clearing
# loop at 0x00510E60 (`add eax, 30h`). But every real reference points into
# ELEMENT ZERO, because the code addresses fields as [reg + field] with reg
# holding index * 0x30. Scanning the whole table instead of the first element
# only produces false positives from byte alignment: at 0x0047943A a dword read
# straddles `push 1` and `push 0C6h` and lands inside the range by accident.
TABLE_LO = 0x00C660A0
TABLE_HI = TABLE_LO + 0x30

# Known references that must NOT be relocated, with the reason. 0x00C660A0 is
# used here as the end sentinel of a *different* array running 0x00C66080 to
# 0x00C660A0, not as a corona field:
#     510E71: mov esi, offset dword_C66080
#     510E8E: cmp esi, offset dword_C660A0
# Repointing it would break that cleanup loop.
ALLOWED = {
    0x00510E90: "loop sentinel for the array at 0xC66080, not a corona field",
}

HERE = os.path.dirname(os.path.abspath(__file__))
PATCH_SRC = os.path.join(HERE, "..", "src", "features", "CoronaPatches.cpp")


def patched_bytes():
    src = open(PATCH_SRC, encoding="utf-8").read()
    entries = re.findall(r"^\s*\{\s*(0x[0-9A-Fa-f]{8})\s*,\s*(\d+)\s*,", src, re.M)
    covered = set()
    for addr, size in entries:
        base = int(addr, 16)
        covered.update(range(base, base + int(size)))
    return len(entries), covered


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else r"E:\BullyDE\Bully_unpacked.exe"
    if not os.path.isfile(dump):
        print(f"unpacked image not found: {dump}")
        print("set DumpUnpackedBinary = 1 in the INI and launch once to produce it")
        return 2

    count, covered = patched_bytes()
    data = open(dump, "rb").read()

    refs, missed = 0, []
    for off in range(TEXT_LO - IMAGE_BASE, TEXT_HI - IMAGE_BASE - 4):
        value = struct.unpack_from("<I", data, off)[0]
        if TABLE_LO <= value < TABLE_HI:
            addr = off + IMAGE_BASE
            refs += 1
            if addr not in covered and addr not in ALLOWED:
                missed.append((addr, value))

    print(f"patch entries      : {count}")
    print(f"table references   : {refs}")
    print(f"deliberately skipped: {len(ALLOWED)}")
    for addr, why in ALLOWED.items():
        print(f"    0x{addr:08X}  {why}")

    if missed:
        print(f"\nUNCOVERED REFERENCES: {len(missed)}")
        for addr, value in missed:
            print(f"    0x{addr:08X} -> 0x{value:08X}  (table + 0x{value - TABLE_LO:X})")
        print("\nDisassemble each before adding it. A value in this range is not")
        print("automatically a field access; it may be a loop bound or sentinel.")
        return 1

    print("\nall corona table references are relocated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
