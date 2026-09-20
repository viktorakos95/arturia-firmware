"""Read-only verification of short hashed evidence windows against local stock."""
from hashlib import sha256

from .profile import FLASH_BASE, require_stock


def check_map(raw: bytes, mapping: dict) -> dict:
    records = require_stock(raw)
    segments = {r.address + FLASH_BASE: r.payload for r in records if r.payload is not None}

    def read(address: int, length: int) -> bytes:
        if length <= 0 or length > 4096:
            raise ValueError("Evidence length must be 1..4096 bytes")
        result = bytearray()
        for position in range(address, address + length):
            base = position & ~1023
            if base not in segments:
                raise ValueError(f"Evidence refers to an absent payload at {position:#010x}")
            result.append(segments[base][position - base])
        return bytes(result)

    segment = b"".join(r.payload for r in records[:109])
    if mapping["image"].get("length", len(segment)) != len(segment):
        raise ValueError("Map contiguous image length mismatch")
    if int(mapping["image"]["base"], 16) != 0x08004000:
        raise ValueError("Map image base differs from the supported profile")
    if sha256(segment).hexdigest() != mapping["image"]["sha256"]:
        raise ValueError("Map contiguous image digest mismatch")
    count = 0
    for node in mapping["nodes"] + mapping.get("edges", []):
        for evidence in node.get("evidence", []):
            address = int(evidence["address"], 16)
            length = evidence["length"]
            if type(length) is not int:
                raise ValueError("Evidence length must be an integer")
            if sha256(read(address, length)).hexdigest() != evidence["sha256"]:
                label = node.get("id", node.get("source", "edge"))
                raise ValueError(f"Map evidence mismatch: {label} at {address:#010x}")
            count += 1
    return {"profile": "official-1.1.6.579", "evidence_windows_verified": count,
            "image_sha256": mapping["image"]["sha256"], "device_access": False}
