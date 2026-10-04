#!/usr/bin/env python3
"""Emulator checks for the keyboard split (seqmute-03 vs seqmute-11). Not a device test."""
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
       's11': image('local/seqmute-11/keystep37_Firmware_Update_1_1_6_579_seqmute11.led')}
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
import random, subprocess
SYM = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.check_output(['llvm-nm', '/tmp/s11.elf'], text=True).split('\n') if len(l.split()) == 3}
CHORD = 0x20001e04
KBCH = 3
def sc_f(b): return 128 if b == 0 else 99 + b
def of_f(b): return 0 if b == 0 else ((b * 25) >> 8) - 12
def fi_f(b): return 0 if b == 0 else ((b * 199) >> 8) - 99
class Model:
    def __init__(s, tracks=1, scl=0, off=0, fin=0): s.T, s.p = tracks, (scl, off, fin); s.reset()
    def reset(s): s.slot = [0] * 8; s.gate = [0] * 4; s.sus = [0] * 8; s.last = 0; s.out = []
    def m(s, t, st, a, b): s.out.append((st | ((KBCH + t) & 15), a, b))
    def level(s, v, val): s.m(v >> 1, 0xb0, 69 + 9 * (v & 1), val)
    def pitch(s, v):
        scl, off, fin = s.p; note = s.slot[v] & 0x7f
        x = 8192 + (note - 64 + of_f(off)) * sc_f(scl) + fi_f(fin); x = max(0, min(16383, x))
        s.m(v >> 1, 0xb0, 16 + (v & 1), x >> 7); s.m(v >> 1, 0xb0, 48 + (v & 1), x & 127)
    def gate_off(s, t):
        if s.gate[t]: s.m(t, 0x80, s.gate[t] & 0x7f, 0x40); s.gate[t] = 0
    def find(s, code):
        for v in range(2 * s.T):
            if s.slot[v] == code: return v
        return None
    def down(s, p, vel):
        code = p | 0x80
        v = s.find(code)
        if v is None: v = s.find(0)
        if v is None:
            sv = [x for x in range(2 * s.T) if s.sus[x]]
            v = sv[0] if sv else s.last                                    # a voice kept alive only by Hold, else the newest key's
        s.sus[v] = 0
        t = v >> 1
        s.gate_off(t)
        s.slot[v] = code
        a, b = s.slot[2 * t], s.slot[2 * t + 1]
        if a and b:
            if a > b: s.slot[2 * t], s.slot[2 * t + 1] = b, a              # lower note = oscillator 1
            vf = 2 * t if s.slot[2 * t] == code else 2 * t + 1
            s.pitch(2 * t); s.pitch(2 * t + 1); s.level(2 * t, 127); s.level(2 * t + 1, 127)
        else:
            vf = v; s.pitch(v); s.level(v, 127); s.level(v ^ 1, 0)
        s.gate[t] = code; s.m(t, 0x90, p, vel); s.last = vf
    def up(s, p, hold):
        v = s.find(p | 0x80)
        if v is None: return
        if hold: s.sus[v] = 1; return                                      # Hold: the key keeps sounding
        s.slot[v] = 0; t = v >> 1
        if s.slot[v ^ 1]: s.level(v, 0)
        else: s.gate_off(t)
    def hold_off(s):
        for t in range(4):
            cl = [v for v in (2 * t, 2 * t + 1) if s.sus[v]]
            if not cl: continue
            for v in cl: s.slot[v] = 0; s.sus[v] = 0
            if s.slot[2 * t] or s.slot[2 * t + 1]:
                for v in cl: s.level(v, 0)
            else: s.gate_off(t)
    def restore(s):
        for t in range(4): s.gate_off(t)
        s.slot = [0] * 8; s.sus = [0] * 8; s.last = 0
        for v in range(2 * s.T):
            s.m(v >> 1, 0xb0, 16 + (v & 1), 64); s.m(v >> 1, 0xb0, 48 + (v & 1), 0); s.level(v, 127)

