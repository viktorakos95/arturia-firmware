#!/usr/bin/env python3
"""Fail if code outside the replaced stock area reads a literal-pool word (or branches/jumps) inside it.
Usage: check_pools.py <lo> <hi> [<dead_lo> <dead_hi>]  (flash addresses of the replaced area; the optional range is stock code that stays
in place but is unreachable, so references from it are ignored; reads local/stock_full.dis)"""
import re, sys
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
dlo, dhi = (int(sys.argv[3], 16), int(sys.argv[4], 16)) if len(sys.argv) > 4 else (0, 0)
bad = []
for l in open('local/stock_full.dis'):
    m = re.match(r'([0-9a-f]{8})\s+\S+\s+(\S+)\s+(.*)', l)
    if not m: continue
    a, mn, rest = int(m.group(1), 16), m.group(2), m.group(3)
    if lo <= a < hi or dlo <= a < dhi: continue
    mm = re.search(r';\s*\[([0-9a-f]{8})\]\s*=', l)
    if mm and lo <= int(mm.group(1), 16) < hi: bad.append((hex(a), 'literal read', mm.group(1)))
    if mn.startswith(('b', 'cb')):
        for t in re.findall(r'#0x([0-9a-f]{6,8})', rest):
            if lo <= int(t, 16) < hi and 0x0800746c != a: bad.append((hex(a), 'branch', t))
    if mn.startswith('adr'):
        for t in re.findall(r'#0x([0-9a-f]+)', rest): bad.append((hex(a), 'adr (check by hand)', t)) if lo <= int(t, 16) < hi else None
print('references into', hex(lo), hex(hi), ':', bad if bad else 'none')
sys.exit(1 if bad else 0)
