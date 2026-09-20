#!/usr/bin/env python3
"""Build the added generator mode from source; firmware remains in memory by default."""
from __future__ import annotations

import argparse
from dataclasses import replace
from hashlib import sha256
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from ks37.container import pack, parse
from ks37.profile import STOCK_SHA256, require_stock
from device.link import thumb_branch, verify_link
from device.verify import check_profile, read_elf, verify_bytes

DEVICE = ROOT / "device"
FLAGS = ["--target=arm-none-eabi", "-mcpu=cortex-m3", "-mthumb", "-Oz",
         "-ffreestanding", "-fno-builtin", "-fno-common", "-fstack-usage",
         "-Wall", "-Wextra", "-Werror"]


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def run(arguments: list) -> str:
    process = subprocess.run([str(arg) for arg in arguments], cwd=ROOT,
                             text=True, capture_output=True)
    if process.returncode:
        raise ValueError(f"{Path(str(arguments[0])).name} failed:\n{process.stdout}{process.stderr}")
    return process.stdout


def executable(requested: str | None, default: str) -> str:
    found = shutil.which(requested or default)
    if found is None:
        raise ValueError(f"Cannot find {default}; install a toolchain or supply --{('ld' if default == 'ld.lld' else default)}")
    return found


def load_profile() -> dict:
    profile = json.loads((DEVICE / "profile.json").read_text())
    geometry = {"flash_base": 0x08000000, "application_start": 0x08004000,
                "code_start": 0x0801f400, "code_limit": 0x0802fc00,
                "ram_start": 0x20005eb0, "payload_start": 0x20005ef0,
                "initial_sp": 0x2000c000}
    if profile["schema_version"] != 1 or profile["stock_sha256"] != STOCK_SHA256:
        raise ValueError("Unsupported device profile")
    if any(int(profile[key], 16) != value for key, value in geometry.items()):
        raise ValueError("Device memory geometry requires a reviewed profile")
    if profile["stack_margin"] != 4096:
        raise ValueError("Expected the 4096-byte stack margin policy")
    for key in ("c_modules", "assembly"):
        modules = profile[key]
        if not isinstance(modules, list) or not modules or len(set(modules)) != len(modules):
            raise ValueError("Invalid source module order")
        if any(not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]+", name) for name in modules):
            raise ValueError("Source modules must be local basenames")
    return profile


def create_patches(profile: dict, names: dict) -> list[dict]:
    rows, previous_end = [], 0
    for recipe in profile["patches"]:
        address = int(recipe["address"], 16)
        before = bytes.fromhex(recipe["before"])
        kind, target = recipe["kind"], recipe["target"]
        if address & 1 or address < previous_end:
            raise ValueError("Patch addresses overlap or are not even")
        if not int(profile["application_start"], 16) <= address < int(profile["code_start"], 16):
            raise ValueError("Patch lies outside the original code extent")
        if kind == "DATA-LE32":
            if target != "__boot_ram_end" or len(before) != 4:
                raise ValueError("Unreviewed data patch")
            after = struct.pack("<I", names[target]["value"])
        else:
            if kind not in ("bl", "b.w") or len(before) not in (4, 6, 10):
                raise ValueError("Unreviewed branch kind or overwritten instruction width")
            symbol = names[target]
            if symbol["type"] != 2 or symbol["binding"] != 1 or not symbol["value"] & 1:
                raise ValueError("Hook must name a global Thumb function")
            after = thumb_branch(address, symbol["value"] & ~1, kind)
            after += bytes.fromhex("00 bf") * ((len(before) - 4) // 2)
        previous_end = address + len(before)
        rows.append({"address": address, "before": before, "after": after,
                     "target": target, "kind": kind})
    if len(rows) != 61:
        raise ValueError("This device profile requires 61 reviewed patches")
    return rows


def assemble_container(stock: bytes, code: bytes, changes: list[dict], profile: dict) -> tuple[bytes, dict]:
    original = require_stock(stock)
    inject, limit = int(profile["code_start"], 16), int(profile["code_limit"], 16)
    application = int(profile["application_start"], 16)
    if not code or inject + len(code) > limit or inject % 1024:
        raise ValueError("Compiled code exceeds the permitted expansion extent")
    records, touched, previous_end = list(original), set(), application
    for change in changes:
        address, before, after = change["address"], change["before"], change["after"]
        if address < previous_end or len(before) != len(after) or not before or address + len(before) > inject:
            raise ValueError("Invalid patch width, range or ordering")
        previous_end = address + len(before)
        index, offset = divmod(address - application, 1024)
        payload = records[index].payload
        if payload is None or offset + len(before) > 1024 or payload[offset:offset + len(before)] != before:
            raise ValueError(f"Stock anchor mismatch at {address:#010x}")
        replacement = payload[:offset] + after + payload[offset + len(before):]
        records[index] = replace(records[index], payload=replacement)
        touched.add(index)
    expanded = []
    for offset in range(0, len(code), 1024):
        index = (inject - application + offset) // 1024
        if records[index].payload is not None:
            raise ValueError("Code expansion would overwrite an existing payload")
        records[index] = replace(records[index], payload=code[offset:offset + 1024].ljust(1024, b"\xff"))
        expanded.append(index)
    logical = b"".join(record.payload if record.payload is not None else b"\xff" * 1024 for record in records)
    total = (-sum(logical[:-2])) & 65535
    records[-1] = replace(records[-1], payload=records[-1].payload[:-2] + total.to_bytes(2, "little"))
    candidate = pack(tuple(records))
    if parse(candidate) != tuple(records):
        raise ValueError("Candidate parse-back differs from assembled records")
    changed = [i for i, (a, b) in enumerate(zip(original, records)) if a != b]
    # A patch may reproduce stock bytes or the final sum may stay unchanged.
    if not set(changed) <= touched | set(expanded) | {175}:
        raise ValueError("Candidate changed an unplanned record")
    return candidate, {"expanded_records": expanded, "changed_records": changed,
                       "local_checksums": len(records), "overall_sum_le16": f"0x{total:04x}"}


def compile_sources(profile: dict, build_dir: Path, clang: str, linker: str) -> tuple:
    sources = [DEVICE / "src" / f"{name}.c" for name in profile["c_modules"]]
    sources += [DEVICE / "src" / f"{name}.S" for name in profile["assembly"]]
    tracked = sources + sorted((DEVICE / "src").glob("*.h"))
    tracked += [DEVICE / name for name in ("profile.json", "placement.ld", "build.py", "link.py")]
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in tracked}
    objects = []
    for source in sources:
        obj = build_dir / (source.name + ".o")
        run([clang, *FLAGS, "-c", source, "-o", obj])
        objects.append(obj)
    elf = build_dir / "generator.elf"
    run([linker, "--emit-relocs", "-T", DEVICE / "placement.ld", *objects, "-o", elf])
    raw = elf.read_bytes()
    code, ram, names, relocations = verify_link(raw, profile)
    if hashes != {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in tracked}:
        raise ValueError("Sources changed during compilation")
    return code, ram, names, relocations, hashes, elf, raw


