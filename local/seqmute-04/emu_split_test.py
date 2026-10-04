#!/usr/bin/env python3
"""Emulator checks for the keyboard split (seqmute-03 vs seqmute-04). Not a device test."""
import sys, struct, itertools
from pathlib import Path
sys.path.insert(0, '.')
from ks37.container import parse
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *
def image(p): return b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in parse(Path(p).read_bytes()))
IMG = {'stock': image('local/keystep37_Firmware_Update_1_1_6_579.led'),
       's02': image('local/seqmute-02/keystep37_Firmware_Update_1_1_6_579_seqmute02.led'),
       's03': image('local/seqmute-03/keystep37_Firmware_Update_1_1_6_579_seqmute03.led'),
       's04': image('local/seqmute-04/keystep37_Firmware_Update_1_1_6_579_seqmute04.led')}
RET, U, H, BLD, NOTE, OUT0, HO, SET = 0x08000100, 0x20002ddc, 0x200051cc, 0x2000063c, 0x20001eb8, 0x20006000, 0x20006400, 0x20006500
CS = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
KSTUBS = {0x0801b384: ('out', None), 0x0801b460: ('chord_out', None), 0x080116e0: ('arp_builder', None),
          0x080160a8: ('alt_channel', 9), 0x08014b7c: ('midi', None), 0x08014258: ('cleanup', None), 0x0801348e: ('rec', None),
          0x0801d178: ('disp', None), 0x0801b246: ('chord_active', 0),
          0x08013164: ('rec_a', 0), 0x08013944: ('rec_b', None), 0x0801316a: ('rec_c', None), 0x08011e88: ('rec_d', 0),
          0x08013b84: ('rec_e', None), 0x0801301a: ('rec_f', 1), 0x080132c2: ('rec_g', 1), 0x08013110: ('seq_len', 16)}
ARITY = {0x0801b384: 4, 0x0801b460: 3, 0x080116e0: 2, 0x080160a8: 2, 0x08014258: 1, 0x0801348e: 3, 0x0801d178: 2,
         0x0801b246: 1, 0x080170e0: 1, 0x0800cc28: 1, 0x0801d76c: 3, 0x0801d790: 4, 0x0801d7ac: 3, 0x0801d7b6: 2,
         0x0801d862: 1, 0x08010b30: 1, 0x08013b84: 3, 0x08013944: 2, 0x08011e88: 1, 0x08013110: 1, 0x080132c2: 1,
         0x0801301a: 1, 0x08013164: 1, 0x0801316a: 2}
class M:
    def __init__(s, which, native=None):
        s.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        s.uc.mem_map(0x08000000, 0x40000); s.uc.mem_write(0x08004000, IMG[which]); s.uc.mem_map(0x20000000, 0x10000)
        s.calls = []; s.stubs = dict(KSTUBS); s.native = native
        s.uc.hook_add(UC_HOOK_CODE, s.hook)
        for ptr, val in ((0x20001124, U), (0x20001094, BLD), (0x2000115c, HO), (0x200010a4, 0x20004ed4)): s.w32(ptr, val)
        s.w32(NOTE, OUT0); s.w8(0x200010d4, 3); s.w8(H + 0x4f, 60); s.w32(0x200000c8, 60); s.w32(HO, SET)
    def hook(s, uc, addr, size, _):
        if addr == RET: uc.emu_stop(); return
        st = s.stubs.get(addr)
        if st is None and s.native and not any(a <= addr < b for a, b in s.native): st = (hex(addr), None)
        if st:
            r = [uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
            n = ARITY.get(addr, 2)                       # record only real arguments, not dead scratch registers
            s.calls.append((st[0], r[0], r[1] if n > 1 else 0, (r[2] & 0xff) if n > 2 else 0, (r[3] & 0xff) if n > 3 else 0))
            if st[1] is not None: uc.reg_write(UC_ARM_REG_R0, st[1])
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)
    def w8(s, a, v): s.uc.mem_write(a, bytes([v & 0xff]))
    def w32(s, a, v): s.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def r8(s, a): return s.uc.mem_read(a, 1)[0]
    def flag(s, p): return s.r8(NOTE + 0x496 + p)
    def call(s, fn, *args, stack=()):
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args): s.uc.reg_write(reg, v & 0xffffffff)
        for reg in CS: s.uc.reg_write(reg, 0xA5A50000 + reg)
        s.uc.reg_write(UC_ARM_REG_SP, 0x2000f000); s.uc.reg_write(UC_ARM_REG_LR, RET | 1)
        for i, v in enumerate(stack): s.w32(0x2000f000 + 4 * i, v)
        s.calls = []
        s.uc.emu_start(fn | 1, RET, count=400000)
        assert s.uc.reg_read(UC_ARM_REG_PC) & ~1 == RET and s.uc.reg_read(UC_ARM_REG_SP) == 0x2000f000
        assert all(s.uc.reg_read(r) == 0xA5A50000 + r for r in CS), 'callee-saved register clobbered'
        return list(s.calls)
    def key(s, on, pitch, src=0, ch=3):
        msg = ((0x90 if on else 0x80) | ch) | (pitch << 8) | ((100 if on else 0x40) << 16)
        return [(c[0], c[2] & 0xffffff, c[3], c[4]) for c in s.call(0x0801b750, NOTE, msg, src, 0) if c[0] != 'chord_active']