def run_toggle(m):
    sp0 = 0x2000e000
    for i in range(4): m.w32(sp0 + 0x14 + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x14 + 16, RET | 1)
    m.stubs[0x0801cd5a] = ('show', None); m.calls = []
    ent = struct.unpack('<H', bytes(m.uc.mem_read(0x0801786c, 2)))[0]
    for reg, v in ((UC_ARM_REG_SP, sp0 + 0x14 - 0x14), (UC_ARM_REG_R0, U), (UC_ARM_REG_R4, U)): m.uc.reg_write(reg, v)
    m.uc.reg_write(UC_ARM_REG_SP, sp0)
    # the stock router frame is 0x48 bytes; our handler pops its own regs and branches to the epilogue: add sp,#0x48; pop {r4,r5,r6,pc}
    for i in range(3): m.w32(sp0 + 0x48 + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x48 + 12, RET | 1)
    m.uc.emu_start((0x0801786c + 2 * ent) | 1, RET, count=200000)
    return [(c[2] & 0xff, (c[2] >> 8) & 0xff, (c[2] >> 16) & 0xff) for c in m.calls if c[0] == 'midi'], [c for c in m.calls if c[0] == 'show']
def boot_machine(tracks=1, scl=0, off=0, fin=0, hold=0, which='s11'):
    m = kb(which, hold=hold)
    for i in range(16): m.w8(CHORD + 0x82 + i, 0x10)                       # stock constructor fill
    m.w8(CHORD + 9, 3 - (tracks - 1)); m.w8(CHORD + 0xb2, scl); m.w8(CHORD + 0xb3, off); m.w8(CHORD + 0x48, fin)
    return m
def rr(tracks=1, scl=0, off=0, fin=0, hold=0):
    m = boot_machine(tracks, scl, off, fin, hold); msgs, show = run_toggle(m)
    assert msgs == [] and m.r8(CHORD + 9) & 0x80 and show[0][4] == 1 and bytes(m.uc.mem_read(CHORD + 0x82, 16)) == bytes(16)
    return m
def ev(m, on, p, vel=100, src=0):
    msg = ((0x90 if on else 0x80) | KBCH) | (p << 8) | ((vel if on else 0x40) << 16)
    r = []
    for c in m.call(0x0801b750, NOTE, msg, src, 0):
        if c[0] == 'midi': msg_ = c[2] & 0xffffff; r.append((msg_ & 0xff, (msg_ >> 8) & 0xff, (msg_ >> 16) & 0xff))
        elif c[0] == 'out' and c[4] != 1: r.append(('OUT', c[2] & 0xffffff))
    return r
def hold_release(m):
    m.w8(HO + 5, 1); c = m.call(SYM['hold_off_button'], HO, 1); return [(x[2] & 0xff, (x[2] >> 8) & 0xff, (x[2] >> 16) & 0xff) for x in c if x[0] == 'midi']
def mm(model): o = model.out; model.out = []; return o

# 1. round robin off: identical to seqmute-03 whatever the state bytes hold
bad = n = 0
for b9, p1, p2, p3 in ((0, 0, 0, 0), (3, 7, 99, 200), (0x1f, 255, 255, 255)):
    for seq, tr, hold in itertools.product((0, 1), (0, 1, 2), (0, 1)):
        res = []
        for w in ('s03', 's11'):
            m = kb(w, seq, tr, hold=hold); m.w8(CHORD + 9, b9); m.w8(CHORD + 0xb2, p1); m.w8(CHORD + 0xb3, p2); m.w8(CHORD + 0x48, p3)
            for i in range(16): m.w8(CHORD + 0x82 + i, 0x10)
            res.append(([m.key(1, 60), m.key(1, 64), m.key(0, 60), m.key(1, 60), m.key(0, 64), m.key(0, 60)], bytes(m.uc.mem_read(NOTE, 0x520))))
        n += 1; bad += res[0][0] != res[1][0] or res[0][1][:0x490] != res[1][1][:0x490] or res[0][1][0x496:] != res[1][1][0x496:]
check(f'round robin off: keyboard path identical to seqmute-03 ({n} states x 6 events)', bad == 0, bad)

