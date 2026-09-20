"""Builder tests use manufactured records and instruction operands only."""
from contextlib import redirect_stderr
from copy import deepcopy
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from device.build import assemble_container, create_patches, load_profile, main, save_new
from device.link import decode_thumb_branch, elf32, thumb_branch
from ks37.container import pack, parse
from test_tools import synthetic_records


def symbols(profile):
    result = {}
    for index, recipe in enumerate(profile["patches"]):
        result[recipe["target"]] = {"value": 0x0801f401 + index * 16, "type": 2, "binding": 1}
    result["__boot_ram_end"] = {"value": 0x20008708, "type": 0, "binding": 1}
    return result


class ThumbTests(unittest.TestCase):
    def test_branch_boundaries_and_directions(self):
        source = 0x08018000
        for delta in (-0x1000000, -0xfffffe, -0x400002, -4, -2, 0, 2, 0x400000, 0xfffffe):
            target = source + 4 + delta
            for kind in ("bl", "b.w"):
                with self.subTest(delta=delta, kind=kind):
                    self.assertEqual(decode_thumb_branch(source, thumb_branch(source, target, kind)), (kind, target))

    def test_invalid_branch_inputs(self):
        cases = [(0x1001, 0x2000, "bl"), (0x1000, 0x2001, "bl"),
                 (0, 0, "bx"), (-2, 0, "bl"), (0xfffffffc, 0, "bl"),
                 (0x1000, 0x1001004, "bl"), (0x2000000, 0xfffffe, "b.w")]
        for args in cases:
            with self.subTest(args=args), self.assertRaises(ValueError):
                thumb_branch(*args)
        for raw in (b"", b"\0" * 2, b"\0" * 4, b"\xff" * 4):
            with self.assertRaises(ValueError):
                decode_thumb_branch(0x1000, raw)


class ElfParserTests(unittest.TestCase):
    def test_short_and_wrong_headers_refused(self):
        for raw in (b"", b"\x7fELF", bytes(100), b"\x7fELF\x02\x01\x01" + bytes(100)):
            with self.assertRaises(ValueError):
                elf32(raw)

    def test_invalid_section_table_refused(self):
        raw = bytearray(100)
        raw[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHI", raw, 16, 2, 40, 1)
        struct.pack_into("<H", raw, 40, 52)
        struct.pack_into("<I", raw, 32, 90)
        struct.pack_into("<HHH", raw, 46, 40, 3, 1)
        with self.assertRaisesRegex(ValueError, "section table"):
            elf32(bytes(raw))


class RecipeTests(unittest.TestCase):
    def setUp(self):
        self.profile = load_profile()
        self.names = symbols(self.profile)

    def test_every_hook_targets_its_actual_symbol_and_data_tracks_ram(self):
        rows = create_patches(self.profile, self.names)
        self.assertEqual(len(rows), 61)
        for row in rows:
            if row["kind"] == "DATA-LE32":
                self.assertEqual(int.from_bytes(row["after"], "little"), self.names["__boot_ram_end"]["value"])
            else:
                self.assertEqual(decode_thumb_branch(row["address"], row["after"][:4]),
                                 (row["kind"], self.names[row["target"]]["value"] & ~1))
                self.assertEqual(row["after"][4:], bytes.fromhex("00bf") * ((len(row["before"]) - 4) // 2))

    def test_recipe_refuses_overlap_wrong_width_and_symbol_kind(self):
        for change in ("overlap", "width", "odd", "missing", "symbol", "data"):
            profile, names = deepcopy(self.profile), deepcopy(self.names)
            if change == "overlap": profile["patches"][1]["address"] = profile["patches"][0]["address"]
            elif change == "width": profile["patches"][0]["before"] = "00 00"
            elif change == "odd": profile["patches"][0]["address"] = "0x08004931"
            elif change == "missing": profile["patches"].pop()
            elif change == "symbol": names[profile["patches"][0]["target"]]["type"] = 1
            else:
                next(p for p in profile["patches"] if p["kind"] == "DATA-LE32")["target"] = "bad"
            with self.subTest(change=change), self.assertRaises(ValueError):
                create_patches(profile, names)


class ContainerBuildTests(unittest.TestCase):
    def setUp(self):
        self.profile, self.records = load_profile(), synthetic_records()
        self.raw = pack(self.records)
        before = self.records[0].payload[:4]
        self.changes = [{"address": 0x08004000, "before": before, "after": bytes(x ^ 1 for x in before)}]

    def test_source_pin_not_bypassed(self):
        with self.assertRaises(ValueError):
            assemble_container(self.raw, b"source-only synthetic test", self.changes, self.profile)

    def test_expansion_padding_both_sums_and_allowed_records(self):
        code = b"\x17" * 1025
        with patch("device.build.require_stock", return_value=self.records):
            raw, report = assemble_container(self.raw, code, self.changes, self.profile)
        records = parse(raw)
        self.assertEqual(report["expanded_records"], [109, 110])
        self.assertEqual(records[109].payload, code[:1024])
        self.assertEqual(records[110].payload, code[1024:] + b"\xff" * 1023)
        self.assertIsNone(records[111].payload)
        logical = b"".join(r.payload if r.payload is not None else b"\xff" * 1024 for r in records)
        self.assertEqual((sum(logical[:-2]) + int.from_bytes(logical[-2:], "little")) & 65535, 0)
        self.assertTrue(set(report["changed_records"]) <= {0, 109, 110, 175})

    def test_range_anchor_order_fail_closed(self):
        cases = [(b"", self.changes), (b"x" * (0x10800 + 1), self.changes),
                 (b"x", self.changes * 2),
                 (b"x", [{**self.changes[0], "before": b"\xff" * 4}]),
                 (b"x", [{**self.changes[0], "address": 0x08003fff}]),
                 (b"x", [{**self.changes[0], "after": b"short"}])]
        with patch("device.build.require_stock", return_value=self.records):
            for code, changes in cases:
                with self.subTest(size=len(code)), self.assertRaises(ValueError):
                    assemble_container(self.raw, code, changes, self.profile)

    def test_explicit_output_never_replaces_source_or_existing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            stock, target = Path(directory) / "stock", Path(directory) / "candidate"
            stock.write_bytes(b"original")
            save_new(target, b"new bytes", stock)
            self.assertEqual(target.read_bytes(), b"new bytes")
            with self.assertRaises(FileExistsError): save_new(target, b"replacement", stock)
            with self.assertRaises(ValueError): save_new(stock, b"replacement", stock)
            self.assertEqual(stock.read_bytes(), b"original")

    def test_link_only_cannot_write_firmware(self):
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            main(["--link-only", "--output", "unused.led"])


if __name__ == "__main__":
    unittest.main()
