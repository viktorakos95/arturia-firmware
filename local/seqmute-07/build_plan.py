#!/usr/bin/env python3
"""Turn rr.bin (offset 0 = flash 0x08004000) into plan.json: stock bytes as `expected`, built bytes as `replacement`."""
import sys, json, subprocess, hashlib
from pathlib import Path
sys.path.insert(0, '.')
from ks37.container import parse
D = Path('local/seqmute-07'); BASE = 0x08004000
stock_led = Path('local/keystep37_Firmware_Update_1_1_6_579.led')
stock = b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in parse(stock_led.read_bytes()))
# flat image offset 0 = ? find by locating the same base used by the emulator: image mapped at 0x08004000
new = (D/'rr.bin').read_bytes()
sym = {}
for l in subprocess.check_output(['llvm-nm', '/tmp/s07.elf'], text=True).split('\n'):
    p = l.split()
    if len(p) == 3: sym[p[2]] = int(p[0], 16) & ~1
sites = [('site_knob_table', 8), ('rr_a_start', sym['rr_a_end'] - sym['rr_a_start']), ('site_pedal_off', 4), ('site_pedal_release', 4), ('site_button_off', 4),
         ('site_button_release', 4), ('site_chord_press', 2), ('site_shift_hold', 2), ('site_note_entry', 4),
         ('site_note_off', 4), ('hook_note_off', sym['added_end'] - sym['hook_note_off'])]
edits = []
for name, n in sites:
    a = sym[name]; o = a - BASE
    exp, rep = stock[o:o+n], new[o:o+n]
    edits.append({'flash_address': '0x%08x' % a, 'expected': exp.hex(), 'replacement': rep.hex(), '_name': name})
    print(name, hex(a), n, exp.hex() if n <= 8 else '(pad ff: %s)' % (set(exp) == {0xff}), '->', rep.hex() if n <= 8 else '...')
# keep seqmute-01/02 edits (those not re-created by this build) from the seqmute-03 plan
mine = {e['flash_address'] for e in edits}
base = [e for e in json.loads(Path('local/seqmute-03/plan.json').read_text())['edits'] if e['flash_address'] not in mine]
print('inherited edits:', len(base), [e['flash_address'] for e in base])
out = {'schema_version': 1, 'source_sha256': hashlib.sha256(stock_led.read_bytes()).hexdigest(),
       'edits': base + [{k: v for k, v in e.items() if k != '_name'} for e in edits]}
(D/'plan.json').write_text(json.dumps(out, indent=1))