# 2. one track, hand-checked
m = rr(1); mo = Model(1)
a = ev(m, 1, 60, 100); mo.down(60, 100)
check('track 1, first key: osc 1 pitch (MSB 60, LSB 0), level 127, osc 2 muted, gate note on', a == mm(mo) == [(0xb3, 16, 60), (0xb3, 48, 0), (0xb3, 69, 127), (0xb3, 78, 0), (0x93, 60, 100)], a)
b = ev(m, 1, 67, 90); mo.down(67, 90)
check('second key higher: gate retrigger, osc 1 keeps 60, osc 2 gets 67', b == mm(mo) == [(0x83, 60, 0x40), (0xb3, 16, 60), (0xb3, 48, 0), (0xb3, 17, 67), (0xb3, 49, 0), (0xb3, 69, 127), (0xb3, 78, 127), (0x93, 67, 90)], b)
c = ev(m, 1, 55, 80); mo.down(55, 80)
check('third key lower than both: takes the newest voice, lower note ends up on osc 1, higher on osc 2', c == mm(mo) and (0xb3, 16, 55) in c and (0xb3, 17, 60) in c and c[-1] == (0x93, 55, 80), c)
d = ev(m, 0, 67); mo.up(67, 0)
check('releasing the key that lost its oscillator (67) does nothing', d == mm(mo) == [], d)
e = ev(m, 0, 60); mo.up(60, 0)
check('releasing 60 (osc 2) while 55 (osc 1) is held: osc 2 muted, gate stays', e == mm(mo) == [(0xb3, 78, 0)], e)
f = ev(m, 0, 55); mo.up(55, 0)
check('last key up closes the gate note (the newest key\'s)', f == mm(mo) == [(0x83, 55, 0x40)], f)
# 2b. simultaneous keys, second one lower: the resident note moves to osc 2 and must be unmuted
m = rr(1); mo = Model(1)
ev(m, 1, 64); mo.down(64, 100); mm(mo)
a = ev(m, 1, 60); mo.down(60, 100)
check('second key lower than the first: both oscillators get level 127 (the resident key moved to osc 2, which was muted)', a == mm(mo) and (0xb3, 69, 127) in a and (0xb3, 78, 127) in a and (0xb3, 16, 60) in a and (0xb3, 17, 64) in a, a)
# 2c. Hold
m = rr(1); mo = Model(1)
for p in (60, 67): ev(m, 1, p); mo.down(p, 100)
mm(mo); m.w8(HO + 5, 1)
r1 = ev(m, 0, 60); mo.up(60, 1); r2 = ev(m, 0, 67); mo.up(67, 1)
check('Hold on: releasing keys sends nothing (every key keeps sounding, no oscillator muted)', r1 == r2 == mm(mo) == [], (r1, r2))
r3 = hold_release(m); mo.hold_off()
check('Hold released: the track ends with one Note Off', r3 == mm(mo) == [(0x83, 67, 0x40)], r3)
m = rr(1); mo = Model(1)
for p in (60, 67): ev(m, 1, p); mo.down(p, 100)
mm(mo); m.w8(HO + 5, 1); ev(m, 0, 60); mo.up(60, 1)
n1 = ev(m, 1, 72, 90); mo.down(72, 90)
check('Hold on, both voices taken (one sustained by Hold): a new key takes the sustained voice and retriggers', n1 == mm(mo) and n1[-1] == (0x93, 72, 90), n1)
m.w8(HO + 5, 0); n2 = hold_release(m); mo.hold_off()
check('Hold released while a key is still down: freed oscillator muted, gate stays', n2 == mm(mo), n2)
# 3. four tracks: channels follow the track
m = rr(4); mo = Model(4)
seq_ = [(1, 60), (1, 64), (1, 67), (1, 72), (0, 64), (1, 50), (0, 60), (0, 67), (0, 72), (0, 50)]
ok3 = True
for on, p in seq_:
    got = ev(m, on, p); (mo.down(p, 100) if on else mo.up(p, 0)); exp = mm(mo); ok3 &= got == exp