def kb(which, seq=1, transport=0, kbdplay=0, hold=0):
    m = M(which); m.call(0x0801b572, NOTE, OUT0)
    m.w8(U + 0x0f, seq); m.w8(U + 0x10, transport); m.w8(U + 0x11, kbdplay); m.w8(HO + 5, hold)
    return m
midi = lambda ev: [c for c in ev if c[0] == 'out' and c[3] == 0 or c[0] == 'out' and c[3] == 2]
ok = True
def check(name, cond, detail=''):
    global ok; ok &= bool(cond); print(('PASS ' if cond else 'FAIL ') + name + (' ' + str(detail) if detail else ''))

ARITY.update({0x0801cd5a: 4, 0x0801d580: 3, 0x0801d660: 2})
CHORD = 0x20001e04
ON = lambda p, ch=3: ('out', (0x90 | ch) | p << 8 | 100 << 16, 1, 0)
OFF = lambda p, ch=3: ('out', (0x80 | ch) | p << 8 | 0x40 << 16, 1, 0)
CC = lambda n, v, ch=3: ('midi', 0xb0 | ch | n << 8 | v << 16, 0, 0)
def sp(which='s04', page=0, split=0, mode=0, seq=1, tr=0, hold=0):
    m = kb(which, seq, tr, hold=hold); m.w8(U + 0x15, page); m.w8(CHORD + 0x48, split); m.w8(CHORD + 9, mode); return m
def ev(m, on, p, src=0):
    return [(c[0], c[1] if c[0] == 'midi' else c[1], c[2], c[3]) for c in m.key(on, p, src)]
def ev2(m, on, p, src=0):   # normalise: ('midi', msg) / ('out', msg,...)
    r = []
    for c in m.call(0x0801b750, NOTE, ((0x90 if on else 0x80) | 3) | (p << 8) | ((100 if on else 0x40) << 16), src, 0):
        if c[0] == 'out' and c[4] != 1: r.append(('out', c[2] & 0xffffff, c[3], c[4]))   # route 1 is the DIN copy
        elif c[0] == 'midi': r.append(('midi', c[2] & 0xffffff, 0, 0))
    return r

# 1. split not active: identical to seqmute-03 (page 1 / not armed / MIDI in / page 0 default), RAM included
n = bad = 0
for page, mode, src in ((1, 3, 0), (1, 1, 0), (0, 0, 0), (0, 3, 1), (2, 2, 0), (1, 2, 1)):
    for seq, tr, hold in itertools.product((0, 1), (0, 1, 2), (0, 1)):
        res = []
        for w in ('s03', 's04'):
            m = sp(w, page, 50, mode, seq, tr, hold)
            res.append(([ev2(m, 1, 60, src), ev2(m, 1, 40, src), ev2(m, 0, 60, src), ev2(m, 1, 60, src), ev2(m, 0, 40, src), ev2(m, 0, 60, src)],
                        bytes(m.uc.mem_read(NOTE, 0x520))))
        n += 1; bad += res[0] != res[1]
check(f'split inactive (CC page, not armed, MIDI in, other page): same as seqmute-03 ({n} states x 6 events)', bad == 0, f'mismatches={bad}')

