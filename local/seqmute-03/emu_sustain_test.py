#!/usr/bin/env python3
"""Emulator checks for the Hold-as-sustain change (stock / seqmute-02 / seqmute-03). Not a device test."""
import sys, struct, itertools
from pathlib import Path
sys.path.insert(0, '.')
from ks37.container import parse
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *
def image(p): return b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in parse(Path(p).read_bytes()))
IMG = {'stock': image('local/keystep37_Firmware_Update_1_1_6_579.led'),
       's02': image('local/seqmute-02/keystep37_Firmware_Update_1_1_6_579_seqmute02.led'),
       's03': image('local/seqmute-03/keystep37_Firmware_Update_1_1_6_579_seqmute03.led')}
RET, U, H, BLD, NOTE, OUT0, HO, SET = 0x08000100, 0x20002ddc, 0x200051cc, 0x2000063c, 0x20001eb8, 0x20006000, 0x20006400, 0x20006500
CS = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
KSTUBS = {0x0801b384: ('out', None), 0x0801b460: ('chord_out', None), 0x080116e0: ('arp_builder', None),
          0x080160a8: ('alt_channel', 9), 0x08014258: ('cleanup', None), 0x0801348e: ('rec', None),
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
ON = lambda p, ch=3: ('out', (0x90 | ch) | p << 8 | 100 << 16, 1, 0)
OFF = lambda p, ch=3, route=0: ('out', (0x80 | ch) | p << 8 | 0x40 << 16, 1, route)

# 1. Hold off: s03 identical to s02 in every keyboard state
n = bad = 0
for seq, tr, kp, rec, u14, src in itertools.product((0, 1), (0, 1, 2), (0, 1), (0, 1, 2), (0, 1), (0, 1)):
    res = []
    for w in ('s02', 's03'):
        m = kb(w, seq, tr, kp); m.w8(U + 0x12, rec); m.w8(U + 0x14, u14)
        res.append(([m.key(1, 60, src), m.key(1, 64, src), m.key(0, 60, src), m.key(1, 60, src), m.key(0, 64, src), m.key(0, 60, src)],
                    bytes(m.uc.mem_read(NOTE, 0x520)), bytes(m.uc.mem_read(U, 0x20))))
    n += 1; bad += res[0] != res[1]
check(f'Hold off: keyboard path identical to seqmute-02 ({n} states x 6 events, calls + note-object RAM)', bad == 0, f'mismatches={bad}')

# 2. Hold on, arp not running: Note Off withheld, released on Hold off
for name, seq, tr in (('SEQ stopped', 1, 0), ('SEQ playing', 1, 2), ('ARP stopped', 0, 0)):
    m = kb('s03', seq, tr, hold=1)
    a, b = m.key(1, 60), m.key(0, 60)
    check(f'{name}, Hold on: Note On sent, Note Off withheld', midi(a) == [ON(60)] and midi(b) == [] and m.flag(60) == 6, (a, b))
    c, d = m.key(1, 64), m.key(1, 67); e = m.key(0, 67)
    m.w8(SET + 0xab, 1)
    rel = [(x[0], x[2] & 0xffffff, x[3], x[4]) for x in m.call(0x0801f246, HO, 1)]          # Hold-off hook (button)
    check(f'{name}: Hold off releases exactly the withheld notes, not the key still down',
          rel == [OFF(60), OFF(67)] and m.flag(60) == 0 and m.flag(67) == 0 and m.flag(64) == 2 and m.r8(HO + 5) == 0, rel)
    f = m.key(0, 64)
    check(f'{name}: key still down releases normally afterwards', midi(f) == [OFF(64)] and m.flag(64) == 0, f)
    rel2 = m.call(0x0801f246, HO, 1); check(f'{name}: second Hold off sends nothing', [x for x in rel2 if x[0] == 'out'] == [])
# 3. retrigger
m = kb('s03', 1, 2, hold=1); m.key(1, 60); m.key(0, 60); a = m.key(1, 60)
check('Hold on: re-pressing a sustained key sends its Note Off, then a new Note On', midi(a) == [OFF(60), ON(60)] and m.flag(60) == 2, a)
# 4. arp engaged: unchanged
res = []
for w in ('stock', 's03'):
    m = kb(w, 0, 2, hold=1); res.append(([m.key(1, 60), m.key(1, 64), m.key(0, 60), m.key(0, 64)], bytes(m.uc.mem_read(NOTE, 0x520))))
check('ARP playing, Hold on: identical to stock (keys feed the arp, nothing withheld)', res[0] == res[1] and any(c[0] == 'arp_builder' for c in res[1][0][0]))
# 5. MIDI-in notes are not sustained
m = kb('s03', 1, 0, hold=1); m.key(1, 60, src=1); a = m.key(0, 60, src=1)
check('Hold on: notes arriving on MIDI in are released normally', [c for c in a if c[0] == 'out' and c[3] == 0] == [('out', 0x403c83, 2, 0)] and m.flag(60) == 0, a)
# 6. alternate-channel keyboard notes
m = kb('s03', 1, 2, kbdplay=1, hold=1); a = m.key(1, 60); b = m.key(0, 60)
rel = [(x[0], x[2] & 0xffffff, x[3], x[4]) for x in m.call(0x0801f246, HO, 1) if x[0] == 'out']
check('alternate-channel keyboard mode: withheld, then released on that channel/route', midi(b) == [] and rel == [OFF(60, ch=9, route=2)], (b, rel))
# 7. pedal-off hook: releases only when the Hold state is actually cleared
m = kb('s03', 1, 0, hold=1); m.key(1, 60); m.key(0, 60)
r1 = [x for x in m.call(0x0801f24a, HO, 2) if x[0] == 'out']            # pedal mode without Hold bit: state stays
r2 = [x for x in m.call(0x0801f24a, HO, 1) if x[0] == 'out']            # pedal mode with Hold bit: stock clears state
check('pedal off: no release while Hold stays on; release when stock clears Hold', r1 == [] and len(r2) == 1 and m.flag(60) == 0)
# 8. real Hold button press function, stock vs s03 (everything outside the Hold object code stubbed)
def button(which, mode, pend):
    m = M(which, native=[(0x08014758, 0x08014b5c), (0x0801f180, 0x0801f26c)]); m.stubs = {0x0801b384: ('out', None)}
    m.w8(SET + 0xab, mode)
    if pend: m.w8(NOTE + 0x496 + 60, 6); m.w8(NOTE + 0x496 + 72, 2)
    tr = []
    for _ in range(3):                                                    # on, off, on
        c = m.call(0x08014a8e, HO, 0x20007000, 0x20007100, BLD, stack=(U, 0x20007200))
        tr.append(([x for x in c if x[0] != 'out'], [(x[2] & 0xffffff, x[3], x[4]) for x in c if x[0] == 'out'], m.r8(HO + 5), m.r8(HO + 6), m.r8(HO + 8)))
    return tr, m.flag(60), m.flag(72)
for mode in (0, 1, 2, 3):
    a, b = button('stock', mode, 0), button('s03', mode, 0)
    check(f'Hold button toggle on/off/on, mode setting {mode}, nothing pending: identical to stock', a == b)
    t, f60, f72 = button('s03', mode, 1); s, _, _ = button('stock', mode, 1)
    same_otherwise = [x[0] for x in t] == [x[0] for x in s] and [x[2:] for x in t] == [x[2:] for x in s]
    check(f'Hold button, mode {mode}: toggle-off releases the withheld note only; LED/CC64/arp-latch calls as stock',
          t[0][1] == [] and t[1][1] == [(0x403c83, 1, 0)] and t[2][1] == [] and f60 == 0 and f72 == 2 and same_otherwise, t[1][1])
print('RESULT', 'ALL PASS' if ok else 'FAILURES'); sys.exit(0 if ok else 1)
