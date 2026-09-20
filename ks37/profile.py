"""A hash-pinned sample profile, not general device or loader rules."""
from dataclasses import replace
from hashlib import sha256

from .container import Record, pack, parse

STOCK_SHA256 = "464e2ca2fc5318e6026f15cf3c3a61f6417a80171260543a18e87df116319a8a"
COLOR_SHA256 = "b7a1a0c22fe5f631bf320a66ee563ae4a2a83df4dc65949d23c6c517242bcc3b"
FLASH_BASE = 0x08000000
FIRST_ADDRESS = 0x004000
FINAL_ADDRESS = 0x02fffe
FINAL_EXTRA = bytes.fromhex("0d02fc0000000e02fc000000")
EXPECTED_DIFF = ((109866, 0x10, 0x00), (110668, 0x4d, 0x5d),
                 (117114, 0x95, 0xa5), (117138, 0x65, 0x55))


def digest(raw: bytes) -> str:
    return sha256(raw).hexdigest()


def validate_layout(records: tuple[Record, ...]) -> None:
    if len(records) != 176:
        raise ValueError("Profile requires exactly 176 records")
    for index, record in enumerate(records):
        if record.address != FIRST_ADDRESS + index * 1024:
            raise ValueError(f"Profile address order mismatch at record {index}")
        if (record.payload is not None) != (index < 109 or index == 175):
            raise ValueError(f"Profile payload layout mismatch at record {index}")
        if record.extra != (FINAL_EXTRA if index == 175 else b""):
            raise ValueError(f"Profile extra fields mismatch at record {index}")
    if records[-1].payload[:-2] != b"\xff" * 1022:
        raise ValueError("Profile final payload changed outside the sum field")


def application_sum(records: tuple[Record, ...]) -> dict:
    """Use FF for payload-free records; this convention is not flash readback."""
    validate_layout(records)
    body = b"".join(r.payload if r.payload is not None else b"\xff" * 1024
                    for r in records)
    value = int.from_bytes(body[-2:], "little")
    subtotal = sum(body[:-2]) & 0xffff
    return {"address_fields": "[0x004000,0x02fffe)", "gap_fill": "ff",
            "stored_le16": f"0x{value:04x}", "body_sum": f"0x{subtotal:04x}",
            "residue": (subtotal + value) & 0xffff}


def require_stock(raw: bytes) -> tuple[Record, ...]:
    if digest(raw) != STOCK_SHA256:
        raise ValueError("Source SHA-256 is not the pinned official 1.1.6.579")
    records = parse(raw)
    validate_layout(records)
    if pack(records) != raw or application_sum(records)["residue"] != 0:
        raise ValueError("Source failed exact round-trip or application checksum")
    if records[104].payload[232:235] != bytes.fromhex("10 10 10"):
        raise ValueError("CC bank 0 palette anchor mismatch at 0x0801e0e8")
    if records[-1].payload[-2:] != bytes.fromhex("95 c4"):
        raise ValueError("Source application checksum anchor mismatch")
    return records


def _color_records(records: tuple[Record, ...]) -> tuple[Record, ...]:
    """Pure transform; callers must separately enforce the pinned source."""
    validate_layout(records)
    if application_sum(records)["residue"]:
        raise ValueError("Application checksum mismatch before transform")
    items = list(records)
    palette = bytearray(items[104].payload)
    if palette[232:235] != bytes.fromhex("10 10 10"):
        raise ValueError("CC bank 0 palette anchor mismatch")
    palette[232] = 0
    items[104] = replace(items[104], payload=bytes(palette))
    image = b"".join(r.payload if r.payload is not None else b"\xff" * 1024
                     for r in items)
    checksum = ((-sum(image[:-2])) & 0xffff).to_bytes(2, "little")
    items[-1] = replace(items[-1], payload=items[-1].payload[:-2] + checksum)
    return tuple(items)


def decoded_diff(before: bytes, after: bytes) -> tuple[tuple[int, int, int], ...]:
    a, b = bytes.fromhex(before.decode("ascii")), bytes.fromhex(after.decode("ascii"))
    if len(a) != len(b):
        raise ValueError("Decoded lengths differ")
    return tuple((i, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y)


def verify_color(stock: bytes, candidate: bytes) -> dict:
    """Check a fixed diff independently of the color transformation."""
    require_stock(stock)
    records = parse(candidate)
    validate_layout(records)
    metrics = application_sum(records)
    if metrics["residue"]:
        raise ValueError("Candidate application checksum mismatch")
    if decoded_diff(stock, candidate) != EXPECTED_DIFF:
        raise ValueError("Candidate differs from the exact four-byte color change")
    if digest(candidate) != COLOR_SHA256:
        raise ValueError("Candidate SHA-256 differs from the known color probe")
    return {"profile": "1.1.6.579-color02", "sha256": digest(candidate),
            "bytes": len(candidate), "records": len(records),
            "local_checksums_valid": len(records), "application_sum": metrics,
            "decoded_diff": [{"offset": i, "before": f"{a:02x}", "after": f"{b:02x}"}
                             for i, a, b in EXPECTED_DIFF]}


def plan_color(raw: bytes) -> tuple[bytes, dict]:
    candidate = pack(_color_records(require_stock(raw)))
    report = verify_color(raw, candidate)
    report.update({"source_sha256": digest(raw), "device_access": False,
                   "functional_change": {"flash_address": "0x0801e0e8",
                                         "before": "10 10 10", "after": "00 10 10"}})
    return candidate, report
