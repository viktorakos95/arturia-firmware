"""Offline inspection and one fixed, hash-pinned patch example."""
import argparse
import json
import os
from pathlib import Path
import stat

from .container import MAX_ASCII_BYTES, pack, parse
from .profile import application_sum, digest, plan_color, require_stock, verify_color
from .patch import plan_patch, verify_patch
from .mapcheck import check_map


def read_file(path: Path) -> bytes:
    with path.open("rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError("Input must be a regular file")
        raw = stream.read(MAX_ASCII_BYTES + 1)
    if len(raw) > MAX_ASCII_BYTES:
        raise ValueError("Input exceeds 1 MiB limit")
    return raw


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect = sub.add_parser("inspect", help="Check observed syntax and local sums")
    inspect.add_argument("firmware", type=Path)
    verify = sub.add_parser("verify", help="Verify pinned stock or exact color02 candidate")
    verify.add_argument("firmware", type=Path)
    verify.add_argument("--stock", type=Path, help="Pinned stock for candidate diff verification")
    verify.add_argument("--plan", type=Path, help="Declarative plan for candidate verification")
    patch = sub.add_parser("patch-color", help="Plan the bank 0 color02 patch in memory")
    patch.add_argument("firmware", type=Path)
    patch.add_argument("--output", type=Path, help="Explicit NEW output file; never overwrite")
    generic = sub.add_parser("patch", help="Plan declared payload edits against pinned stock")
    generic.add_argument("firmware", type=Path)
    generic.add_argument("plan", type=Path)
    generic.add_argument("--output", type=Path, help="Explicit NEW output file; never overwrite")
    mapcheck = sub.add_parser("map-check", help="Verify map evidence hashes against local stock")
    mapcheck.add_argument("firmware", type=Path)
    mapcheck.add_argument("--map", type=Path, default=Path("map/firmware.json"))
    args = parser.parse_args(argv)
    try:
        raw = read_file(args.firmware)
        if args.command == "inspect":
            records = parse(raw)
            report = {"sha256": digest(raw), "bytes": len(raw), "records": len(records),
                      "payload_records": sum(r.payload is not None for r in records),
                      "local_checksums_valid": len(records),
                      "canonical_roundtrip_equal": pack(records) == raw,
                      "device_access": False, "profile_matches": False}
            try:
                report["application_sum"] = application_sum(records)
                report["profile_matches"] = True
            except ValueError as exc:
                report["profile_note"] = str(exc)
        elif args.command == "verify":
            if args.plan and not args.stock:
                raise ValueError("Candidate --plan verification also requires --stock")
            if args.stock:
                report = (verify_patch(read_file(args.stock), json.loads(read_file(args.plan)), raw)
                          if args.plan else verify_color(read_file(args.stock), raw))
            else:
                records = require_stock(raw)
                report = {"profile": "official-1.1.6.579", "sha256": digest(raw),
                          "bytes": len(raw), "records": len(records),
                          "local_checksums_valid": len(records),
                          "application_sum": application_sum(records)}
            report["device_access"] = False
        elif args.command == "map-check":
            report = check_map(raw, json.loads(read_file(args.map)))
        else:
            candidate, report = (plan_patch(raw, json.loads(read_file(args.plan)))
                                 if args.command == "patch" else plan_color(raw))
            report["written"] = False
            if args.output:
                if args.output.resolve() == args.firmware.resolve():
                    raise ValueError("Output must differ from the source")
                # Exclusive creation also rejects existing files and symlinks.
                with args.output.open("xb") as stream:
                    stream.write(candidate)
                if read_file(args.output) != candidate:
                    raise ValueError("Saved output differs from the verified plan")
                report["written"] = True
                report["output"] = str(args.output)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"Refused: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
