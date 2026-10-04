#!/usr/bin/env python3
"""Local analysis helper: flat image + Thumb disassembly with literal/BL annotation."""
import sys, struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ks37.container import parse
import capstone
BASE = 0x08004000
def image(path="local/keystep37_Firmware_Update_1_1_6_579.led"):
    recs = parse(Path(path).read_bytes())
    return b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in recs)
def dis(img, start, end, out=sys.stdout):
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
    md.detail = False
    a = start
    while a < end:
        chunk = img[a-BASE:a-BASE+4]
        ins = next(md.disasm(chunk, a), None)
        if ins is None:
            w = struct.unpack_from("<H", img, a-BASE)[0]
            print(f"{a:08x}  {w:04x}      .short 0x{w:04x}", file=out); a += 2; continue
        note = ""
        if ins.mnemonic.startswith("ldr") and "[pc" in ins.op_str:
            try:
                off = int(ins.op_str.split("#")[1].rstrip("]"), 0)
                lit = ((a + 4) & ~3) + off
                v = struct.unpack_from("<I", img, lit-BASE)[0]
                note = f"   ; [{lit:08x}] = 0x{v:08x}"
            except Exception: pass
        print(f"{a:08x}  {ins.bytes.hex():<8}  {ins.mnemonic:<8} {ins.op_str}{note}", file=out)
        a += ins.size
if __name__ == "__main__":
    img = image(sys.argv[3] if len(sys.argv) > 3 else "local/keystep37_Firmware_Update_1_1_6_579.led")
    dis(img, int(sys.argv[1], 0), int(sys.argv[2], 0))
