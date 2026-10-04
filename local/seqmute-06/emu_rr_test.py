#!/usr/bin/env python3
"""Emulator checks for the keyboard split (seqmute-03 vs seqmute-06). Not a device test."""
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
       's06': image('local/seqmute-06/keystep37_Firmware_Update_1_1_6_579_seqmute06.led')}
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

import random
ARITY.update({0x0801cd5a: 4, 0x0801d580: 3, 0x0801d660: 2})
CHORD = 0x20001e04
CH = 3
OFFS = [0, -1, 1, -12, 12]; SCALES = [128, 171, 341]
class Model:
    def __init__(s, scale=0, off=0): s.scale, s.off = scale, off; s.reset()
    def reset(s): s.count = 0; s.slot = [0, 0]; s.oldest = 0; s.pending = False; s.gate = 0; s.out = []
    def m(s, st, a, b): s.out.append((st | CH, a, b))
    def level(s, o, v): s.m(0xb0, 69 if o == 0 else 78, v)
    def pitch(s, o, note):
        sc = SCALES[s.scale if s.scale < 3 else 0]
        v = 8192 + (note - 64 + OFFS[s.off]) * sc; v = max(0, min(16383, v))
        s.m(0xb0, 16 + o, v >> 7); s.m(0xb0, 48 + o, v & 127)
    def gate_off(s):
        if s.gate: s.m(0x80, s.gate & 0x7f, 0x40); s.gate = 0
        s.pending = False
    def down(s, p, vel):
        first = s.count == 0
        if first:
            if s.pending: s.gate_off()
            s.slot = [0, 0]
        if not s.slot[0]: o = 0
        elif not s.slot[1]: o = 1
        else: o = s.oldest
        s.slot[o] = p | 0x80; s.oldest = 1 - o; s.count += 1
        s.pitch(o, p); s.level(o, 127)
        if first:
            s.level(1 - o, 0); s.gate = p | 0x80; s.m(0x90, p, vel)
    def up(s, p, hold):
        if s.count == 0: return
        s.count -= 1
        for o in (0, 1):
            if s.slot[o] == p | 0x80:
                s.slot[o] = 0
                if s.count: s.level(o, 0)
                break
        if s.count == 0:
            if hold: s.pending = True
            else: s.gate_off()
    def hold_off(s):
        if s.pending: s.gate_off()
    def restore(s):
        s.gate_off(); s.count = 0; s.slot = [0, 0]
        for o in (0, 1): s.m(0xb0, 16 + o, 64); s.m(0xb0, 48 + o, 0); s.level(o, 127)

def rr(scale=0, off=0, on=True, hold=0, which='s06'):
    m = kb(which, hold=hold); m.w8(CHORD + 9, (0x80 if on else 0) | scale | (off << 2)); return m
def ev(m, on, p, vel=100, src=0):
    msg = ((0x90 if on else 0x80) | 3) | (p << 8) | ((vel if on else 0x40) << 16)
    r = []
    for c in m.call(0x0801b750, NOTE, msg, src, 0):
        if c[0] == 'midi': msg_ = c[2] & 0xffffff; r.append((msg_ & 0xff, (msg_ >> 8) & 0xff, (msg_ >> 16) & 0xff))
        elif c[0] == 'out' and c[4] != 1: r.append(('OUT', c[2] & 0xffffff))
    return r
def hold_release(m):
    m.w8(HO + 5, 1); c = m.call(0x0801f31e, HO, 1); return [(x[2] & 0xff, (x[2] >> 8) & 0xff, (x[2] >> 16) & 0xff) for x in c if x[0] == 'midi']
def mm(model): o = model.out; model.out = []; return o

# 1. RR off: identical to seqmute-03 (incl. note-object RAM), any scale/offset bits
bad = n = 0
for bits in (0, 3, 0x1c, 0x1f):
    for seq, tr, hold in itertools.product((0, 1), (0, 1, 2), (0, 1)):
        res = []
        for w in ('s03', 's06'):
            m = kb(w, seq, tr, hold=hold); m.w8(CHORD + 9, bits)
            res.append(([m.key(1, 60), m.key(1, 64), m.key(0, 60), m.key(1, 60), m.key(0, 64), m.key(0, 60)], bytes(m.uc.mem_read(NOTE, 0x520))))
        n += 1; bad += res[0][0] != res[1][0] or res[0][1][:0x490] != res[1][1][:0x490] or res[0][1][0x496:] != res[1][1][0x496:]
