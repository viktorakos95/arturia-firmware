#!/usr/bin/env python3
"""Check map identity anchors and direct BL targets against user-supplied stock firmware."""
from __future__ import annotations
import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ks37.profile import require_stock
from render import load

FLASH_BASE = 0x08000000


def read(records, address: int, size: int) -> bytes:
    """Read an exact covered range; never synthesize missing bytes for identity checks."""
    result = bytearray()
    cursor = address - FLASH_BASE
    end = cursor + size
    for record in sorted(records, key=lambda r: r.address):
        if record.payload is None:
            continue
        start = record.address
        stop = start + len(record.payload)
        if stop <= cursor:
            continue
        if start > cursor:
            break
        take = min(end, stop) - cursor
        if take > 0:
            result.extend(record.payload[cursor-start:cursor-start+take])
            cursor += take
        if cursor == end:
            return bytes(result)
    raise ValueError(f'Uncovered image range at 0x{address:08x}, size {size}')


def bl_target(raw: bytes, site: int) -> int:
    h1, h2 = struct.unpack('<HH', raw)
    if h1 & 0xf800 != 0xf000 or h2 & 0xd000 != 0xd000:
        raise ValueError(f'Not a Thumb BL instruction at 0x{site:08x}')
    sign = (h1 >> 10) & 1
    i1 = 1 ^ (((h2 >> 13) & 1) ^ sign)
    i2 = 1 ^ (((h2 >> 11) & 1) ^ sign)
    displacement = ((sign << 24) | (i1 << 23) | (i2 << 22)
                    | ((h1 & 0x3ff) << 12) | ((h2 & 0x7ff) << 1))
    if sign:
        displacement -= 1 << 25
    return site + 4 + displacement


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, type=Path)
    args = parser.parse_args()
    data = load()
    records = require_stock(args.firmware.read_bytes())
    image = data['image']
    raw = read(records, int(image['base'], 16), image['length'])
    if hashlib.sha256(raw).hexdigest() != image['sha256']:
        raise ValueError('Application segment digest mismatch')
    windows = 0
    for owner in [*data['nodes'], *data['edges']]:
        for evidence in owner.get('evidence', []):
            content = read(records, int(evidence['address'], 16), evidence['length'])
            if hashlib.sha256(content).hexdigest() != evidence['sha256']:
                raise ValueError(f'Identity mismatch at {evidence["address"]}')
            windows += 1
    by_id = {n['id']: n for n in data['nodes']}
    calls = 0
    for edge in data['edges']:
        if edge['kind'] != 'direct_call':
            continue
        site = int(edge['site'], 16)
        target = int(by_id[edge['target']]['address'], 16)
        if bl_target(read(records, site, 4), site) != target:
            raise ValueError(f'BL target mismatch at {edge["site"]}')
        calls += 1
    print(f'Profile, application digest, {windows} identity windows, {calls} direct BL targets: PASS')
    print('Static identity only; no device operation or semantic proof.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as exc:
        raise SystemExit(f'Map check failed: {exc}')
