#!/usr/bin/env python3
"""Emulator (Unicorn, Cortex-M Thumb) differential checks: stock vs candidate.
Evidence level: instruction/register behaviour of the firmware functions under a
synthetic RAM model with stubbed output calls. NOT a device test."""
import sys, struct, random
from pathlib import Path
sys.path.insert(0, '.')
from ks37.container import parse
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UcError
from unicorn.arm_const import *

STOCK = 'local/keystep37_Firmware_Update_1_1_6_579.led'
CAND = 'local/seqmute-02/keystep37_Firmware_Update_1_1_6_579_seqmute02.led'
def image(p):
    return b"".join(r.payload if r.payload is not None else b"\xff"*1024 for r in parse(Path(p).read_bytes()))
IMG = {'stock': image(STOCK), 'cand': image(CAND)}
RET = 0x08000100
U, N, S, BLK, H, BLD, NOTE = 0x20002ddc, 0x20004ed4, 0x20002c4c, 0x200027e4, 0x200051cc, 0x2000063c, 0x20001eb8
OUT0 = 0x20006000  # stand-in for *NOTE (output object)

class M:
    def __init__(s, which, stubs):
        s.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        s.uc.mem_map(0x08000000, 0x40000); s.uc.mem_write(0x08004000, IMG[which])
        s.uc.mem_map(0x20000000, 0x10000)
        s.calls = []; s.stubs = stubs
        s.uc.hook_add(UC_HOOK_CODE, s.hook)
    def hook(s, uc, addr, size, _):
        if addr == RET: uc.emu_stop(); return
        if addr in s.stubs:
            name, ret = s.stubs[addr]
            r = [uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
            s.calls.append((name, r[0], r[1], r[2] & 0xff, r[3] & 0xff))
            if ret is not None: uc.reg_write(UC_ARM_REG_R0, ret)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)
    def w8(s, a, v): s.uc.mem_write(a, bytes([v & 0xff]))
    def w32(s, a, v): s.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def r8(s, a): return s.uc.mem_read(a, 1)[0]
    def ram(s): return bytes(s.uc.mem_read(0x20000000, 0x10000))
    def call(s, fn, *args):
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            s.uc.reg_write(reg, v & 0xffffffff)
        for reg in (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
                    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11):
            s.uc.reg_write(reg, 0xA5A50000 + reg)
        s.uc.reg_write(UC_ARM_REG_SP, 0x2000f000); s.uc.reg_write(UC_ARM_REG_LR, RET | 1)
        s.calls = []
        s.uc.emu_start(fn | 1, RET, count=200000)
        assert s.uc.reg_read(UC_ARM_REG_PC) & ~1 == RET, hex(s.uc.reg_read(UC_ARM_REG_PC))
        assert s.uc.reg_read(UC_ARM_REG_SP) == 0x2000f000, 'SP not restored'
        for reg in (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
                    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11):
            assert s.uc.reg_read(reg) == 0xA5A50000 + reg, 'callee-saved register clobbered'
        return list(s.calls)

def common(m, seq):
    for ptr, val in ((0x20001124, U), (0x20001098, 0x20002bec), (0x20001094, BLD), (0x20001150, S),
                     (0x200010a4, N), (0x20001120, 0x20007000), (0x20001128, 0x20007100)):
        m.w32(ptr, val)
    m.w8(U + 0x0f, seq); m.w32(NOTE, OUT0)
    m.w8(0x200010d4, 3)            # user channel 4
    m.w8(H + 0x4f, 60); m.w32(0x200000c8, 60 if seq else 0xffffffff)

# ---------------- consumer / release ----------------
SEQ_STUBS = {0x0801b6c4: ('note_msg', None), 0x0801b5ea: ('batch_begin', None),
             0x0801b604: ('batch_end', None), 0x0801d76c: ('led', None)}