check(f'round robin off: keyboard path identical to seqmute-03 ({n} states x 6 events)', bad == 0, bad)

# 2. hand-checked sequence
m = rr(); mo = Model()
a = ev(m, 1, 60, 100); mo.down(60, 100)
check('first key: pitch CC16/48, level 127 on osc 1, osc 2 muted, gate note on', a == mm(mo) and a[0] == (0xb3, 16, 64 + 60 - 64 + 0) and a[-1] == (0x93, 60, 100), a)
b = ev(m, 1, 67, 90); mo.down(67, 90)
check('second key anywhere: osc 2 pitch CC17/49 + level 127, no new gate note', b == mm(mo) and b[0][1] == 17 and all(x[0] != 0x93 for x in b), b)
c = ev(m, 1, 72, 80); mo.down(72, 80)
check('third key steals the oldest oscillator (osc 1)', c == mm(mo) and c[0][1] == 16, c)
d = ev(m, 0, 67); mo.up(67, 0)
check('releasing osc 2 key while others held: osc 2 muted, gate stays', d == mm(mo) == [(0xb3, 78, 0)], d)
e = ev(m, 0, 60); mo.up(60, 0)
check('releasing a key that lost its oscillator does nothing but count', e == mm(mo) == [], e)
f = ev(m, 0, 72); mo.up(72, 0)
check('last key up closes the gate note', f == mm(mo) == [(0x83, 60, 0x40)], f)
# 3. pitch maths
for scale, off in itertools.product(range(4), range(5)):
    m = rr(scale, off); mo = Model(scale, off)
    bad = 0
    for p in (0, 1, 24, 52, 63, 64, 65, 76, 100, 127):
        a = ev(m, 1, p); mo.down(p, 100); bad += a != mm(mo); a = ev(m, 0, p); mo.up(p, 0); bad += a != mm(mo)
    check(f'pitch/clamp maths, scale index {scale}, offset index {off}', bad == 0)
# 4. random differential against the model, with Hold
rng = random.Random(6); fails = 0; events = 0
for t in range(400):
    scale, off = rng.randrange(4), rng.randrange(5)
    m = rr(scale, off); mo = Model(scale, off); held = set(); hold = 0; bad = False
    for _ in range(rng.randint(5, 60)):
        r = rng.random()
        if r < 0.08:
            hold ^= 1; m.w8(HO + 5, hold)
            if not hold:
                got = [x for x in hold_release(m)]; mo.hold_off(); exp = mm(mo)
                m.w8(HO + 5, 0)
                bad |= got != exp
            continue
        if held and (r < 0.5 or len(held) >= 8):
            p = rng.choice(sorted(held)); held.discard(p); got = ev(m, 0, p); mo.up(p, hold); events += 1
        else:
            p = rng.randrange(20, 110)
            if p in held: continue
            held.add(p); vel = rng.randrange(1, 128); got = ev(m, 1, p, vel); mo.down(p, vel); events += 1
        bad |= got != mm(mo)
    fails += bad
check(f'400 random key/Hold sessions ({events} key events) == reference model, message for message', fails == 0, fails)
# 5. MIDI-in keys are not touched while round robin is on
m = rr(); a = ev(m, 1, 72, src=1)
check('MIDI-in notes bypass round robin', a == [('OUT', (0x90 | 3) | 72 << 8 | 100 << 16)], a)
# 6. toggle through the real router table
def toggle(start, gate_note=0, count=0):
    m = M('s06'); m.w8(CHORD + 9, start); m.w8(CHORD + 0xb1, gate_note); m.w8(CHORD + 0x4b, count); m.w8(CHORD + 0x49, 0xbc)
    sp0 = 0x2000e000
    for i in range(3): m.w32(sp0 + 0x48 + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x48 + 12, RET | 1)
    m.stubs[0x0801cd5a] = ('show', None); m.calls = []
    ent = struct.unpack('<H', bytes(m.uc.mem_read(0x0801786c, 2)))[0]
    for reg, v in ((UC_ARM_REG_SP, sp0), (UC_ARM_REG_R0, U), (UC_ARM_REG_R4, U)): m.uc.reg_write(reg, v)
    m.uc.emu_start((0x0801786c + 2 * ent) | 1, RET, count=100000)
    return m, [(c[2] & 0xff, (c[2] >> 8) & 0xff, (c[2] >> 16) & 0xff) for c in m.calls if c[0] == 'midi'], [c for c in m.calls if c[0] == 'show']
