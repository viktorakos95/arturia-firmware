#!/usr/bin/env python3
"""Print a bounded Thumb window from a user-supplied, hash-pinned stock image."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ks37.profile import require_stock


def read_window(records, start, size):
    if start & 1 or size < 2 or size > 4096 or size & 1:
        raise ValueError("start must be even; size must be even and in 2..4096")
    offset = start - 0x08000000
    window = bytearray()
    for address in range(offset, offset + size):
        record = next((r for r in records if r.address <= address < r.address + 1024), None)
        if record is None or record.payload is None:
            raise ValueError(f"no explicit payload at flash {address + 0x08000000:#010x}")
        window.append(record.payload[address - record.address])
    return bytes(window)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--start", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--size", type=lambda x: int(x, 0), required=True)
    args = parser.parse_args()
    try:
        records = require_stock(args.firmware.read_bytes())
        data = read_window(records, args.start, args.size)
        try:
            import capstone
        except ImportError:
            raise ValueError("optional dependency missing: install capstone>=5,<6") from None
        engine = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
        consumed = 0
        for insn in engine.disasm(data, args.start):
            print(f"{insn.address:08x}  {insn.bytes.hex():<8}  {insn.mnemonic:<8} {insn.op_str}")
            consumed += insn.size
        if consumed != len(data):
            raise ValueError(f"decoded {consumed}/{len(data)} bytes; window includes undecodable data")
        print("Linear decode only; code/data boundaries require independent analysis.", file=sys.stderr)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Disassembly refused: {error}\n")


if __name__ == "__main__":
    main()