def seq_setup(m, seq, steps, active=None, retain_mode=0, n28=0):
    common(m, seq)
    m.w32(N, S); m.w32(S, BLK); m.w32(S + 4, BLK)
    m.uc.mem_write(BLK, b'\xff' * 0x400 + bytes([len(steps), 50, 50, 0, retain_mode, 0, 0, 0]))
    for i, voices in enumerate(steps):
        for v, (p, vel) in enumerate(voices):
            m.uc.mem_write(BLK + 16 * i + 2 * v, bytes([p, vel]))
    m.uc.mem_write(N + 5, b'\xff' * 8); m.uc.mem_write(N + 0xd, b'\xff' * 8)
    m.w8(N + 4, 3); m.w8(N + 0x25, 0); m.w8(N + 0x28, n28)
    if active:
        m.uc.mem_write(N + 5, bytes(active)); m.w8(N + 0x25, 1)
def notes(calls):
    return [(c[2] & 0xf0, (c[2] >> 8) & 0xff, c[3]) for c in calls if c[0] == 'note_msg']  # (status, pitch, tag)

ok = True
def check(name, cond, detail=''):
    global ok; ok &= bool(cond); print(('PASS ' if cond else 'FAIL ') + name + (' ' + str(detail) if detail else ''))

CONSUMER, RELEASE, CLEANUP = 0x08013e8c, 0x0801415c, 0x08014258
STEP = [[(60, 100), (64, 90)], [(0x81, 0)], [(0x82, 0)], [(67, 100 | 0x80)]]

for which in ('stock', 'cand'):
    m = M(which, SEQ_STUBS); seq_setup(m, 1, STEP)
    n = notes(m.call(CONSUMER, N, 0))
    if which == 'stock': check('stock SEQ step emits Note On (control)', n == [(0x90, 60, 2), (0x90, 64, 2)], n)
    else:
        check('cand SEQ step emits nothing', n == [], n)
        check('cand SEQ leaves no active-note state', m.r8(N + 0x25) == 0 and m.r8(N + 5) == 0xff)

for st in range(4):                       # normal, tie, rest, tie-flag steps
    for rm in (0, 1):
        for n28 in (0, 1):
            m = M('cand', SEQ_STUBS); seq_setup(m, 1, STEP, active=[62, 65], retain_mode=rm, n28=n28)
            n = notes(m.call(CONSUMER, N, st))
            check(f'cand SEQ releases stale notes unconditionally step={st} retain_mode={rm} n28={n28}',
                  n == [(0x80, 62, 2), (0x80, 65, 2)] and m.r8(N + 0x25) == 0, n)
            n2 = notes(m.call(CONSUMER, N, st)) + notes(m.call(RELEASE, N, st))
            check('   then no further messages (no duplicate Note Off)', n2 == [], n2)

# ARP: candidate must be byte-for-byte equivalent in effect to stock (random differential)
rng = random.Random(37); diffs = 0; trials = 3000
for t in range(trials):
    nsteps = rng.randint(1, 8)
    steps = [[(rng.choice([0x81, 0x82, 0xff] + list(range(36, 96)) * 3), rng.randrange(256)) for _ in range(rng.randint(1, 8))]
             for _ in range(nsteps)]
    act = [rng.randrange(36, 96) for _ in range(rng.randint(1, 8))] if rng.random() < .6 else None
    rm, n28, mode, step = rng.randint(0, 1), rng.randint(0, 1), rng.randrange(8), rng.randrange(nsteps)
    fn = rng.choice([CONSUMER, CONSUMER, RELEASE, CLEANUP])
    res = []
    for which in ('stock', 'cand'):
        m = M(which, SEQ_STUBS); seq_setup(m, 0, steps, active=act, retain_mode=rm, n28=n28)
        m.w8(BLD + 0x10, mode); m.w8(0x200010a8, 1 if rng.random() < 0 else 0)
        try: c = m.call(fn, N, step); res.append((c, m.ram()))
        except (UcError, AssertionError) as e: res.append(('EXC', str(e)))
    diffs += res[0] != res[1]
check(f'ARP: stock == candidate (calls + full RAM) over {trials} random consumer/release/cleanup runs', diffs == 0, f'diffs={diffs}')