chs = {x[0] & 15 for x in ev(rr(4), 1, 60)} 
check('four tracks, a chord and its release: messages == model, channels = keyboard channel + track', ok3, ok3)
m = rr(4); mo = Model(4)
for p in (60, 62, 64):
    ev(m, 1, p); mo.down(p, 100)
t3 = [x for x in mo.out if x[0] == 0x94]
check('three keys with four tracks: key 1+2 in track 1 (channel 4), key 3 alone in track 2 (channel 5, other osc muted)', (0xb4, 78, 0) in mo.out and (0x94, 64, 100) in mo.out and (0x93, 62, 100) in mo.out and (0xb3, 17, 62) in mo.out, mo.out)
# 4. pitch maths (all tracks share it)
for scl, off, fin in itertools.product((0, 1, 28, 255), (0, 1, 128, 255), (0, 1, 128, 255)):
    m = rr(1, scl, off, fin); mo = Model(1, scl, off, fin); bad = 0
    for p in (0, 1, 24, 52, 63, 64, 65, 76, 100, 127):
        a = ev(m, 1, p); mo.down(p, 100); bad += a != mm(mo); a = ev(m, 0, p); mo.up(p, 0); bad += a != mm(mo)
    check(f'pitch maths and clamp: scale byte {scl}, offset byte {off}, fine byte {fin}', bad == 0)
# 5. random differential, 1..4 tracks, Hold, repeated presses
rng = random.Random(10); fails = 0; events = 0
for t in range(600):
    tracks = rng.randint(1, 4); scl, off, fin = rng.choice([0, 1, 255, rng.randrange(256)]), rng.choice([0, 255, rng.randrange(256)]), rng.choice([0, 255, rng.randrange(256)])
    m = rr(tracks, scl, off, fin); mo = Model(tracks, scl, off, fin); held = set(); hold = 0; bad = False
    for _ in range(rng.randint(5, 70)):
        r = rng.random()
        if r < 0.07:
            hold ^= 1; m.w8(HO + 5, hold)
            if not hold:
                got = hold_release(m); mo.hold_off(); exp = mm(mo); m.w8(HO + 5, 0); bad |= got != exp
            continue
        if held and (r < 0.5 or len(held) >= 12):
            p = rng.choice(sorted(held)); held.discard(p); got = ev(m, 0, p); mo.up(p, hold); events += 1
        else:
            p = rng.choice(sorted(held)) if held and rng.random() < 0.1 else rng.randrange(20, 110)
            held.add(p); vel = rng.randrange(1, 128); got = ev(m, 1, p, vel); mo.down(p, vel); events += 1
        bad |= got != mm(mo)
    fails += bad
check(f'600 random sessions with 1-4 tracks ({events} key events) == reference model, message for message', fails == 0, fails)
m = rr(2); a = ev(m, 1, 72, src=1)
check('MIDI-in notes bypass round robin', a == [('OUT', (0x90 | KBCH) | 72 << 8 | 100 << 16)], a)
# 6. toggle off: gates closed, voices in use restored
for tracks in (1, 2, 4):
    m = rr(tracks); mo = Model(tracks)
    for p in (60, 70, 40): ev(m, 1, p); mo.down(p, 100)
    mm(mo); mo.restore(); exp = mm(mo)
    msgs, show = run_toggle(m)
    check(f'Shift+Hold off with {tracks} track(s): open gates closed, tune 0 and level 127 on every voice in use, state cleared, shows OFF',
          msgs == exp and show[0][4] == 0 and not (m.r8(CHORD + 9) & 0x80) and bytes(m.uc.mem_read(CHORD + 0x82, 16)) == bytes(16), (msgs, exp))
