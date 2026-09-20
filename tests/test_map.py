"""Synthetic and metadata checks; no vendor firmware is required."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT / 'map'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


# check.py is also a directly executable entry point; resolve its sibling import.
sys.path.insert(0, str(MAP))
try:
    renderer = module('research_map_renderer', MAP / 'render.py')
    checker = module('research_map_checker', MAP / 'check.py')
finally:
    sys.path.remove(str(MAP))


class MapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = json.loads((MAP / 'firmware.json').read_text())

    def test_bl_forward_synthetic(self):
        # BL displacement +16, relative to architectural PC = site + 4.
        self.assertEqual(checker.bl_target(bytes.fromhex('00 f0 08 f8'), 0x08001000),
                         0x08001014)

    def test_bl_backward_synthetic(self):
        # BL displacement -8, exercising sign extension and inverted J bits.
        self.assertEqual(checker.bl_target(bytes.fromhex('ff f7 fc ff'), 0x08001000),
                         0x08000ffc)

    def test_non_bl_rejected(self):
        for raw in ('00 bf 00 bf', '00 f0 00 b8', '00 f0 00 e8'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                checker.bl_target(bytes.fromhex(raw), 0x08001000)

    def test_dangling_edge_rejected(self):
        data = copy.deepcopy(self.mapping)
        data['edges'][0]['target'] = 'missing_node'
        with self.assertRaisesRegex(ValueError, 'Dangling'):
            renderer.validate(data)

    def test_thumb_pointer_as_entry_rejected(self):
        data = copy.deepcopy(self.mapping)
        node = next(n for n in data['nodes'] if n['kind'] == 'function')
        node['address'] = f'0x{int(node["address"], 16) | 1:08x}'
        with self.assertRaisesRegex(ValueError, 'even'):
            renderer.validate(data)

    def test_out_of_image_anchor_rejected(self):
        data = copy.deepcopy(self.mapping)
        end = int(data['image']['base'], 16) + data['image']['length']
        data['nodes'][0]['evidence'][0]['address'] = f'0x{end:08x}'
        with self.assertRaisesRegex(ValueError, 'outside'):
            renderer.validate(data)

    def test_generated_outputs_current(self):
        renderer.validate(self.mapping)
        self.assertEqual((MAP / 'index.md').read_text(), renderer.render_index(self.mapping))
        self.assertEqual((MAP / 'calls.mmd').read_text(), renderer.render_graph(self.mapping))


if __name__ == '__main__':
    unittest.main()
