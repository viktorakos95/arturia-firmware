"""Observed .led syntax. This is not a bootloader specification."""
from dataclasses import dataclass
import re

MAGIC = bytes.fromhex("6874da51")
MAX_ASCII_BYTES = 1_048_576
PAYLOAD_BYTES = 1024


@dataclass(frozen=True)
class Record:
    address: int
    payload: bytes | None
    extra: bytes = b""


def parse(raw: bytes) -> tuple[Record, ...]:
    """Check framing and every local checksum; do not infer record effects."""
    if not isinstance(raw, bytes) or not 0 < len(raw) <= MAX_ASCII_BYTES:
        raise ValueError("Expected a nonempty container of at most 1 MiB")
    if len(raw) % 2 or re.fullmatch(rb"[0-9A-Fa-f]+", raw) is None:
        raise ValueError("Expected even-length ASCII hex, without whitespace")
    decoded = bytes.fromhex(raw.decode("ascii"))
    result = []
    offset = 0
    while offset < len(decoded):
        if offset + 2 > len(decoded):
            raise ValueError(f"Truncated length at decoded offset {offset:#x}")
        size = int.from_bytes(decoded[offset:offset + 2], "big") + 2
        if size not in (18, 1054, 1066) or offset + size > len(decoded):
            raise ValueError(f"Unsupported or truncated record at {offset:#x}")
        block = decoded[offset:offset + size]
        if block[2:6] != MAGIC or block[6] != 9 or block[10:12] != bytes(2):
            raise ValueError(f"Unsupported record header at {offset:#x}")
        if block[-6:-2] != bytes(4):
            raise ValueError(f"Unsupported trailing fields at {offset:#x}")
        if (sum(block[2:-2]) + int.from_bytes(block[-2:], "little")) & 0xffff:
            raise ValueError(f"Local checksum mismatch at {offset:#x}")
        address_bytes = block[7:10]
        payload, extra = None, b""
        if size > 18:
            if block[12:18] != bytes.fromhex("0a0000000400"):
                raise ValueError(f"Unsupported payload prefix at {offset:#x}")
            if block[1042:1048] != b"\x0b" + address_bytes + b"\x04\x00":
                raise ValueError(f"Payload suffix mismatch at {offset:#x}")
            payload, extra = block[18:1042], block[1048:-6]
        result.append(Record(int.from_bytes(address_bytes, "big"), payload, extra))
        offset += size
    return tuple(result)


def pack(records: tuple[Record, ...]) -> bytes:
    """Serialize the observed syntax and recalculate local sums in memory."""
    if not records:
        raise ValueError("At least one record is required")
    encoded = bytearray()
    for record in records:
        if type(record.address) is not int or not 0 <= record.address <= 0xffffff:
            raise ValueError("Address must be an unsigned 24-bit integer")
        if not isinstance(record.extra, bytes):
            raise ValueError("Extra fields must be bytes")
        address = record.address.to_bytes(3, "big")
        body = MAGIC + b"\x09" + address + bytes(2)
        if record.payload is None:
            if record.extra:
                raise ValueError("Payload-free records cannot have extra fields")
        else:
            if not isinstance(record.payload, bytes) or len(record.payload) != PAYLOAD_BYTES:
                raise ValueError("Payload must contain exactly 1024 bytes")
            if len(record.extra) not in (0, 12):
                raise ValueError("Extra fields must contain zero or twelve bytes")
            body += bytes.fromhex("0a0000000400") + record.payload
            body += b"\x0b" + address + b"\x04\x00" + record.extra
        body += bytes(4)
        encoded += (len(body) + 2).to_bytes(2, "big") + body
        encoded += ((-sum(body)) & 0xffff).to_bytes(2, "little")
        if len(encoded) * 2 > MAX_ASCII_BYTES:
            raise ValueError("Container exceeds 1 MiB limit")
    return encoded.hex().upper().encode("ascii")
