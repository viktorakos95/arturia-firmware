"""Declarative payload edits against one pinned source, with both sums rebuilt."""
from dataclasses import replace
import re

from .container import pack, parse
from .profile import (FINAL_ADDRESS, FIRST_ADDRESS, FLASH_BASE, STOCK_SHA256,
                      application_sum, decoded_diff, digest, require_stock,
                      validate_layout)


def validate_plan(plan: dict, records: tuple) -> dict[int, int]:
    if not isinstance(plan, dict) or set(plan) != {"schema_version", "source_sha256", "edits"}:
        raise ValueError("Plan requires only schema_version, source_sha256 and edits")
    if type(plan["schema_version"]) is not int or plan["schema_version"] != 1:
        raise ValueError("Unsupported plan schema_version")
    if plan["source_sha256"] != STOCK_SHA256:
        raise ValueError("Plan source_sha256 must name the pinned stock")
    if not isinstance(plan["edits"], list) or not 1 <= len(plan["edits"]) <= 1024:
        raise ValueError("Plan requires 1..1024 edits")
    replacements = {}
    for edit in plan["edits"]:
        if not isinstance(edit, dict) or set(edit) != {"flash_address", "expected", "replacement"}:
            raise ValueError("Each edit requires only flash_address, expected and replacement")
        text = edit["flash_address"]
        if not isinstance(text, str) or re.fullmatch(r"0x[0-9A-Fa-f]{8}", text) is None:
            raise ValueError("flash_address must be an eight-digit 0x address")
        address = int(text, 16) - FLASH_BASE
        values = []
        for key in ("expected", "replacement"):
            value = edit[key]
            if not isinstance(value, str) or re.fullmatch(r"[0-9A-Fa-f]+", value) is None or len(value) % 2:
                raise ValueError(f"{key} must be contiguous even-length hex")
            values.append(bytes.fromhex(value))
        expected, replacement = values
        if len(expected) != len(replacement) or not 1 <= len(expected) <= 4096:
            raise ValueError("Edit lengths must match and contain 1..4096 bytes")
        if expected == replacement:
            raise ValueError("No-op edits are not accepted")
        for offset, (before, after) in enumerate(zip(expected, replacement)):
            position = address + offset
            if not FIRST_ADDRESS <= position < FINAL_ADDRESS:
                raise ValueError("Edit outside the application or touching the final sum")
            if position in replacements:
                raise ValueError("Overlapping edits")
            index, within = divmod(position - FIRST_ADDRESS, 1024)
            payload = records[index].payload
            if payload is None:
                raise ValueError("Edits cannot populate payload-free records")
            if index == 175:
                raise ValueError("The final record is reserved for application integrity")
            if payload[within] != before:
                raise ValueError(f"Expected-byte mismatch at {position + FLASH_BASE:#010x}")
            replacements[position] = after
    return replacements


def verify_patch(stock: bytes, plan: dict, candidate: bytes) -> dict:
    """Check allowed payload differences without invoking the patch transform."""
    original = require_stock(stock)
    replacements = validate_plan(plan, original)
    updated = parse(candidate)
    validate_layout(updated)
    metrics = application_sum(updated)
    if metrics["residue"]:
        raise ValueError("Candidate application checksum mismatch")
    if pack(updated) != candidate:
        raise ValueError("Candidate must use canonical uppercase ASCII")
    for index, (before, after) in enumerate(zip(original, updated)):
        if before.payload is None:
            continue
        for offset, (old, new) in enumerate(zip(before.payload, after.payload)):
            position = before.address + offset
            if position >= FINAL_ADDRESS:
                continue
            if new != replacements.get(position, old):
                raise ValueError(f"Candidate differs from plan at {position + FLASH_BASE:#010x}")
    return {"profile": "1.1.6.579-declarative-patch", "source_sha256": digest(stock),
            "sha256": digest(candidate), "bytes": len(candidate),
            "records": len(updated), "local_checksums_valid": len(updated),
            "application_sum": metrics, "plan_edits": len(plan["edits"]),
            "decoded_diff": [{"offset": i, "before": f"{a:02x}", "after": f"{b:02x}"}
                             for i, a, b in decoded_diff(stock, candidate)],
            "device_access": False}


def plan_patch(stock: bytes, plan: dict) -> tuple[bytes, dict]:
    records = require_stock(stock)
    replacements = validate_plan(plan, records)
    items = list(records)
    for index, record in enumerate(items):
        if record.payload is None:
            continue
        payload = bytearray(record.payload)
        for offset in range(len(payload)):
            payload[offset] = replacements.get(record.address + offset, payload[offset])
        items[index] = replace(record, payload=bytes(payload))
    image = b"".join(r.payload if r.payload is not None else b"\xff" * 1024 for r in items)
    tail = ((-sum(image[:-2])) & 0xffff).to_bytes(2, "little")
    items[-1] = replace(items[-1], payload=items[-1].payload[:-2] + tail)
    candidate = pack(tuple(items))
    return candidate, verify_patch(stock, plan, candidate)
