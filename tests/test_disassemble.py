import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

SPEC = importlib.util.spec_from_file_location(
    "local_disassemble", Path(__file__).resolve().parents[1] / "tools" / "disassemble.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WindowTests(unittest.TestCase):
    def test_cross_record_window(self):
        records = (SimpleNamespace(address=0x4000, payload=b"A" * 1024),
                   SimpleNamespace(address=0x4400, payload=b"B" * 1024))
        self.assertEqual(MODULE.read_window(records, 0x080043fe, 4), b"AABB")

    def test_absent_payload_is_not_synthesized(self):
        records = (SimpleNamespace(address=0x4000, payload=None),)
        with self.assertRaisesRegex(ValueError, "no explicit payload"):
            MODULE.read_window(records, 0x08004000, 2)

    def test_window_bounds(self):
        for start, size in ((0x08004001, 2), (0x08004000, 0),
                            (0x08004000, 3), (0x08004000, 4098)):
            with self.subTest(start=start, size=size), self.assertRaises(ValueError):
                MODULE.read_window((), start, size)


if __name__ == "__main__":
    unittest.main()