# 7. knobs
KN = 0x20004000
def knob(idx, raw, b9=0x83):
    m = M('s11'); m.w8(CHORD + 9, b9)
    for off in (0xb2, 0xb3, 0x48): m.w8(CHORD + off, 0)
    m.w8(KN + 0x58, idx); m.w8(KN + 0x59, raw)
    sp0 = 0x2000e000
    for i in range(9): m.w32(sp0 + 0x5c + 4 * i, 0xA0A00000 + i)
    m.w32(sp0 + 0x5c + 32, RET | 1)
    m.stubs[0x0801cd5a] = ('show', None); m.calls = []
    for reg, v in ((UC_ARM_REG_R4, KN), (UC_ARM_REG_SP, sp0)): m.uc.reg_write(reg, v)
    tbl = 0x0800496c; ent = struct.unpack('<H', bytes(m.uc.mem_read(tbl + 2 * (idx - 1), 2)))[0]
    m.uc.emu_start((tbl + 2 * ent) | 1, RET, count=100000)
    return m, [c for c in m.calls if c[0] == 'show']
for raw in (0, 1, 2, 100, 127, 128, 129, 200, 254, 255):
    b = max(raw, 1)
    m, show = knob(1, raw); check(f'knob 1 raw {raw}: scale byte {b}, shows {99 + b if raw else 100}', m.r8(CHORD + 0xb2) == b and show[0][4] == b - 1 and show[0][2] & 1 and m.r8(CHORD + 9) == 0x83, (m.r8(CHORD + 0xb2), show))
    m, show = knob(2, raw); check(f'knob 2 raw {raw}: offset byte {b}, shows {of_f(b):+d} semitones', m.r8(CHORD + 0xb3) == b and show[0][4] == of_f(b) + 128 and show[0][2] & 1, (m.r8(CHORD + 0xb3), show))
    m, show = knob(3, raw); check(f'knob 3 raw {raw}: fine byte {b}, shows {fi_f(b):+d} units', m.r8(CHORD + 0x48) == b and show[0][4] == fi_f(b) + 128 and show[0][2] & 1, (m.r8(CHORD + 0x48), show))
for raw in (0, 63, 64, 127, 128, 191, 192, 255):
    idx = (raw * 4) >> 8
    m, show = knob(4, raw, 0x83); check(f'knob 4 raw {raw}: {idx + 1} track(s), on bit kept, name index {2 + idx}', m.r8(CHORD + 9) == 0x80 | (3 - idx) and show[0][4] == 2 + idx and show[0][2] & 1, (hex(m.r8(CHORD + 9)), show))
# 8. display formatters
def fmt(addr, value):
    m = M('s11'); DISP = 0x20005000; m.w32(DISP + 0x58, 0x20005800); m.w8(0x20005900, value)
    m.stubs = {0x0801c9ac: ('val', 0x20005900), 0x0801d660: ('str', None), 0x0801d580: ('chr', None)}
    c = m.call(addr, DISP)
    chars = [y for y in c if y[0] == 'chr']
    if chars: return ''.join(chr(y[3]) for y in sorted(chars, key=lambda y: y[2]))
    s = [y for y in c if y[0] == 'str'][0][2]; return bytes(m.uc.mem_read(s, 3)).decode()
check('scale display (value + 100)', all(fmt(SYM['fmt_scale'], v) == f'{v + 100:03d}' for v in range(255)))
check('signed display (value - 128)', all(fmt(SYM['fmt_signed'], v + 128) == f'{v:+03d}' for v in range(-99, 100)))
check('names OFF, ON, 1, 2, 3, 4', [fmt(SYM['fmt_mode'], v) for v in range(6)] == ['OFF', 'ON ', '1  ', '2  ', '3  ', '4  '])
# 9. stock literal pool words used by the CC-page code are intact; nothing outside reads what we replaced
check('stock literal pool words used by the CC-page code (0x0800531c..0x08005333) are unchanged', IMG['stock'][0x0800531c - 0x08004000:0x08005334 - 0x08004000] == IMG['s11'][0x0800531c - 0x08004000:0x08005334 - 0x08004000])
check('no code outside the replaced area reads a word inside it (check_pools.py)', subprocess.run([sys.executable, 'local/seqmute-11/check_pools.py', '0x08005334', '0x080059ec', '0x0800527c', '0x08005334'], capture_output=True).returncode == 0)
print('RESULT', 'ALL PASS' if ok else 'FAILURES'); sys.exit(0 if ok else 1)