def save_new(path: Path, candidate: bytes, stock_path: Path) -> None:
    if path.resolve() == stock_path.resolve():
        raise ValueError("Output must differ from stock")
    with path.open("xb") as stream:
        stream.write(candidate)
    if path.read_bytes() != candidate:
        raise ValueError("Saved candidate differs from the verified plan")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--stock", type=Path, help="User-supplied pinned official .led")
    mode.add_argument("--link-only", action="store_true", help="Compile and validate owned code without a firmware file")
    parser.add_argument("--output", type=Path, help="Explicit NEW firmware file, requires --stock")
    parser.add_argument("--build-dir", type=Path, default=DEVICE / "build")
    parser.add_argument("--clang", help="Compiler executable; default clang on PATH")
    parser.add_argument("--ld", help="Linker executable; default ld.lld on PATH")
    args = parser.parse_args(argv)
    try:
        if args.output and args.link_only:
            raise ValueError("--output requires --stock")
        if args.output and (args.output.exists() or args.output.is_symlink()):
            raise ValueError("Output exists; refusing to overwrite")
        profile = load_profile()
        check_profile(profile)
        stock = args.stock.read_bytes() if args.stock else None
        if stock is not None:
            require_stock(stock)
        compiler, linker = executable(args.clang, "clang"), executable(args.ld, "ld.lld")
        args.build_dir.mkdir(parents=True, exist_ok=True)
        report_path = args.build_dir / "build-report.json"
        report_path.write_text('{"status":"INCOMPLETE","firmware_file_written":false}\n')
        code, ram, names, rels, sources, elf, elf_raw = compile_sources(profile, args.build_dir, compiler, linker)
        # Also validate the ELF independently when no vendor file is available.
        read_elf(elf_raw)
        patches = create_patches(profile, names)
        source_manifest = json.loads((DEVICE / "source-manifest.json").read_text())
        frozen = source_manifest.get("sources", source_manifest.get("files", {}))
        # The source digest report is informational; edited sources remain buildable.
        baseline_sources = all(digest((DEVICE / "src" / name).read_bytes()) == value
                               for name, value in frozen.items()) if isinstance(frozen, dict) and frozen else None
        report = {"status": "PASS-link-only", "profile": profile["profile"],
                  "firmware_file_written": False, "hardware_tested": False,
                  "code_bytes": code["size"], "code_sha256": digest(code["data"]),
                  "elf_sha256": digest(elf_raw), "runtime_bytes": names["runtime_storage"]["size"],
                  "ram_start": hex(ram["address"]), "ram_end": hex(ram["address"] + ram["size"]),
                  "ram_bytes": ram["size"], "payload_start": hex(names["__boot_payload_start"]["value"]),
                  "stack_margin_policy_bytes": profile["stack_margin"], "stack_bound_proven": False,
                  "baseline_source_hashes_match": baseline_sources,
                  "compiler": run([compiler, "--version"]).splitlines()[0],
                  "linker": run([linker, "--version"]).splitlines()[0],
                  "compile_flags": FLAGS, "sources": sources,
                  "patches": [{**row, "address": hex(row["address"]),
                               "before": row["before"].hex(" "), "after": row["after"].hex(" ")}
                              for row in patches], "retained_relocations": len(rels), "relocations": rels}
        if stock is not None:
            candidate, metrics = assemble_container(stock, code["data"], patches, profile)
            report.update({"status": "PASS-in-memory-candidate", "source_sha256": digest(stock),
                           "candidate_sha256": digest(candidate), "candidate_bytes": len(candidate),
                           "baseline_candidate_matches": digest(candidate) == profile["baseline"]["candidate_sha256"],
                           "container": metrics})
            independent = verify_bytes(stock, candidate, elf_raw, profile, report)
            report["independent_verification"] = independent
            if args.output:
                if args.stock.read_bytes() != stock:
                    raise ValueError("Stock source changed during preparation")
                save_new(args.output, candidate, args.stock)
                report["firmware_file_written"] = True
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        summary_keys = ("status", "code_bytes", "runtime_bytes", "ram_bytes", "ram_end",
                        "candidate_sha256", "candidate_bytes", "baseline_candidate_matches",
                        "firmware_file_written")
        print(json.dumps({key: report[key] for key in summary_keys if key in report}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        parser.exit(1, f"Build refused: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