m, msgs, show = toggle(3)
check('Shift+Hold on (boot byte 3): no MIDI, state cleared, shows ON, bit7 set, scale/offset bits kept', msgs == [] and m.r8(CHORD + 9) == 0x83 and show and show[0][4] == 4 and m.r8(CHORD + 0x49) == 0, (msgs, hex(m.r8(CHORD + 9))))
exp_restore = [(0xb3, 16, 64), (0xb3, 48, 0), (0xb3, 69, 127), (0xb3, 17, 64), (0xb3, 49, 0), (0xb3, 78, 127)]
m, msgs, show = toggle(0x83)
check('Shift+Hold off: tune 0 and level 127 restored on both oscillators, shows OFF', msgs == exp_restore and m.r8(CHORD + 9) == 3 and show[0][4] == 3, msgs)
m, msgs, show = toggle(0x83, gate_note=0xbc, count=2)
check('Shift+Hold off with the gate open: gate note off first, then restore', msgs == [(0x83, 0x3c, 0x40)] + exp_restore and m.r8(CHORD + 0xb1) == 0 and m.r8(CHORD + 0x4b) == 0, msgs)
# 7. knobs
KN = 0x20004000
def knob(idx, raw, bits):
    m = M('s06'); m.w8(CHORD + 9, bits); m.w8(KN + 0x58, idx); m.w8(KN + 0x59, raw)
    sp0 = 0x2000e000
    for i in range(9): m.w32(sp0 + 0x5c + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x5c + 32, RET | 1)
    m.stubs[0x0801cd5a] = ('show', None); m.calls = []
    for reg, v in ((UC_ARM_REG_R4, KN), (UC_ARM_REG_SP, sp0)): m.uc.reg_write(reg, v)
    tbl = 0x0800496c; ent = struct.unpack('<H', bytes(m.uc.mem_read(tbl + 2 * (idx - 1), 2)))[0]
    m.uc.emu_start((tbl + 2 * ent) | 1, RET, count=100000)
    return m, [c for c in m.calls if c[0] == 'show']
for raw, want in ((0, 0), (85, 0), (86, 1), (170, 1), (171, 2), (255, 2)):
    m, show = knob(1, raw, 0x9f)
    check(f'knob 1 raw {raw}: scale index {want}, other bits kept, name index {want}', m.r8(CHORD + 9) == (0x9f & ~3) | want and show[0][4] == want and show[0][2] & 1, hex(m.r8(CHORD + 9)))
for raw, want in ((0, 0), (51, 0), (52, 1), (102, 1), (103, 2), (153, 2), (154, 3), (204, 3), (205, 4), (255, 4)):
    m, show = knob(2, raw, 0x83)
    check(f'knob 2 raw {raw}: offset index {want}, other bits kept, name index {5 + want}', m.r8(CHORD + 9) == (0x83 & ~0x1c) | (want << 2) and show[0][4] == 5 + want, hex(m.r8(CHORD + 9)))
for idx in (3, 4):
    m, show = knob(idx, 77, 0x83); check(f'knob {idx} does nothing', m.r8(CHORD + 9) == 0x83 and not show)
# 8. names
def fmt(value):
    m = M('s06'); DISP = 0x20005000; m.w32(DISP + 0x58, 0x20005800); m.w8(0x20005900, value)
    m.stubs = {0x0801c9ac: ('val', 0x20005900), 0x0801d660: ('str', None)}
    c = m.call(0x0801f260, DISP); s = [y for y in c if y[0] == 'str'][0][2]; return bytes(m.uc.mem_read(s, 3)).decode()
names = [fmt(v) for v in range(10)]
check('display names', names == ['128', '171', '341', 'OFF', 'ON ', '0  ', '-1 ', '+1 ', '-12', '+12'], names)
print('RESULT', 'ALL PASS' if ok else 'FAILURES'); sys.exit(0 if ok else 1)
