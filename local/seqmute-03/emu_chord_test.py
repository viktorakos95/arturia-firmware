#!/usr/bin/env python3
"""Emulator check of the Chord-button release swap (stock vs seqmute-02). Not a device test.
Everything the button router calls outside its own body is stubbed and recorded."""
import sys, struct, itertools
from pathlib import Path
sys.path.insert(0, '.')
from ks37.container import parse
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *
def image(p): return b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in parse(Path(p).read_bytes()))
IMG = {'stock': image('local/keystep37_Firmware_Update_1_1_6_579.led'),
       'cand': image('local/seqmute-03/keystep37_Firmware_Update_1_1_6_579_seqmute03.led')}
RET, U, ROUTER, LO, HI = 0x08000100, 0x20002ddc, 0x08017260, 0x08017260, 0x08017ed0
SHIFT, CAP = 0x200010d2, 0x200010e4
CS = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
NAMES = {0x080168e4: 'set_page', 0x0800d39c: 'led', 0x0801cd5a: 'display', 0x0801d2c0: 'display_reset',
         0x080171f8: 'chord_finalize', 0x0801b246: 'chord_stored', 0x08006ec8: 'tick'}
def run(which, bid, pressed, shift, page, bank, cap, uc, seq=1):
    uc_ = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc_.mem_map(0x08000000, 0x40000); uc_.mem_write(0x08004000, IMG[which]); uc_.mem_map(0x20000000, 0x10000)
    w8 = lambda a, v: uc_.mem_write(a, bytes([v]))
    for p in (0x20001124,): uc_.mem_write(p, struct.pack('<I', U))
    w8(SHIFT, shift); w8(CAP, cap); w8(U + 0x15, page); w8(U + 0x16, bank); w8(U + 0x0c, uc); w8(U + 0x0f, seq)
    w8(U + 0x0d, 1); uc_.mem_write(U + 0x18, struct.pack('<I', 1234))
    calls = []
    def hook(u, addr, size, _):
        if addr == RET: u.emu_stop(); return
        if not (LO <= addr < HI):
            r = [u.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1)]
            if addr == 0x080168e4: w8(r[0] + 0x15, r[1] & 0xff)
            calls.append((NAMES.get(addr, hex(addr)), r[0], r[1] & 0xff))
            u.reg_write(UC_ARM_REG_R0, 0xDEAD0000)          # clobber r0 like a real callee may
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR) | 1)
    uc_.hook_add(UC_HOOK_CODE, hook)
    for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), (U, bid, pressed, 0)): uc_.reg_write(reg, v)
    for reg in CS: uc_.reg_write(reg, 0xA5A50000 + reg)
    uc_.reg_write(UC_ARM_REG_SP, 0x2000f000); uc_.reg_write(UC_ARM_REG_LR, RET | 1)
    uc_.emu_start(ROUTER | 1, RET, count=100000)
    assert uc_.reg_read(UC_ARM_REG_PC) & ~1 == RET and uc_.reg_read(UC_ARM_REG_SP) == 0x2000f000
    assert all(uc_.reg_read(r) == 0xA5A50000 + r for r in CS)
    return calls, bytes(uc_.mem_read(U, 0x20)), uc_.mem_read(CAP, 1)[0]
ok = True
def check(name, cond, detail=''):
    global ok; ok &= bool(cond); print(('PASS ' if cond else 'FAIL ') + name + (' ' + str(detail) if detail else ''))
n = bad = 0
for page, bank, uc in itertools.product(range(4), range(4), (0, 1)):
    n += 1; bad += run('cand', 8, 0, 0, page, bank, 0, uc) != run('stock', 8, 0, 1, page, bank, 0, uc)
check(f'plain Chord release (no capture) == stock Shift+Chord release [CC bank path] ({n} states)', bad == 0, f'mismatches={bad}')
n = bad = 0
for page, bank, uc in itertools.product((0, 1), range(4), (0, 1)):
    n += 1; bad += run('cand', 8, 0, 1, page, bank, 0, uc) != run('stock', 8, 0, 0, page, bank, 0, uc)
check(f'Shift+Chord release on chord/CC page == stock plain Chord release [page toggle] ({n} states)', bad == 0, f'mismatches={bad}')
n = bad = 0
for page, bank, uc, cap in itertools.product((2, 3), range(4), (0, 1, 2), (0, 1)):
    c, ram, _ = run('cand', 8, 0, 1, page, bank, cap, uc); n += 1
    bad += not (c == [] and ram[0x15] == page and ram[0x16] == bank and ram[0x18:0x1c] == bytes(4) and ram[0x0d] == 0)
check(f'Shift+Chord release on page 2: no page/bank change, timer cleared ({n} states)', bad == 0, f'mismatches={bad}')
n = bad = 0
for page, bank, uc in itertools.product(range(4), range(4), (0, 1, 2)):
    n += 1; bad += run('cand', 8, 0, 0, page, bank, 1, uc) != run('stock', 8, 0, 0, page, bank, 1, uc)
check(f'plain Chord release after hold+notes (capture) == stock: finalize, no bank change ({n} states)', bad == 0, f'mismatches={bad}')
c, ram, _ = run('cand', 8, 0, 0, 1, 2, 0, 2)
check('plain release, capture state without notes: finalize runs, then bank path with intact pointer',
      c[0][0] == 'chord_finalize' and ram[0x16] == 3 and ram[0x15] == 1, [x[0] for x in c])
c, ram, _ = run('cand', 8, 0, 0, 1, 3, 0, 0); check('plain release on CC page: bank 4 wraps to bank 1', ram[0x16] == 0 and ram[0x15] == 1)
c, ram, _ = run('cand', 8, 0, 0, 0, 2, 0, 0); check('plain release on chord page: goes to CC page, bank kept', ram[0x16] == 2 and ram[0x15] == 1)
c, ram, _ = run('cand', 8, 0, 1, 1, 2, 0, 0); check('Shift release on CC page: goes to chord page, bank kept', ram[0x16] == 2 and ram[0x15] == 0)
c, ram, _ = run('cand', 8, 0, 1, 0, 2, 0, 0); check('Shift release on chord page: goes to CC page, bank kept', ram[0x16] == 2 and ram[0x15] == 1)
n = bad = 0
for bid, pressed, shift, page, uc, seq in itertools.product(range(10), (0, 1), (0, 1), range(3), (0, 1, 2), (0, 1)):
    if bid == 8 and not pressed: continue
    n += 1
    try: bad += run('cand', bid, pressed, shift, page, 1, 0, uc, seq) != run('stock', bid, pressed, shift, page, 1, 0, uc, seq)
    except Exception as e: bad += 1; print('EXC', bid, pressed, shift, page, uc, seq, e)
check(f'all other button events (ids 0-9, press/release, incl. Chord press) identical to stock ({n} states)', bad == 0, f'mismatches={bad}')
print('RESULT', 'ALL PASS' if ok else 'FAILURES'); sys.exit(0 if ok else 1)
