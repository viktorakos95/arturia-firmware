"""Synthetic containers only. Optional local-stock checks never save firmware."""
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ks37.__main__ import main
from ks37.container import MAX_ASCII_BYTES, Record, pack, parse
from ks37.patch import plan_patch, validate_plan, verify_patch
from ks37.profile import (FINAL_EXTRA, STOCK_SHA256, _color_records, application_sum,
                          digest, plan_color, require_stock, validate_layout, verify_color)


def synthetic_records():
    """Manufactured values, never a vendor image or executable firmware."""
    records = []
    for index in range(176):
        payload = None
        if index < 109:
            payload = bytes((index * 13 + i * 17) & 255 for i in range(1024))
        if index == 104:
            payload = payload[:232] + bytes.fromhex("10 10 10") + payload[235:]
        if index == 175:
            payload = b"\xff" * 1022 + bytes(2)
        records.append(Record(0x4000 + index * 1024, payload,
                              FINAL_EXTRA if index == 175 else b""))
    body = b"".join(r.payload if r.payload is not None else b"\xff" * 1024 for r in records)
    records[-1] = replace(records[-1], payload=b"\xff" * 1022 + ((-sum(body[:-2])) & 65535).to_bytes(2, "little"))
    return tuple(records)


def color_plan():
    return {"schema_version": 1, "source_sha256": STOCK_SHA256,
            "edits": [{"flash_address": "0x0801e0e8", "expected": "10", "replacement": "00"}]}


def mutate_block(raw, offset, value, repair=True):
    data = bytearray(bytes.fromhex(raw.decode()))
    data[offset] = value
    if repair:
        size = int.from_bytes(data[:2], "big") + 2
        data[size-2:size] = ((-sum(data[2:size-2])) & 65535).to_bytes(2, "little")
    return data.hex().upper().encode()


class ContainerTests(unittest.TestCase):
    def test_roundtrip_all_shapes(self):
        records = (Record(0x4000, bytes(1024)), Record(0x4400, None),
                   Record(0x4800, bytes([7]) * 1024, bytes(range(12))))
        self.assertEqual(parse(pack(records)), records)

    def test_lowercase_is_readable_and_pack_is_canonical(self):
        raw = pack((Record(0x4000, None),))
        self.assertEqual(pack(parse(raw.lower())), raw)

    def test_ascii_rejections(self):
        for raw in (b"", b"0", b"GG", b"00\n", b"\xff\xff", b"0" * (MAX_ASCII_BYTES + 2)):
            with self.subTest(raw=raw[:12]), self.assertRaises(ValueError):
                parse(raw)

    def test_truncation_and_length(self):
        raw = pack((Record(0x4000, None),))
        for bad in (raw[:-2], raw + b"00", b"0000", b"FFFF" + raw[4:]):
            with self.subTest(raw=bad[:12]), self.assertRaises(ValueError):
                parse(bad)

    def test_corrupt_local_sum(self):
        raw = pack((Record(0x4000, bytes(1024)),))
        with self.assertRaisesRegex(ValueError, "checksum"):
            parse(mutate_block(raw, 18, 1, repair=False))

    def test_corrupt_structural_fields_even_when_checksum_repaired(self):
        raw = pack((Record(0x4000, bytes(1024)),))
        for offset in (2, 6, 10, 12, 1042, 1043, 1046, 1048):
            value = bytes.fromhex(raw.decode())[offset] ^ 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                parse(mutate_block(raw, offset, value))

    def test_pack_rejects_invalid_values(self):
        bad = [Record(-1, None), Record(1 << 24, None), Record(True, None),
               Record(0, b""), Record(0, bytes(1023)), Record(0, bytearray(1024)),
               Record(0, bytes(1024), b"x"), Record(0, None, b"x"),
               Record(0, None, "")]
        for record in bad:
            with self.subTest(record=record.address), self.assertRaises(ValueError):
                pack((record,))
        with self.assertRaises(ValueError):
            pack(())


class ProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = synthetic_records()
        cls.raw = pack(cls.records)

    def test_synthetic_roundtrip_and_application_sum(self):
        self.assertEqual(parse(self.raw), self.records)
        self.assertEqual(application_sum(self.records)["residue"], 0)
        self.assertEqual(len(self.raw), 234280)

    def test_synthetic_is_never_accepted_as_stock(self):
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            require_stock(self.raw)
        with self.assertRaises(ValueError):
            plan_color(self.raw)

    def test_local_checksums_do_not_imply_application_integrity(self):
        records = list(self.records)
        payload = bytearray(records[0].payload)
        payload[20] ^= 1
        records[0] = replace(records[0], payload=bytes(payload))
        parsed = parse(pack(tuple(records)))
        self.assertNotEqual(application_sum(parsed)["residue"], 0)
        with self.assertRaises(ValueError):
            _color_records(parsed)

    def test_layout_rejections(self):
        cases = [self.records[:-1], (replace(self.records[0], address=0),) + self.records[1:],
                 (replace(self.records[0], payload=None),) + self.records[1:],
                 self.records[:-1] + (replace(self.records[-1], extra=bytes(12)),),
                 self.records[:-1] + (replace(self.records[-1], payload=bytes(1024)),)]
        for records in cases:
            with self.subTest(count=len(records)), self.assertRaises(ValueError):
                validate_layout(records)

    def test_color_transform_changes_only_palette_and_tail_payload(self):
        result = parse(pack(_color_records(self.records)))
        self.assertEqual(application_sum(result)["residue"], 0)
        self.assertEqual(result[104].payload[232:235], bytes.fromhex("00 10 10"))
        changed = [(i, j) for i, (a, b) in enumerate(zip(self.records, result))
                   if a.payload is not None for j, (x, y) in enumerate(zip(a.payload, b.payload)) if x != y]
        self.assertIn((104, 232), changed)
        self.assertTrue(all((i, j) == (104, 232) or (i == 175 and j >= 1022) for i, j in changed))


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.records = synthetic_records()
        self.raw = pack(self.records)

    def test_reject_synthetic_source(self):
        with self.assertRaises(ValueError):
            plan_patch(self.raw, color_plan())

    def test_transform_and_separate_verifier_on_synthetic_records(self):
        # Pin admission is separately tested. Only this synthetic test substitutes it.
        with patch("ks37.patch.require_stock", return_value=self.records):
            candidate, report = plan_patch(self.raw, color_plan())
            self.assertEqual(report["application_sum"]["residue"], 0)
            self.assertEqual(verify_patch(self.raw, color_plan(), candidate)["sha256"], digest(candidate))
            self.assertEqual(parse(candidate), _color_records(self.records))

    def test_verifier_rejects_unlisted_edit_even_with_both_sums_repaired(self):
        p = color_plan()
        p["edits"].append({"flash_address": "0x08004000", "expected": "00", "replacement": "01"})
        with patch("ks37.patch.require_stock", return_value=self.records):
            candidate, _ = plan_patch(self.raw, p)
            with self.assertRaisesRegex(ValueError, "differs from plan"):
                verify_patch(self.raw, color_plan(), candidate)

    def test_bad_plans(self):
        malformed = [None, {}, {**color_plan(), "unused": 0}, {**color_plan(), "schema_version": True},
                     {**color_plan(), "source_sha256": "0" * 64}, {**color_plan(), "edits": []}]
        edits = [
            {"flash_address": "0x0801e0e8", "expected": "11", "replacement": "00"},
            {"flash_address": "0x0801e0e8", "expected": "10", "replacement": "10"},
            {"flash_address": "0x0801e0e8", "expected": "10", "replacement": "0000"},
            {"flash_address": "0x0801e0e8", "expected": "1", "replacement": "0"},
            {"flash_address": "0x0801e0e8", "expected": "10 ", "replacement": "00"},
            {"flash_address": "0x0801e0e8", "expected": "10", "replacement": "GG"},
            {"flash_address": "0801e0e8", "expected": "10", "replacement": "00"},
            {"flash_address": "0x0803f800", "expected": "ff", "replacement": "00"},
            {"flash_address": "0x0801f400", "expected": "ff", "replacement": "00"},
            {"flash_address": "0x0802fc00", "expected": "ff", "replacement": "00"},
            {"flash_address": "0x0802fffe", "expected": "ff", "replacement": "00"},
            {"flash_address": "0x0801e0e8", "expected": "10", "replacement": "00", "extra": 1}]
        malformed.extend({**color_plan(), "edits": [e]} for e in edits)
        malformed.append({**color_plan(), "edits": color_plan()["edits"] * 2})
        for p in malformed:
            with self.subTest(plan=p), self.assertRaises(ValueError):
                validate_plan(p, self.records)


class CommandTests(unittest.TestCase):
    def test_inspect_does_not_write(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "synthetic.hex"
            source.write_bytes(pack((Record(0x4000, None),)))
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["inspect", str(source)]), 0)
            self.assertEqual(json.loads(output.getvalue())["local_checksums_valid"], 1)
            self.assertEqual(list(Path(directory).iterdir()), [source])

    def test_output_requires_explicit_option_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "input", Path(directory) / "output"
            source.write_bytes(b"input remains unchanged")
            with patch("ks37.__main__.plan_color", return_value=(b"result", {})), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                main(["patch-color", str(source)])
                self.assertFalse(output.exists())
                main(["patch-color", str(source), "--output", str(output)])
                self.assertEqual(output.read_bytes(), b"result")
                with self.assertRaises(SystemExit):
                    main(["patch-color", str(source), "--output", str(output)])
                with self.assertRaises(SystemExit):
                    main(["patch-color", str(source), "--output", str(source)])
                link = Path(directory) / "link"
                link.symlink_to(source)
                with self.assertRaises(SystemExit):
                    main(["patch-color", str(source), "--output", str(link)])
            self.assertEqual(source.read_bytes(), b"input remains unchanged")


@unittest.skipUnless(os.environ.get("KS37_STOCK_FILE"), "Optional user-supplied official file not set")
class LocalStockTests(unittest.TestCase):
    def test_read_only_real_profile_and_patch(self):
        raw = Path(os.environ["KS37_STOCK_FILE"]).read_bytes()
        require_stock(raw)
        candidate, report = plan_color(raw)
        self.assertEqual(verify_color(raw, candidate)["application_sum"]["residue"], 0)
        generic, generic_report = plan_patch(raw, color_plan())
        self.assertEqual(candidate, generic)
        self.assertEqual(report["sha256"], generic_report["sha256"])
        with self.assertRaises(ValueError):
            verify_color(raw, raw)
        with self.assertRaises(ValueError):
            plan_color(candidate)


if __name__ == "__main__":
    unittest.main()