# ---------------- keyboard note path ----------------
KBD = 0x0801b750
KSTUBS = {0x0801b384: ('out', None), 0x0801b460: ('chord_out', None), 0x080116e0: ('arp_builder', None),
          0x080160a8: ('kbd_play_channel', 9), 0x08014258: ('cleanup', None), 0x0801348e: ('rec', None),
          0x0801d178: ('disp', None), 0x0801b246: ('chord_active', 0),
          # record-path helpers (sequence writers), stubbed: outside the patched behaviour
          0x08013164: ('rec_a', 0), 0x08013944: ('rec_b', None), 0x0801316a: ('rec_c', None),
          0x08011e88: ('rec_d', 0), 0x08013b84: ('rec_e', None), 0x0801301a: ('rec_f', 1),
          0x080132c2: ('rec_g', 1), 0x08013110: ('seq_len', 16)}
def kbd(which, seq, transport, kbdplay=0, rec=0, chord_gate=0, u14=0):
    m = M(which, KSTUBS); common(m, seq)
    m.call(0x0801b572, NOTE, OUT0)                 # native constructor of the note object
    m.w8(U + 0x10, transport); m.w8(U + 0x11, kbdplay); m.w8(U + 0x12, rec); m.w8(U + 0x14, u14)
    if chord_gate: m.w8(U + 0x0c, 1); m.stubs = dict(KSTUBS); m.stubs[0x0801b246] = ('chord_active', 1)
    tr = []
    for msg in (0x00643c90, 0x00644090, 0x00403c80, 0x00404080):   # on 60, on 64, off 60, off 64 (ch 1 raw)
        tr.append([c for c in m.call(KBD, NOTE, msg, 0, 0) if c[0] != 'chord_active'])
    return tr, m.ram()
def outs(tr): return [[(c[0], c[2] & 0xffffff, c[3], c[4]) for c in ev] for ev in tr]

ref, ref_ram = kbd('stock', 1, 0)
check('stock SEQ stopped: keyboard sends Note On/Off (reference)',
      outs(ref)[0][0][:2] == ('out', 0x643c90) and len([c for ev in ref for c in ev if c[0] == 'out' and c[4] == 0]) == 4, outs(ref))
st, _ = kbd('stock', 1, 2)
check('stock SEQ playing: keyboard does NOT send notes (transposes) (control)', all(c[0] != 'out' for ev in st for c in ev), outs(st))
cd, cd_ram = kbd('cand', 1, 2)
midi = lambda tr: [[c for c in ev if c[0] == 'out' and c[4] == 0] for ev in tr]
check('cand SEQ playing: keyboard MIDI Note On/Off identical to stopped reference', midi(cd) == midi(ref), outs(cd))
for name, kw in (('SEQ stopped', dict(seq=1, transport=0)), ('SEQ paused', dict(seq=1, transport=1)),
                 ('SEQ playing + Kbd Play', dict(seq=1, transport=2, kbdplay=1)),
                 ('SEQ step-record', dict(seq=1, transport=0, rec=1)), ('SEQ rt-record playing', dict(seq=1, transport=2, rec=2)),
                 ('ARP stopped', dict(seq=0, transport=0)), ('ARP playing', dict(seq=0, transport=2)),
                 ('ARP playing + flag11', dict(seq=0, transport=2, kbdplay=1)),
                 ('ARP stopped chord', dict(seq=0, transport=0, chord_gate=1)), ('ARP playing chord', dict(seq=0, transport=2, chord_gate=1)),
                 ('SEQ stopped chord', dict(seq=1, transport=0, chord_gate=1)), ('SEQ stopped u14', dict(seq=1, transport=0, u14=1)),
                 ('ARP playing u14', dict(seq=0, transport=2, u14=1))):
    try:
        a, b = kbd('stock', **kw), kbd('cand', **kw)
        check(f'keyboard path unchanged vs stock: {name}', a == b)
    except (UcError, AssertionError) as e: check(f'keyboard path unchanged vs stock: {name}', False, e)
for name, kw in (('chord', dict(chord_gate=1)), ('u14', dict(u14=1))):
    a, _ = kbd('stock', 1, 0, **kw); b, _ = kbd('cand', 1, 2, **kw); c, _ = kbd('stock', 1, 2, **kw)
    sel = lambda tr: [[x for x in ev if x[0] in ('out', 'chord_out') and x[4] != 1] for ev in tr]
    check(f'cand SEQ playing ({name} mode): same note output as stopped reference', sel(a) == sel(b) and sel(c) != sel(a), (outs(b)))
print('RESULT', 'ALL PASS' if ok else 'FAILURES')
sys.exit(0 if ok else 1)