# 2. mode "1" (stored 1): keys >= split send CC17 only, no note, release swallowed
m = sp(page=0, split=50, mode=1)
a, b, c, d = ev2(m, 1, 40), ev2(m, 1, 50), ev2(m, 1, 72), ev2(m, 0, 72)
check('mode 1: below split plays normally', a == [ON(40)] and m.flag(40) == 2, a)
check('mode 1: at/above split sends CC17=note, no note, flags untouched', b == [CC(17, 50)] and c == [CC(17, 72)] and m.flag(50) == 0 and m.flag(72) == 0, (b, c))
check('mode 1: release of an upper key sends nothing', d == [], d)
e = ev2(m, 0, 40); check('mode 1: lower key release is a normal Note Off', e == [OFF(40)] and m.flag(40) == 0, e)
# 3. mode "2": lower keys send CC16, upper play
m = sp(page=0, split=50, mode=2)
a, b, c, d = ev2(m, 1, 40), ev2(m, 1, 60), ev2(m, 0, 40), ev2(m, 0, 60)
check('mode 2: below split sends CC16=note only; above plays normally', a == [CC(16, 40)] and b == [ON(60)] and c == [] and d == [OFF(60)], (a, b, c, d))
# 4. mode ALL
m = sp(page=0, split=50, mode=3)
a = ev2(m, 1, 60); b = ev2(m, 0, 60)
check('ALL, nothing held below: upper key sends CC17 and also plays', a == [CC(17, 60), ON(60)] and b == [OFF(60)], (a, b))
m = sp(page=0, split=50, mode=3)
x = ev2(m, 1, 40); a = ev2(m, 1, 60); b = ev2(m, 0, 60); y = ev2(m, 0, 40)
check('ALL, lower key held: upper key sends CC17 only (no second note)', x == [ON(40)] and a == [CC(17, 60)] and b == [] and y == [OFF(40)], (x, a, b, y))
# 5. default split point 60 when never set; armed by mode alone
m = sp(page=0, split=0, mode=1); a, b = ev2(m, 1, 59), ev2(m, 1, 60)
check('split byte 0 means 60', a == [ON(59)] and b == [CC(17, 60)], (a, b))
# 6. MIDI-in notes never split
m = sp(page=0, split=50, mode=1); a = ev2(m, 1, 72, src=1)
check('MIDI-in note above the split is untouched', a == [('out', (0x90 | 3) | 72 << 8 | 100 << 16, 2, 0)], a)
# 7. Hold still works for the lower side; upper-side keys are never sustained
m = sp(page=0, split=50, mode=1, hold=1)
a, b, c, d = ev2(m, 1, 40), ev2(m, 0, 40), ev2(m, 1, 72), ev2(m, 0, 72)
check('Hold on: lower note sustained as before, upper key still just a CC', a == [ON(40)] and b == [] and m.flag(40) == 6 and c == [CC(17, 72)] and d == [] and m.flag(72) == 0, (a, b, c, d))
# 8. chord-page knobs 1 and 2 (code run inside the stock knob function; stock epilogue pops a prepared frame)
KN = 0x20004000
def knob(which, idx, raw, split=0, mode=0, setup=None):
    m = M(which); m.w8(CHORD + 0x48, split); m.w8(CHORD + 9, mode)
    m.w8(KN + 0x58, idx); m.w8(KN + 0x59, raw)
    sp0 = 0x2000e000
    for i in range(9): m.w32(sp0 + 0x5c + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x5c + 32, RET | 1)
    m.stubs[0x0801cd5a] = ('show', None)
    m.calls = []
    for reg, v in ((UC_ARM_REG_R4, KN), (UC_ARM_REG_SP, sp0), (UC_ARM_REG_R0, 0), (UC_ARM_REG_R3, 0)): m.uc.reg_write(reg, v)
    # jump through the real tbh table: r3 = idx-1, as stock does at 0x08004968
    tbl = 0x0800496c; ent = struct.unpack('<H', bytes(m.uc.mem_read(tbl + 2 * (idx - 1), 2)))[0]
    tgt = (tbl + 2 * ent) | 1
    m.uc.emu_start(tgt, RET, count=100000)
    return m
for raw, want in ((0, 24), (3, 24), (4, 25), (128, 24 + 42), (255, 108)):
    m = knob('s04', 1, raw)
    show = [c for c in m.calls if c[0] == 'show']
    check(f'knob 1 raw {raw} -> split {want}, display called once with formatter+value, armed as mode "1"',
          m.r8(CHORD + 0x48) == want and len(show) == 1 and show[0][2] & 1 == 1 and show[0][4] == want and m.r8(CHORD + 9) == 1, (m.r8(CHORD+0x48), show))
for raw, want in ((0, 0), (85, 0), (86, 1), (170, 1), (171, 2), (255, 2)):
    m = knob('s04', 2, raw)
    show = [c for c in m.calls if c[0] == 'show']
    check(f'knob 2 raw {raw} -> mode {want} (stored {want + 1}), displayed value {want}', m.r8(CHORD + 9) == want + 1 and show and show[0][4] == want, show)
m = knob('s04', 1, 100, mode=3); check('knob 1 keeps an existing mode', m.r8(CHORD + 9) == 3)
for idx in (3, 4):
    m = knob('s04', idx, 77, split=55, mode=2)
    check(f'knob {idx} on the chord page does nothing', m.r8(CHORD + 0x48) == 55 and m.r8(CHORD + 9) == 2 and not [c for c in m.calls if c[0] == 'show'])
# 9. display formatters
def fmt(kind, value):
    m = M('s04'); out = []
    DISP = 0x20005000; m.w32(DISP + 0x58, 0x20005800); m.w8(0x20005900, value)
    m.stubs = {0x0801c9ac: ('val', 0x20005900), 0x0801d580: ('chr', None), 0x0801d660: ('str', None)}
    base = 0x0801f2a8 if kind == 'note' else 0x0801f2e4
    c = m.call(base, DISP)
    if kind == 'note': return ''.join(chr(x[3]) for x in sorted([y for y in c if y[0] == 'chr'], key=lambda y: y[2]))
    s = [y for y in c if y[0] == 'str'][0][2]; return bytes(m.uc.mem_read(s, 3)).decode()
names = [fmt('note', v) for v in (24, 25, 26, 36, 50, 60, 61, 70, 71, 108)]
check('note names (middle C = C3)', names == ['C 0', 'Db0', 'D 0', 'C 1', 'D 2', 'C 3', 'Db3', 'Bb3', 'B 3', 'C 7'], names)
modes = [fmt('mode', v) for v in (0, 1, 2)]
check('mode names', modes == ['1  ', '2  ', 'ALL'], modes)
print('RESULT', 'ALL PASS' if ok else 'FAILURES'); sys.exit(0 if ok else 1)
